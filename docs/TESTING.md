# Testing

## Automated checks

```powershell
python -m compileall -q mcp tests
python tests/mcp_contract_test.py
cmake -S native -B native/build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build native/build --parallel
```

CI performs a clean Windows native build and an offline MCP handshake/tool-schema test. It cannot validate Bluetooth timing or analog accuracy without physical hardware.

For the UI regression check, run `PokitSuiteNative.exe --ui-responsiveness-test` with no other instance open. It draws two 1,000-point jagged traces while the device worker is deliberately stalled for two seconds. Exit code 0 requires the UI heartbeat to stay below 250 ms. `--ui-monitor` logs a one-second UI heartbeat during live measurements.

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

### Manual cursor checks

- At 10 ms/div and 1 V/div, reset cursors: T1 = −20 ms, T2 = +20 ms, Δt = 40 ms, reciprocal = 25 Hz; U1 = +1.6 V, U2 = −1.6 V, ΔU = −3.2 V.
- Drag each time/voltage line, including crossing the other cursor; verify signed differences and no division by zero for coincident time cursors. Toggle each pair independently and reset.
- Change time/div, volts/div, vertical offset and trigger position: cursor values must follow the displayed axes. Verify both independent and radio-pair display, with live and reference curves. Cursor motion must not restart acquisition.
- Windows UI check: dragging T1 changed Δt from 40.000 ms to 27.380 ms (36.522 Hz); dragging U1 from +1.600 V to +2.293 V changed ΔU from −3.200 V to −3.893 V. Native build passed.

### Radio-trigger hardware checks

1. In the Oscilloscope, choose **CH1 · Funk** as the trigger source. Choose **Sofort** and click **SINGLE**. Verify complete paired frames at 10, 20, 50 and 100 µs/div with 100, 200, 500 and 1,000 samples per channel respectively. The live JSON `scope_sync` object exposes the pair flag, acquisition number and trigger sample for each channel.
2. Feed the same low-voltage pulse train with distinct edge spacings to both inputs in DC mode. Use a rising trigger and a threshold between the pulse levels. Measure the inter-channel offset at several edges within each capture. Repeat with CH2 as the trigger source, at different time bases and after a reconnect. Record offset, worst-case jitter and incomplete captures.
3. Check stop, reconnect and timeout by cancelling an armed acquisition and by withholding the trigger. Incomplete or mismatched pairs should never be shown as synchronized.
4. Feed known A/B signals in both directions. Check that the measured edge order and interval agree with the reference at the highest expected speed. The total timing uncertainty should be comfortably below the smallest expected separation of two A/B edges.
5. Start radio RUN and change volts/div and time/div while capturing. Verify that RUN stays active and both channels report the newly requested window/range. Check packet-loss recovery without mixing different groups.

Radio-triggered single capture requires two Pokit Pro devices and firmware that reports matching acquisition numbers. A successful pair identifies one radio-triggered acquisition. Its timing accuracy is a separate hardware measurement.

