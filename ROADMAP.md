# Roadmap

Statuses are evidence-based: **implemented** means present in source, **tested** records the available verification, **experimental** warns about incomplete validation, and **planned** is not promised functionality.

## Implemented and hardware-tested

- Pokit Pro BLE discovery, connection, service initialization and automatic reconnect
- Automatic acquisition on connect and on DMM/DSO tab changes
- DMM modes with 0.5 s / 1 s polling and time-value history
- CH1 DSO run/stop/single, coupling, V/div, time/div, trigger mode/level/position
- Whole-frame acquisition and min/max/mean/RMS/Vpp/frequency/period/cycles/duty metrics
- Mouse-wheel horizontal timebase control
- Local telemetry/command bridge and MCP measurement/configuration tools
- Two-device DMM voltage readings and DSO acquisition
- Native radio-triggered single captures with either Pokit Pro as master, matched frame numbers and shared trigger-axis rendering
- Repeated radio-triggered acquisition pairs with RUN/STOP, 10 µs/div support and setting changes between complete pairs
- Selectable saved reference curves with recorded timebase, including radio-trigger display

## Implemented and UI-tested

- Draggable time and voltage cursors with signed differences and reciprocal time interval
- Fixed two-by-two channel metric layout with full CH1/CH2 readings and reserved CH3/CH4 positions
- Dual DMM HOLD/REL and absolute min/max/mean statistics, transition-based limit alarms, timestamped trend zoom/pan, annotations, streaming CSV recording and PDF summary reports

## Implemented, protocol-tested, hardware-dependent

- MCP server handshake and 17 published tool schemas
- Arduino sketch discovery, guarded reading/replacement and backup restoration
- Arduino CLI board listing, compilation, upload and bounded serial monitoring

## Experimental / known limitations

- CH1 and CH2 acquisition are hardware-tested; CH3–CH4 remain placeholders
- Radio-triggered single acquisitions are tested, while channel skew/jitter and the usable A/B encoder speed range still need reference measurements
- Bluetooth behavior can vary with Windows adapter/driver and competing Pokit clients
- Frequency/duty estimates need clean crossings and sufficient cycles
- Rising/falling radio-trigger acquisition still has intermittent missing captures under investigation
- Arduino end-to-end autonomous repair is not validated for every board/core
- Binaries are not code-signed; first launch may trigger SmartScreen
- Legacy browser frontend remains available but is not the supported UI

## Planned

### Near term

- Add deterministic replay fixtures for DSO packet/frame regression tests
- Validate Pokit Meter and document model-specific capabilities
- Validate native DMM streaming CSV recording during multi-day runs and adapter reconnects; add saved-session replay
- Improve frequency/duty algorithms for noisy or clipped waveforms
- Produce a signed installer and reproducible dependency manifest/SBOM

### Later

- Extend native radio-triggered acquisition to CH3/CH4
- Protocol-oriented views and richer trigger types
- Snap measurement cursors to waveform edges and extrema
- Math traces (CH1 minus CH2, sum and ratio), with explicit handling of acquisition timing
- Phase/edge-delay measurements and A/B encoder direction, count and error analysis after skew/jitter validation
- Cross-platform validation on Linux and macOS where BLE support permits
- Guardrailed AI diagnosis recipes and repeatable hardware-in-the-loop tests

Track active work in [GitHub Issues](https://github.com/robertbuss85-stack/PokitSuite/issues).

