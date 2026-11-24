"""Offline MCP transport and schema smoke test."""
import asyncio
from pathlib import Path
import sys
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

