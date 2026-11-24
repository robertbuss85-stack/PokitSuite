from __future__ import annotations

import importlib.util
import json
from pathlib import Path


server_path = Path(__file__).with_name("pokitsuite_mcp.py")
spec = importlib.util.spec_from_file_location("pokitsuite_mcp_server", server_path)
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

state = module._read_state()
assert state.get("schema") == "pokitsuite.telemetry.v1", state

boards = module._run_cli(["board", "list", "--json"], timeout=30)
assert boards["ok"], boards
json.loads(boards["stdout"])

print(json.dumps({
    "telemetry": "ok",
    "connected": state.get("connected"),
    "arduino_cli": str(module.ARDUINO_CLI),
    "board_detection": "ok",
}, indent=2))
