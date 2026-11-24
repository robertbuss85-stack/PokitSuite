"""Local web UI and Dokit bridge for PokitSuite."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.parse
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    from winpty import PtyProcess
except ImportError:  # A normal pipe works, but Dokit may buffer live CSV output.
    PtyProcess = None


APP_DIR = Path(__file__).resolve().parent
DATA_DIR = APP_DIR / "data"
HTML_FILE = APP_DIR / "PokitSuite.html"
PORT = 8765
WINDOWS_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
ANSI_ESCAPE = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|$))")
UNIT_BY_MODE = {
    "AC Voltage": "V AC",
    "DC Voltage": "V DC",
    "AC Current": "A AC",
    "DC Current": "A DC",
    "Resistance": "Ω",
    "Diode": "V",
    "Continuity": "Ω",
    "Temperature": "°C",
}


def find_backend(explicit: str | None = None) -> Path | None:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit).expanduser())
    if os.environ.get("POKITSUITE_DOKIT"):
        candidates.append(Path(os.environ["POKITSUITE_DOKIT"]).expanduser())
    candidates.extend((APP_DIR / "dokit.exe", APP_DIR / "bin" / "dokit.exe"))
    candidates.extend(APP_DIR.rglob("dokit.exe"))
    for candidate in candidates:
        try:
            path = candidate.resolve()
        except OSError:
            continue
        if path.is_file():
            return path
    found = shutil.which("dokit.exe")
    return Path(found).resolve() if found else None


def _json_objects(output: str) -> list[dict]:
    objects: list[dict] = []
    decoder = json.JSONDecoder()
    text = output.strip()
    if not text:
        return objects
    try:
        parsed = json.loads(text)
        if isinstance(parsed, list):
            return [x for x in parsed if isinstance(x, dict)]
        if isinstance(parsed, dict):
            return [parsed]
    except json.JSONDecodeError:
        pass
    # Dokit can emit a pretty-printed JSON object followed by a diagnostic on
    # stderr. Since both streams are combined, parse embedded multi-line JSON
    # before falling back to individual lines.
    pos = 0
    while pos < len(text):
        if text[pos] not in "{[":
            pos += 1
            continue
        try:
            item, end = decoder.raw_decode(text, pos)
            if isinstance(item, dict):
                objects.append(item)
            elif isinstance(item, list):
                objects.extend(x for x in item if isinstance(x, dict))
            pos = end
        except json.JSONDecodeError:
            pos += 1
    if objects:
        return objects
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            item = json.loads(line)
            if isinstance(item, dict):
                objects.append(item)
                continue
        except json.JSONDecodeError:
            pass
        # Some Dokit builds may write multiple JSON objects without line breaks.
        pos = 0
        while pos < len(line):
            try:
                item, end = decoder.raw_decode(line, pos)
                if isinstance(item, dict):
                    objects.append(item)
                pos = end
            except json.JSONDecodeError:
                pos += 1
    return objects


def _numeric_measurement(output: str) -> float | None:
    for item in _json_objects(output):
        for key in ("value", "reading", "measurement", "result", "dcValue", "acValue"):
            value = item.get(key)
            if isinstance(value, (int, float)):
                return float(value)
    # Dokit supports CSV and Text output. Ignore headers/status strings and use
    # the last numeric field on the last data row.
    for line in reversed(output.splitlines()):
        cells = next(csv.reader([line]))
        numbers: list[float] = []
        for cell in cells:
            cleaned = cell.strip().replace("\u2212", "-")
            if re.fullmatch(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?", cleaned):
                try:
                    numbers.append(float(cleaned))
                except ValueError:
                    pass
        if numbers:
            return numbers[-1]
    return None


def _run_cli(backend: Path, args: list[str], timeout: float = 15) -> tuple[int, str]:
    try:
        completed = subprocess.run(
            [str(backend), *args],
            cwd=str(backend.parent),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            creationflags=WINDOWS_NO_WINDOW,
        )
        return completed.returncode, (completed.stdout or "") + (completed.stderr or "")
    except subprocess.TimeoutExpired as exc:
        return 124, f"Zeitüberschreitung nach {timeout:g} Sekunden."
    except OSError as exc:
        return 1, str(exc)


def _cli_failure(code: int, output: str, operation: str) -> str:
    detail = ANSI_ESCAPE.sub("", output).strip()
    if detail:
        return detail
    if code < 0:
        return f"Dokit ist bei {operation} mit Windows-Abbruchcode 0x{code & 0xFFFFFFFF:08X} abgestürzt."
    return f"Dokit-Fehler {code} bei {operation}."


class PokitBridge:
    def __init__(self, backend: Path | None):
        self.backend = backend
        self.lock = threading.RLock()
        self.stop_event = threading.Event()
        self.worker: threading.Thread | None = None
        self.active_process = None
        self.state = {
            "backend": bool(backend),
            "connected": False,
            "deviceName": "Noch nicht verbunden",
            "address": "",
            "signalStrength": None,
            "battery": None,
            "batteryStatus": "",
            "firmware": "",
            "deviceStatus": "",
            "mode": "DC Voltage",
            "unit": "V DC",
            "value": None,
            "samples": [],
            "running": False,
            "message": "Bereit zur Verbindung",
            "startedAt": None,
            "csvPath": "",
            "scope": [],
            "loggerActive": False,
            "loggerMode": "DC Voltage",
            "loggerInterval": 1000,
            "loggerStartedAt": None,
            "loggerSamples": [],
            "loggerCsvPath": "",
        }

    def snapshot(self) -> dict:
        with self.lock:
            return dict(
                self.state,
                samples=[dict(point) for point in self.state["samples"]],
                scope=list(self.state["scope"]),
            )

    def _update_status(self) -> tuple[bool, str]:
        if not self.backend:
            return False, "Dokit wurde nicht gefunden."
        with self.lock:
            address = self.state["address"]
        args = ["status", "--output", "json"]
        if address:
            args.extend(["--device", address])
        code, output = _run_cli(self.backend, args)
        if code != 0:
            return False, output.strip() or f"Dokit-Fehler {code}"
        items = _json_objects(output)
        data = items[0] if items else {}
        battery_obj = data.get("battery") or {}
        fw_obj = data.get("firmwareVersion") or {}
        battery = battery_obj.get("level")
        battery_status = (battery_obj.get("status") or "")
        firmware = f"{fw_obj.get('major', '?')}.{fw_obj.get('minor', '?')}" if fw_obj else ""
        with self.lock:
            if data:
                self.state.update(
                    connected=True,
                    deviceName=data.get("deviceName") or self.state["deviceName"],
                    address=data.get("macAddress") or self.state["address"],
                    battery=battery,
                    batteryStatus=battery_status,
                    firmware=firmware,
                    deviceStatus=(data.get("deviceStatus") or {}).get("label", ""),
                    message="Verbunden",
                )
        return bool(data), ""

    def scan(self) -> dict:
        if not self.backend:
            return {"ok": False, "message": "Dokit-Backend fehlt."}
        with self.lock:
            if self.state["running"]:
                return {"ok": False, "message": "Bitte die laufende Messung zuerst stoppen."}
        code, output = _run_cli(self.backend, ["scan", "--timeout", "5s", "--output", "json"], timeout=12)
        devices = _json_objects(output)
        # Some Dokit builds return a non-zero scan status even though they
        # already emitted a valid discovered device object.
        if code != 0 and not devices:
            message = _cli_failure(code, output, "der Bluetooth-Gerätesuche")
            with self.lock:
                self.state.update(connected=False, address="", signalStrength=None, message=message[:300])
            return {"ok": False, "message": message[:300]}
        if not devices:
            message = output.strip() or "Kein Pokit gefunden. Prüfe, ob das Gerät eingeschaltet und Bluetooth am PC aktiv ist."
            with self.lock:
                self.state.update(connected=False, address="", signalStrength=None, message=message[:300])
            return {"ok": False, "message": message[:300]}
        device = devices[0]
        with self.lock:
            self.state.update(
                connected=True,
                deviceName=device.get("name") or "Pokit",
                address=device.get("address", ""),
                signalStrength=device.get("signalStrength"),
                message="Pokit gefunden – bereit zur Messung",
            )
        # Do not launch a second CLI process just to read status here. Dokit
        # creates a fresh BLE controller for every command; the immediate
        # reconnect is the source of the recurring 15-second timeout.
        return {"ok": True, "message": f"{self.state['deviceName']} gefunden – Messung startet"}

    def refresh_status(self) -> dict:
        ok, error = self._update_status()
        if not ok:
            with self.lock:
                self.state["message"] = error[:300] or "Status konnte nicht gelesen werden."
            return {"ok": False, "message": self.state["message"]}
        return {"ok": True, "message": "Verbunden"}

    def start_measurement(self, mode: str, interval_ms: int) -> dict:
        if not self.backend:
            return {"ok": False, "message": "Dokit-Backend fehlt."}
        if mode not in UNIT_BY_MODE:
            return {"ok": False, "message": "Unbekannte Messart."}
        interval_ms = max(250, min(int(interval_ms), 10000))
        with self.lock:
            if self.state["running"] or (self.worker and self.worker.is_alive()):
                return {"ok": False, "message": "Es läuft bereits eine Messung."}
            address = self.state["address"]
            if not address:
                return {"ok": False, "message": "Erst das Pokit suchen und verbinden."}
            self.stop_event.clear()
            self.state.update(
                running=True,
                mode=mode,
                unit=UNIT_BY_MODE[mode],
                value=None,
                samples=[],
                message="Messverbindung wird aufgebaut – der erste Wert kann bis zu 20 Sekunden dauern",
                startedAt=dt.datetime.now().isoformat(timespec="seconds"),
            )
        self.worker = threading.Thread(target=self._measurement_supervisor, args=(address, mode, interval_ms), daemon=True)
        self.worker.start()
        return {"ok": True, "message": "Live-Messung gestartet"}

    def _measurement_supervisor(self, address: str, mode: str, interval_ms: int) -> None:
        """Retry transient BLE disconnects without requiring another click."""
        for attempt in range(4):
            self._measurement_loop(address, mode, interval_ms)
            if self.stop_event.is_set():
                return
            with self.lock:
                message = self.state["message"]
            transient = any(term in message for term in (
                "RemoteHostClosedError", "Bluetooth controller error",
                "CharacteristicWriteError", "Zeitüberschreitung",
            ))
            if not transient or attempt == 3:
                return
            with self.lock:
                self.state.update(
                    running=True,
                    message=f"Pokit hat die Verbindung beendet – neuer Versuch {attempt + 2}/4 …",
                )
            if self.stop_event.wait(2.0):
                return

    def _measurement_loop(self, address: str, mode: str, interval_ms: int) -> None:
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        path = DATA_DIR / f"live_{stamp}_{mode.lower().replace(' ', '_')}.csv"
        with self.lock:
            self.state["csvPath"] = str(path)
        start_time = time.monotonic()
        try:
            with path.open("w", newline="", encoding="utf-8-sig") as stream:
                writer = csv.writer(stream)
                writer.writerow(["timestamp", "value", "unit", "mode"])
                stream.flush()
                args = [
                    "meter", "--device", address,
                    "--mode", mode,
                    "--range", "auto",
                    "--samples", "100000",
                    "--interval", f"{interval_ms}ms",
                    "--output", "csv",
                ]
                command = [str(self.backend), *args]
                # Dokit buffers stdout when attached to an ordinary pipe. A
                # Windows pseudo terminal makes it flush every live CSV row.
                if PtyProcess is not None:
                    proc = PtyProcess.spawn(command, cwd=str(self.backend.parent), dimensions=(24, 160))
                    read_line = proc.readline
                else:
                    proc = subprocess.Popen(
                        command, cwd=str(self.backend.parent), stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                        errors="replace", bufsize=1, creationflags=WINDOWS_NO_WINDOW,
                    )
                    assert proc.stdout is not None
                    read_line = proc.stdout.readline
                self.active_process = proc
                diagnostics: list[str] = []
                while True:
                    if self.stop_event.is_set():
                        break
                    try:
                        line = read_line()
                    except (EOFError, OSError) as exc:
                        if not self.stop_event.is_set():
                            diagnostics.append(str(exc))
                        break
                    if not line:
                        break
                    value = _numeric_measurement(line)
                    if value is None:
                        cleaned = ANSI_ESCAPE.sub("", line).strip()
                        if cleaned:
                            diagnostics.append(cleaned)
                            del diagnostics[:-8]
                        continue
                    now = dt.datetime.now()
                    writer.writerow([now.isoformat(timespec="milliseconds"), f"{value:.12g}", UNIT_BY_MODE[mode], mode])
                    stream.flush()
                    point = {"t": now.strftime("%H:%M:%S"), "value": value}
                    with self.lock:
                        samples = self.state["samples"]
                        samples.append(point)
                        # Keep up to one hour at the fastest supported UI rate
                        # so the optional t-y recorder can show long trends.
                        del samples[:-7200]
                        self.state.update(value=value, connected=True, deviceStatus="Messung", message="Live-Messung läuft")
                if PtyProcess is not None and isinstance(proc, PtyProcess):
                    # ConPTY handles are invalid as soon as the peer closes.
                    # Do not query/wait on the handle after a user stop.
                    code = 0
                    try:
                        if self.stop_event.is_set() and proc.isalive():
                            proc.terminate(force=True)
                        elif not self.stop_event.is_set() and not proc.isalive():
                            code = proc.exitstatus or 0
                    except (EOFError, OSError):
                        code = 0 if self.stop_event.is_set() else 1
                else:
                    if self.stop_event.is_set() and proc.poll() is None:
                        proc.terminate()
                    try:
                        code = proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        code = proc.wait(timeout=2)
                self.active_process = None
                if code != 0 and not self.stop_event.is_set():
                    detail = " | ".join(diagnostics[-3:])
                    with self.lock:
                        message = f"Dokit wurde mit Fehlercode {code} beendet."
                        if detail:
                            message += f" {detail[:220]}"
                        self.state.update(running=False, message=message)
                elif code == 0 and not self.stop_event.is_set():
                    with self.lock:
                        self.state.update(running=False, message="Messreihe abgeschlossen.")
        except Exception as exc:
            with self.lock:
                self.state.update(running=False, message=f"Messung gestoppt: {exc}")
        finally:
            with self.lock:
                self.state["running"] = False
                if self.state["message"] == "Live-Messung läuft":
                    self.state["message"] = "Messung gestoppt"

    def stop_measurement(self) -> dict:
        self.stop_event.set()
        proc = self.active_process
        if proc:
            try:
                if PtyProcess is not None and isinstance(proc, PtyProcess):
                    # The measurement thread owns this ConPTY handle. Closing
                    # it here races with readline() and causes WinError 6.
                    pass
                elif proc.poll() is None:
                    proc.terminate()
            except (OSError, EOFError):
                pass
        worker = self.worker
        if worker and worker.is_alive() and worker is not threading.current_thread():
            # Usually below one sample interval; during the initial BLE setup
            # this can take longer. Waiting prevents the next feature from
            # fighting the old Dokit process for the device.
            worker.join(timeout=25)
        with self.lock:
            self.state.update(running=False, message="Messung gestoppt")
            return {"ok": True, "message": "Messung gestoppt", "csvPath": self.state["csvPath"]}

    def dso_capture(self, mode: str, sample_rate: str = "10k", window_size: int = 1000,
                    trigger_mode: str = "free", trigger_level: float = 0.0,
                    measurement_range: str = "10V") -> dict:
        if mode not in ("AC Voltage", "DC Voltage", "AC Current", "DC Current"):
            return {"ok": False, "message": "DSO unterstützt AC/DC-Spannung und AC/DC-Strom."}
        with self.lock:
            address = self.state["address"]
            if self.state["running"]:
                return {"ok": False, "message": "Erst die Live-Messung stoppen."}
        if not address:
            return {"ok": False, "message": "Erst das Pokit suchen und verbinden."}
        if sample_rate not in ("1k", "5k", "10k", "50k", "100k", "250k", "500k", "1M"):
            sample_rate = "10k"
        window_size = max(100, min(int(window_size), 10000))
        if trigger_mode not in ("free", "rising", "falling"):
            trigger_mode = "free"
        trigger_level = max(-1000.0, min(float(trigger_level), 1000.0))
        is_current = "Current" in mode
        allowed_ranges = ("10mA", "50mA", "500mA", "2A", "10A") if is_current else ("300mV", "2V", "10V", "30V", "60V", "600V")
        if measurement_range not in allowed_ranges:
            measurement_range = "2A" if is_current else "10V"
        trigger_value = f"{trigger_level:g}{'A' if is_current else 'V'}"
        args = ["dso", "--device", address, "--mode", mode, "--range", measurement_range,
                "--sample-rate", sample_rate, "--window-size", str(window_size)]
        # Dokit's true free-running mode is represented by omitting both
        # trigger arguments. Edge modes require both together.
        if trigger_mode != "free":
            args.extend(["--trigger-mode", trigger_mode, "--trigger-level", trigger_value])
        args.extend(["--output", "csv"])
        code, output = _run_cli(self.backend, args, timeout=60)
        if code != 0:
            return {"ok": False, "message": _cli_failure(code, output, "der Oszilloskop-Aufnahme")[:300]}
        points = []
        for line in output.splitlines():
            value = _numeric_measurement(line)
            if value is not None:
                points.append(value)
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        file_path = DATA_DIR / f"dso_{dt.datetime.now():%Y%m%d_%H%M%S}.csv"
        file_path.write_text(output, encoding="utf-8-sig")
        with self.lock:
            self.state["scope"] = points
            self.state["message"] = f"Oszilloskop: {len(points)} Werte"
        return {"ok": True, "message": f"Oszilloskop-Aufnahme mit {len(points)} Werten", "file": str(file_path),
                "scope": points, "sampleRate": sample_rate, "windowSize": window_size, "range": measurement_range}

    def logger_start(self, mode: str, interval_ms: int) -> dict:
        """Start the Pokit's autonomous on-device data logger."""
        if not self.backend:
            return {"ok": False, "message": "Dokit-Backend fehlt."}
        if mode not in ("AC Voltage", "DC Voltage", "AC Current", "DC Current", "Temperature"):
            return {"ok": False, "message": "Der Logger unterstützt Spannung, Strom und (ab Firmware 1.5) Temperatur."}
        interval_ms = max(500, min(int(interval_ms), 86_400_000))
        with self.lock:
            address = self.state["address"]
            if self.state["running"]:
                return {"ok": False, "message": "Bitte zuerst die Live-Messung stoppen."}
        if not address:
            return {"ok": False, "message": "Erst das Pokit suchen und verbinden."}
        args = ["logger-start", "--device", address, "--mode", mode, "--range", "auto",
                "--interval", f"{interval_ms}ms", "--timestamp", "now", "--output", "json"]
        code, output = _run_cli(self.backend, args, timeout=30)
        if code != 0:
            return {"ok": False, "message": _cli_failure(code, output, "dem Logger-Start")[:300]}
        started = dt.datetime.now().isoformat(timespec="seconds")
        with self.lock:
            self.state.update(loggerActive=True, loggerMode=mode, loggerInterval=interval_ms,
                              loggerStartedAt=started, loggerSamples=[], message="Gerätelogger läuft")
        return {"ok": True, "message": "Gerätelogger gestartet – er zeichnet jetzt selbstständig auf."}

    def logger_stop(self) -> dict:
        if not self.backend:
            return {"ok": False, "message": "Dokit-Backend fehlt."}
        with self.lock:
            address = self.state["address"]
        if not address:
            return {"ok": False, "message": "Kein Pokit verbunden."}
        code, output = _run_cli(self.backend, ["logger-stop", "--device", address, "--output", "json"], timeout=30)
        if code != 0:
            return {"ok": False, "message": _cli_failure(code, output, "dem Logger-Stopp")[:300]}
        with self.lock:
            self.state.update(loggerActive=False, message="Gerätelogger gestoppt")
        return {"ok": True, "message": "Gerätelogger gestoppt. Die Daten können jetzt geladen werden."}

    def logger_fetch(self) -> dict:
        if not self.backend:
            return {"ok": False, "message": "Dokit-Backend fehlt."}
        with self.lock:
            address = self.state["address"]
            mode = self.state["loggerMode"]
            interval_ms = self.state["loggerInterval"]
        if not address:
            return {"ok": False, "message": "Kein Pokit verbunden."}
        code, output = _run_cli(self.backend, ["logger-fetch", "--device", address, "--output", "csv"], timeout=60)
        if code != 0:
            return {"ok": False, "message": _cli_failure(code, output, "dem Laden der Loggerdaten")[:300]}
        values: list[float] = []
        for line in output.splitlines():
            value = _numeric_measurement(line)
            if value is not None:
                values.append(value)
        now = dt.datetime.now()
        points = [{"t": (now - dt.timedelta(milliseconds=interval_ms * (len(values) - i - 1))).strftime("%H:%M:%S"),
                   "value": value} for i, value in enumerate(values)]
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        file_path = DATA_DIR / f"logger_{now:%Y%m%d_%H%M%S}_{mode.lower().replace(' ', '_')}.csv"
        file_path.write_text(output, encoding="utf-8-sig")
        with self.lock:
            self.state.update(loggerSamples=points, loggerCsvPath=str(file_path),
                              message=f"{len(points)} Loggerwerte geladen")
        return {"ok": True, "message": f"{len(points)} Loggerwerte geladen", "samples": points,
                "file": str(file_path), "unit": UNIT_BY_MODE.get(mode, "")}


