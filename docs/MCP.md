# PokitSuite MCP interface

PokitSuite includes a Model Context Protocol server built with the official Python MCP SDK. It lets an MCP-capable AI client inspect live measurements and perform supervised Arduino development operations without taking ownership of the Bluetooth connection.

## Data flow and safety boundary

```text
Pokit Pro ⇄ BLE ⇄ PokitSuiteNative.exe ⇄ bounded JSON snapshots ⇄ MCP server ⇄ AI client
                                                    ⇅
                                             Arduino CLI / IDE
```

The native application must be running for live measurements. Snapshots are local and contain device state and measurement samples, not credentials. Code-edit and upload tools affect local sketches or attached hardware; review their arguments and results.

## Setup

```powershell
py -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

Generic MCP client configuration:

```json
{
  "mcpServers": {
    "pokitsuite": {
      "command": "C:\\path\\to\\PokitSuite\\.venv\\Scripts\\python.exe",
      "args": ["C:\\path\\to\\PokitSuite\\mcp\\pokitsuite_mcp.py"]
    }
  }
}
```

Codex CLI example:

```powershell
codex mcp add pokitsuite -- `
  C:\path\to\PokitSuite\.venv\Scripts\python.exe `
  C:\path\to\PokitSuite\mcp\pokitsuite_mcp.py
```

Restart the MCP client after configuration, launch PokitSuite, and connect the device.

## Tools

| Tool | Purpose |
|---|---|
| `get_device_status` | Connection, device, active feature, freshness and status |
| `get_multimeter_reading` | Latest DMM value/unit/mode and timestamp |
| `get_scope_metrics` | Sample rate plus min/max/mean/RMS/Vpp/frequency/period/cycles/duty |
| `get_scope_frame` | Complete bounded scope frame for numerical interpretation |
| `evaluate_scope_reference` | Compare metrics with expected values and tolerances |
| `wait_for_scope_update` | Wait for a newer acquisition without polling aggressively |
| `configure_scope` | Set coupling, V/div, time/div and trigger parameters |
| `capture_scope_single` | Request a single acquisition and wait for its result |
| `find_arduino_sketches` | Discover `.ino` sketches in an allowed search root |
| `list_arduino_boards` | Query Arduino CLI board detection |
| `read_arduino_sketch` | Read a sketch with size and path checks |
| `open_sketch_in_arduino_ide` | Open a selected sketch in Arduino IDE |
| `read_serial_monitor` | Capture bounded serial output through Arduino CLI |
| `replace_in_sketch` | Exact, backup-aware replacement in a selected sketch |
| `compile_arduino_sketch` | Compile with an explicit FQBN |
| `upload_arduino_sketch` | Upload with explicit FQBN and port |
| `restore_sketch_backup` | Restore the backup created by an edit |

Tools validate paths, timeouts and bounded payload sizes. The server does not invent an FQBN and does not upload merely because a serial port exists.

## Example agent workflow

1. Call `get_device_status`; stop if disconnected or stale.
2. Call `configure_scope` with an appropriate time/div and safe trigger level.
3. Call `capture_scope_single`, then `evaluate_scope_reference` against a known target.
4. If the deviation is attributable to firmware, read the sketch and propose a minimal exact replacement.
5. Use `replace_in_sketch`; inspect the diff/backup path.
6. Compile with the known board FQBN. Upload only after successful compilation and target/port verification.
7. Re-capture and compare. Restore the backup if behavior regresses.

Example request to an agent:

> Measure CH1, expect a 1 kHz square wave at 3.3 Vpp with ±5% frequency and ±10% amplitude. Explain any deviation. Do not modify code until you identify the relevant constant. Compile after a minimal change, but ask before uploading.

## Limitations

- The bridge currently controls CH1 only.
- MCP transport is local stdio; there is no network service or authentication layer.
- Signal classification is metric/reference based. AI interpretation quality depends on frame quality and the client model.
- Arduino CLI must be installed or discoverable through Arduino IDE; board support packages remain separate.
- Upload and serial monitor usually cannot own the same port simultaneously.

