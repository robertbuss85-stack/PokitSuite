# PokitSuite MCP

Lokaler MCP-Server für Pokit-Messdaten und Arduino-Workflows. PokitSuite muss parallel laufen und schreibt `data/pokit_live.json`.

## Installation

```powershell
python -m pip install "mcp[cli]>=2.0,<3"
```

## MCP-Konfiguration

Command: `python`

Arguments: `<repository-path>\mcp\pokitsuite_mcp.py`

Optional begrenzt `POKITSUITE_ARDUINO_ROOT` die erlaubten Sketch-Verzeichnisse. Ohne Angabe ist ausschließlich das Benutzerprofil zugelassen. Upload benötigt bei jedem Aufruf `confirm_upload=true`.

## Werkzeuge

- Pokit: Status, Multimeterwert, Scope-Metriken, vollständiger/ausgedünnter Frame, Scope-Konfiguration und Single-Shot.
- Arduino: Board-Erkennung, Sketch lesen, gesicherte Textersetzung, kompilieren, verifizieren/hochladen und Backup wiederherstellen.

Alle BLE-Zugriffe bleiben in der nativen PokitSuite. Der MCP-Prozess öffnet keine zweite Geräteverbindung.
