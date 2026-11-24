# Testing

## Automated checks

```powershell
python -m compileall -q mcp tests
python tests/mcp_contract_test.py
cmake -S native -B native/build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build native/build --parallel
```

CI performs a clean Windows native build and an offline MCP handshake/tool-schema test. It cannot validate Bluetooth timing or analog accuracy without physical hardware.

## Hardware smoke test

Before a release candidate is considered hardware-tested:

1. Start with Bluetooth enabled and confirm discovery/connect without another Pokit client open.
2. Observe DMM readings for at least two minutes at 0.5 s and 1 s intervals.
3. Switch DMM → DSO → DMM repeatedly; verify acquisition starts automatically and the connection remains stable.
4. Exercise all time/div values, run/stop, single, auto/normal/free trigger and valid trigger levels.
5. Compare Vpp/frequency/period against a known generator where available.
6. Start an MCP client, list all tools, fetch a DMM value/frame, configure scope and capture single.

The first public release was interactively exercised with one Pokit Pro on Windows. Four-channel, Pokit Meter and cross-platform operation are not hardware-validated.

