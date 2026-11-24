# PokitSuite – Deutsche Dokumentation

PokitSuite ist eine native Windows-Anwendung für Pokit-Pro-Messgeräte. Sie bietet ein automatisch startendes Digitalmultimeter, ein an klassische Tischoszilloskope angelehntes DSO, berechnete Signalwerte und eine MCP-Schnittstelle für KI-gestützte Mess- und Arduino-Abläufe.

PokitSuite wurde mit wesentlicher Unterstützung von **OpenAI Codex** entwickelt und am realen Pokit-Pro-Gerät getestet.

## Funktionsstand

- **Implementiert und am Pokit Pro getestet:** BLE-Suche, Verbindung und Wiederaufbau; Multimeter mit 0,5/1 s Intervall; Verlaufsschreiber; einkanaliges Oszilloskop; Trigger, Kopplung, V/div, Zeit/div; Einzelaufnahme; Signalmetriken.
- **Implementiert und protokollgetestet:** MCP-Server mit 17 Werkzeugen. Für Livewerte müssen PokitSuite und das Gerät laufen.
- **Experimentell:** automatische Arduino-Skizzenbearbeitung, Kompilierung, Upload und erneute Messung; alte Weboberfläche.
- **Geplant:** vier echte Messkanäle, native Langzeit-Logger-Oberfläche, signierter Installer, breitere Hardwaretests.

## Installation

1. Das ZIP-Archiv der [aktuellen Version](https://github.com/robertbuss85-stack/PokitSuite/releases/latest) herunterladen und vollständig entpacken.
2. Bluetooth einschalten und das Pokit Pro aufwecken.
3. `PokitSuiteNative.exe` starten. Nach der Verbindung startet der aktuell sichtbare Messmodus selbstständig.

Vorausgesetzt werden Windows 10/11 x64 und Bluetooth Low Energy. Ausführliche Hinweise stehen in [INSTALL.md](INSTALL.md); die MCP-Konfiguration in [MCP.md](MCP.md).

## Bedienung

Beim Wechsel zwischen Multimeter und Oszilloskop wird der bisherige Modus gestoppt und der neue automatisch gestartet. Im Oszilloskop verändert das Mausrad über dem Diagramm die horizontale Zeitbasis. `Run/Stop` schaltet die fortlaufende Erfassung, `Single` löst eine Einzelaufnahme aus.

Die Frequenz- und Tastgradmessung setzt ein ausreichend sauberes, periodisches Signal voraus. Bei starkem Rauschen, Clipping oder weniger als einer vollständigen Periode können Werte fehlen oder ungenau sein.

## Sicherheit

Das MCP-Werkzeug kann Arduino-Quellcode verändern und Firmware übertragen. Änderungen werden gesichert, ersetzen aber keine Prüfung des Diffs und keine elektrische Schutzmaßnahme. Messbereiche, Massebezug und zulässige Eingangsspannungen des Pokit-Geräts sind einzuhalten.

