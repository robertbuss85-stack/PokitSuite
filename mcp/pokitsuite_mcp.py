"""Local MCP bridge for PokitSuite and Arduino CLI."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from mcp.server import MCPServer


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = PROJECT_ROOT / "data"
STATE_FILE = DATA_DIR / "pokit_live.json"
COMMAND_FILE = DATA_DIR / "pokit_command.json"
BACKUP_DIR = DATA_DIR / "arduino_backups"
ARDUINO_CLI = Path(os.environ.get(
    "POKITSUITE_ARDUINO_CLI",
    Path.home() / "AppData/Local/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe",
))
ARDUINO_IDE = Path(os.environ.get(
    "POKITSUITE_ARDUINO_IDE",
    Path.home() / "AppData/Local/Programs/Arduino IDE/Arduino IDE.exe",
))
ALLOWED_ROOT = Path(os.environ.get("POKITSUITE_ARDUINO_ROOT", Path.home())).resolve()

mcp = MCPServer(
    "PokitSuite",
    version="0.1.0",
    instructions=(
        "Analyse Pokit measurements and Arduino sketches. Read measurements first, "
        "compile every patch before upload, and preserve a backup. Never upload unless "
        "the tool call explicitly sets confirm_upload=true."
    ),
)


def _read_state() -> dict[str, Any]:
    if not STATE_FILE.exists():
        return {"connected": False, "error": "PokitSuite telemetry is not available"}
    try:
        return json.loads(STATE_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return {"connected": False, "error": f"Invalid telemetry: {exc}"}


def _send_command(action: str, **values: Any) -> dict[str, Any]:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    payload = {"action": action, "timestamp": time.time(), **values}
    temporary = COMMAND_FILE.with_suffix(".tmp")
    temporary.write_text(json.dumps(payload), encoding="utf-8")
    os.replace(temporary, COMMAND_FILE)
    return {"accepted": True, "command": payload}


def _safe_sketch(path: str) -> Path:
    candidate = Path(path).expanduser().resolve()
    try:
        candidate.relative_to(ALLOWED_ROOT)
    except ValueError as exc:
        raise ValueError(f"Sketch must be below {ALLOWED_ROOT}") from exc
    if candidate.is_file():
        candidate = candidate.parent
    ino = candidate / f"{candidate.name}.ino"
    if not ino.exists():
        alternatives = list(candidate.glob("*.ino"))
        if not alternatives:
            raise FileNotFoundError(f"No .ino file in {candidate}")
    return candidate


def _run_cli(arguments: list[str], timeout: int = 180) -> dict[str, Any]:
    if not ARDUINO_CLI.exists():
        return {"ok": False, "error": f"arduino-cli not found: {ARDUINO_CLI}"}
    process = subprocess.run(
        [str(ARDUINO_CLI), *arguments], capture_output=True, text=True,
        timeout=timeout, encoding="utf-8", errors="replace",
    )
    return {
        "ok": process.returncode == 0,
        "exit_code": process.returncode,
        "stdout": process.stdout[-20000:],
        "stderr": process.stderr[-20000:],
        "command": [str(ARDUINO_CLI), *arguments],
    }


def _backup_sketch(sketch: Path) -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    destination = BACKUP_DIR / f"{sketch.name}_{stamp}"
    BACKUP_DIR.mkdir(parents=True, exist_ok=True)
    shutil.copytree(sketch, destination)
    return destination


@mcp.tool()
def get_device_status() -> dict[str, Any]:
    """Return Pokit connection, active feature and current status."""
    state = _read_state()
    return {key: state.get(key) for key in ("timestamp_utc", "connected", "device", "status", "active_feature")}


@mcp.tool()
def get_multimeter_reading() -> dict[str, Any]:
    """Return the latest multimeter value and unit."""
    return _read_state().get("multimeter", {})


@mcp.tool()
def get_scope_metrics() -> dict[str, Any]:
    """Return acquisition settings and calculated signal metrics without waveform samples."""
    scope = dict(_read_state().get("scope", {}))
    scope.pop("waveform_v", None)
    return scope


@mcp.tool()
def get_scope_frame(max_points: int = 1000) -> dict[str, Any]:
    """Return the latest complete scope frame, optionally decimated to max_points."""
    scope = dict(_read_state().get("scope", {}))
    waveform = scope.get("waveform_v", [])
    limit = max(20, min(int(max_points), 1000))
    if len(waveform) > limit:
        step = len(waveform) / limit
        waveform = [waveform[min(int(index * step), len(waveform) - 1)] for index in range(limit)]
    scope["waveform_v"] = waveform
    scope["returned_points"] = len(waveform)
    return scope


@mcp.tool()
def evaluate_scope_reference(expected_frequency_hz: float | None = None,
                             frequency_tolerance_percent: float = 2.0,
                             expected_vpp_v: float | None = None,
                             vpp_tolerance_v: float = 0.2,
                             expected_duty_percent: float | None = None,
                             duty_tolerance_percent: float = 3.0) -> dict[str, Any]:
    """Compare the latest complete frame with expected signal limits."""
    state = _read_state()
    measured = state.get("scope", {}).get("metrics", {})
    violations: list[dict[str, Any]] = []
    checks: dict[str, Any] = {}
    if expected_frequency_hz is not None:
        actual = float(measured.get("frequency_hz", 0))
        tolerance = abs(expected_frequency_hz) * frequency_tolerance_percent / 100.0
        checks["frequency_hz"] = {"expected": expected_frequency_hz, "actual": actual, "tolerance": tolerance}
        if abs(actual - expected_frequency_hz) > tolerance:
            violations.append({"metric": "frequency_hz", "expected": expected_frequency_hz, "actual": actual})
    if expected_vpp_v is not None:
        actual = float(measured.get("vpp_v", 0))
        checks["vpp_v"] = {"expected": expected_vpp_v, "actual": actual, "tolerance": vpp_tolerance_v}
        if abs(actual - expected_vpp_v) > vpp_tolerance_v:
            violations.append({"metric": "vpp_v", "expected": expected_vpp_v, "actual": actual})
    if expected_duty_percent is not None:
        actual = float(measured.get("duty_cycle_percent", 0))
        checks["duty_cycle_percent"] = {"expected": expected_duty_percent, "actual": actual, "tolerance": duty_tolerance_percent}
        if abs(actual - expected_duty_percent) > duty_tolerance_percent:
            violations.append({"metric": "duty_cycle_percent", "expected": expected_duty_percent, "actual": actual})
    return {"pass": not violations, "checks": checks, "violations": violations,
            "measurement_timestamp": state.get("timestamp_utc")}


@mcp.tool()
def wait_for_scope_update(previous_timestamp: str = "", timeout_seconds: float = 15.0) -> dict[str, Any]:
    """Wait for a new complete scope frame, then return its metrics."""
    deadline = time.monotonic() + max(1.0, min(float(timeout_seconds), 60.0))
    while time.monotonic() < deadline:
        state = _read_state()
        if (state.get("timestamp_utc") != previous_timestamp and state.get("scope", {}).get("samples", 0) > 0):
            scope = dict(state["scope"])
            scope.pop("waveform_v", None)
            return {"updated": True, "timestamp_utc": state.get("timestamp_utc"), "scope": scope}
        time.sleep(0.2)
    return {"updated": False, "error": "No new complete scope frame before timeout"}


@mcp.tool()
def find_arduino_sketches(max_results: int = 50) -> dict[str, Any]:
    """Find user sketches in the standard Arduino and Downloads directories."""
    roots = [Path.home() / "Documents/Arduino", Path.home() / "Downloads"]
    sketches: list[str] = []
    for root in roots:
        if not root.exists():
            continue
        for ino in root.rglob("*.ino"):
            if "libraries" in {part.lower() for part in ino.parts}:
                continue
            sketches.append(str(ino.parent))
            if len(sketches) >= max(1, min(max_results, 200)):
                return {"sketches": sorted(set(sketches))}
    return {"sketches": sorted(set(sketches))}


@mcp.tool()
def configure_scope(time_per_div_us: int, volts_per_div: float = 1.0,
                    coupling: str = "DC", trigger_mode: str = "free",
                    trigger_level_v: float = 0.0) -> dict[str, Any]:
    """Configure and start CH1. trigger_mode: free, rising or falling."""
    if coupling not in {"DC", "AC"}:
        raise ValueError("coupling must be DC or AC")
    if trigger_mode not in {"free", "rising", "falling"}:
        raise ValueError("trigger_mode must be free, rising or falling")
    return _send_command("scope_config", time_per_div_us=int(time_per_div_us),
                         volts_per_div=float(volts_per_div), coupling=coupling,
                         trigger_mode=trigger_mode, trigger_level_v=float(trigger_level_v))


@mcp.tool()
def capture_scope_single() -> dict[str, Any]:
    """Switch to scope and request one acquisition."""
    _send_command("show_scope")
    time.sleep(0.3)
    return _send_command("scope_single")


@mcp.tool()
def list_arduino_boards() -> dict[str, Any]:
    """Detect connected Arduino-compatible boards and ports."""
    result = _run_cli(["board", "list", "--json"], timeout=30)
    if result.get("ok"):
        try:
            result["boards"] = json.loads(result["stdout"])
        except json.JSONDecodeError:
            pass
    return result


@mcp.tool()
def read_arduino_sketch(sketch_path: str) -> dict[str, Any]:
    """Read all source files in an Arduino sketch directory."""
    sketch = _safe_sketch(sketch_path)
    files: dict[str, str] = {}
    for pattern in ("*.ino", "*.h", "*.hpp", "*.c", "*.cpp"):
        for source in sketch.glob(pattern):
            files[source.name] = source.read_text(encoding="utf-8", errors="replace")
    return {"sketch": str(sketch), "files": files}


@mcp.tool()
def open_sketch_in_arduino_ide(sketch_path: str) -> dict[str, Any]:
    """Open the sketch's main .ino file in Arduino IDE so edits remain visible."""
    sketch = _safe_sketch(sketch_path)
    main_file = sketch / f"{sketch.name}.ino"
    if not main_file.exists():
        main_file = next(sketch.glob("*.ino"))
    if not ARDUINO_IDE.exists():
        return {"ok": False, "error": f"Arduino IDE not found: {ARDUINO_IDE}"}
    subprocess.Popen([str(ARDUINO_IDE), str(main_file)], close_fds=True)
    return {"ok": True, "opened": str(main_file)}


