"""Offline MCP transport and schema smoke test."""
import asyncio
from pathlib import Path
import sys
import runpy
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

EXPECTED_TOOLS = {
    "get_device_status", "get_multimeter_reading", "get_scope_metrics",
    "get_scope_frame", "evaluate_scope_reference", "wait_for_scope_update",
    "find_arduino_sketches", "configure_scope", "capture_scope_single",
    "list_arduino_boards", "read_arduino_sketch", "open_sketch_in_arduino_ide",
    "read_serial_monitor", "replace_in_sketch", "compile_arduino_sketch",
    "upload_arduino_sketch", "restore_sketch_backup",
}

async def main() -> None:
    root = Path(__file__).resolve().parents[1]
    module = runpy.run_path(str(root / "mcp" / "pokitsuite_mcp.py"))
    namespace = module["get_scope_frame"].__globals__
    namespace["_read_state"] = lambda: {"scope": {"unit": "A", "waveform": list(range(60)), "waveform_a": list(range(60)), "metrics": {"vpp_a": .1}}}
    frame = module["get_scope_frame"](max_points=20)
    assert frame["returned_points"] == 20 and "waveform_v" not in frame
    assert len(frame["waveform_a"]) == 20 and len(frame["waveform"]) == 20
    assert not any(key.startswith("waveform") for key in module["get_scope_metrics"]())
    assert not module["evaluate_scope_reference"](expected_vpp_v=1)["pass"]
    namespace["_read_state"] = lambda: {"scope2": {"unit": "A", "waveform_a": [0.01, 0.02], "metrics": {"vpp_a": .01}}}
    assert module["get_scope_frame"](channel=2)["waveform_a"] == [.01, .02]
    assert module["get_scope_metrics"](channel=2)["unit"] == "A"
    server = StdioServerParameters(command=sys.executable,
        args=[str(root / "mcp" / "pokitsuite_mcp.py")], cwd=str(root))
    async with stdio_client(server) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            names = {tool.name for tool in (await session.list_tools()).tools}
            if names != EXPECTED_TOOLS:
                raise AssertionError(f"MCP tool mismatch: missing={EXPECTED_TOOLS-names}, extra={names-EXPECTED_TOOLS}")
            result = await session.call_tool("get_device_status", {})
            if result.is_error:
                raise AssertionError("Offline status call returned a protocol error")
            print(f"MCP contract OK: {len(names)} tools")

if __name__ == "__main__":
    asyncio.run(main())