class Handler(BaseHTTPRequestHandler):
    bridge: PokitBridge

    def log_message(self, *_args) -> None:
        return

    def _send(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _json(self, data: dict, status: int = 200) -> None:
        self._send(status, json.dumps(data, ensure_ascii=False).encode("utf-8"), "application/json; charset=utf-8")

    def do_GET(self) -> None:
        route = urllib.parse.urlparse(self.path).path
        if route == "/":
            if not HTML_FILE.exists():
                self._send(500, b"PokitSuite.html fehlt", "text/plain; charset=utf-8")
                return
            self._send(200, HTML_FILE.read_bytes(), "text/html; charset=utf-8")
        elif route == "/api/state":
            self._json(self.bridge.snapshot())
        else:
            self._send(404, b"Not found", "text/plain; charset=utf-8")

    def do_POST(self) -> None:
        route = urllib.parse.urlparse(self.path).path
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8")) if length else {}
        except (ValueError, json.JSONDecodeError):
            self._json({"ok": False, "message": "Ungültige Anfrage."}, 400)
            return
        if not isinstance(payload, dict):
            self._json({"ok": False, "message": "Die Anfrage muss ein JSON-Objekt enthalten."}, 400)
            return
        if route == "/api/scan":
            self._json(self.bridge.scan())
        elif route == "/api/status":
            self._json(self.bridge.refresh_status())
        elif route == "/api/start":
            try:
                interval = int(payload.get("interval", 1000))
            except (TypeError, ValueError):
                self._json({"ok": False, "message": "Ungültiges Messintervall."}, 400)
                return
            self._json(self.bridge.start_measurement(str(payload.get("mode", "DC Voltage")), interval))
        elif route == "/api/stop":
            self._json(self.bridge.stop_measurement())
        elif route == "/api/dso":
            try:
                window_size = int(payload.get("windowSize", 1000))
                trigger_level = float(payload.get("triggerLevel", 0))
            except (TypeError, ValueError):
                self._json({"ok": False, "message": "Ungültige Scope-Einstellung."}, 400)
                return
            self._json(self.bridge.dso_capture(
                str(payload.get("mode", "DC Voltage")), str(payload.get("sampleRate", "10k")),
                window_size, str(payload.get("triggerMode", "free")), trigger_level,
                str(payload.get("range", "10V"))))
        elif route == "/api/logger/start":
            try:
                interval = int(payload.get("interval", 1000))
            except (TypeError, ValueError):
                self._json({"ok": False, "message": "Ungültiges Loggerintervall."}, 400)
                return
            self._json(self.bridge.logger_start(str(payload.get("mode", "DC Voltage")), interval))
        elif route == "/api/logger/stop":
            self._json(self.bridge.logger_stop())
        elif route == "/api/logger/fetch":
            self._json(self.bridge.logger_fetch())
        else:
            self._send(404, b"Not found", "text/plain; charset=utf-8")


def self_test(backend: Path | None) -> int:
    if not HTML_FILE.is_file():
        print("FAIL: PokitSuite.html fehlt.")
        return 3
    if _numeric_measurement('timestamp,value\n2026-11-19T12:00:00,1.25') != 1.25:
        print("FAIL: CSV-Messwert konnte nicht gelesen werden.")
        return 4
    if _numeric_measurement('{"reading": -0.125}') != -0.125:
        print("FAIL: JSON-Messwert konnte nicht gelesen werden.")
        return 4
    if backend is None:
        print("FAIL: Dokit-Backend wurde nicht gefunden.")
        return 2
    for command in (("--version",), ("--help",), ("meter", "--help"), ("dso", "--help"), ("logger-start", "--help")):
        result = subprocess.run([str(backend), *command], cwd=str(backend.parent), capture_output=True, text=True, errors="replace", timeout=10)
        output = result.stdout + result.stderr
        print(f"{' '.join(command)}: exit={result.returncode}; Ausgabe={output.splitlines()[0] if output.splitlines() else '(leer)'}")
        if result.returncode != 0 or not output.strip():
            return 1
    print(f"PokitSuite self-check OK. Backend: {backend}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="PokitSuite live desktop interface")
    parser.add_argument("--backend", help="Pfad zu dokit.exe")
    parser.add_argument("--self-test", action="store_true", help="Prüft Weboberfläche und Dokit-Backend")
    args = parser.parse_args()
    backend = find_backend(args.backend)
    if args.self_test:
        return self_test(backend)

    Handler.bridge = PokitBridge(backend)
    try:
        server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    except OSError:
        # Another PokitSuite instance already owns the port. Do not open an
        # additional browser tab; the existing instance remains authoritative.
        return 0
    url = f"http://127.0.0.1:{PORT}/"
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        Handler.bridge.stop_measurement()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