@mcp.tool()
def read_serial_monitor(port: str, fqbn: str = "", baudrate: int = 115200,
                        duration_seconds: float = 5.0) -> dict[str, Any]:
    """Collect a bounded sample from Arduino CLI serial monitor."""
    arguments = [str(ARDUINO_CLI), "monitor", "--port", port,
                 "--config", f"baudrate={int(baudrate)}", "--timestamp"]
    if fqbn:
        arguments.extend(["--fqbn", fqbn])
    process = subprocess.Popen(arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, encoding="utf-8", errors="replace")
    try:
        stdout, stderr = process.communicate(timeout=max(1.0, min(duration_seconds, 60.0)))
    except subprocess.TimeoutExpired:
        process.terminate()
        stdout, stderr = process.communicate(timeout=5)
    return {"ok": process.returncode in (0, 1), "stdout": stdout[-20000:],
            "stderr": stderr[-10000:], "port": port, "baudrate": baudrate}


@mcp.tool()
def replace_in_sketch(sketch_path: str, file_name: str, old_text: str,
                      new_text: str) -> dict[str, Any]:
    """Back up a sketch and perform one exact, unambiguous source replacement."""
    sketch = _safe_sketch(sketch_path)
    target = (sketch / file_name).resolve()
    target.relative_to(sketch)
    if not target.exists():
        raise FileNotFoundError(target)
    content = target.read_text(encoding="utf-8")
    occurrences = content.count(old_text)
    if occurrences != 1:
        raise ValueError(f"Expected exactly one match, found {occurrences}")
    backup = _backup_sketch(sketch)
    target.write_text(content.replace(old_text, new_text, 1), encoding="utf-8")
    return {"changed": str(target), "backup": str(backup)}


