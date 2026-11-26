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

## Pokit Pro radio-trigger validation

1. In the Oscilloscope, choose **CH1 · Funk** as the trigger source and set at least 100 µs/div. Choose **Sofort** and click **SINGLE**. Verify that both channels appear only after a complete pair is received. The live JSON `scope_sync` object exposes the pair flag, acquisition number and trigger sample for each channel.
2. Feed the same low-voltage pulse train with distinct edge spacings to both inputs in DC mode. Use a rising trigger and a threshold between the pulse levels. Measure the inter-channel offset at several edges within each capture. Repeat with CH2 as the trigger source, at different time bases and after a reconnect. Record offset, worst-case jitter and incomplete captures.
3. Check stop, reconnect and timeout by cancelling an armed acquisition and by withholding the trigger. Incomplete or mismatched pairs should never be shown as synchronized.
4. Feed known A/B signals in both directions. Check that the measured edge order and interval agree with the reference at the highest expected speed. The total timing uncertainty should be comfortably below the smallest expected separation of two A/B edges.

Radio-triggered single capture requires two Pokit Pro devices and firmware that reports matching acquisition numbers. A successful pair identifies one radio-triggered acquisition. Its timing accuracy is a separate hardware measurement.