@mcp.tool()
def compile_arduino_sketch(sketch_path: str, fqbn: str,
                           warnings: str = "all") -> dict[str, Any]:
    """Compile an Arduino sketch without uploading it."""
    sketch = _safe_sketch(sketch_path)
    return _run_cli(["compile", "--fqbn", fqbn, "--warnings", warnings, str(sketch)])


@mcp.tool()
def upload_arduino_sketch(sketch_path: str, fqbn: str, port: str,
                          confirm_upload: bool = False) -> dict[str, Any]:
    """Compile and upload a sketch. confirm_upload must explicitly be true."""
    if not confirm_upload:
        return {"ok": False, "blocked": True, "error": "Upload requires confirm_upload=true"}
    sketch = _safe_sketch(sketch_path)
    backup = _backup_sketch(sketch)
    result = _run_cli(["compile", "--fqbn", fqbn, "--port", port,
                       "--upload", "--verify", str(sketch)], timeout=300)
    result["backup"] = str(backup)
    return result


@mcp.tool()
def restore_sketch_backup(sketch_path: str, backup_path: str) -> dict[str, Any]:
    """Restore source files from a PokitSuite-created backup. Does not upload."""
    sketch = _safe_sketch(sketch_path)
    backup = Path(backup_path).resolve()
    backup.relative_to(BACKUP_DIR.resolve())
    if not backup.is_dir():
        raise FileNotFoundError(backup)
    for source in backup.iterdir():
        if source.is_file() and source.suffix.lower() in {".ino", ".h", ".hpp", ".c", ".cpp"}:
            shutil.copy2(source, sketch / source.name)
    return {"restored": str(sketch), "from": str(backup)}


if __name__ == "__main__":
    mcp.run()
