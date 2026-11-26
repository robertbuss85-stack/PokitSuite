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

## Implemented, protocol-tested, hardware-dependent

- MCP server handshake and 17 published tool schemas
- Arduino sketch discovery, guarded reading/replacement and backup restoration
- Arduino CLI board listing, compilation, upload and bounded serial monitoring

## Experimental / known limitations

- CH1 and CH2 acquisition are hardware-tested; CH3–CH4 remain placeholders
- Radio-triggered single acquisitions are tested, while channel skew/jitter and the usable A/B encoder speed range still need reference measurements
- Bluetooth behavior can vary with Windows adapter/driver and competing Pokit clients
- Frequency/duty estimates need clean crossings and sufficient cycles
- Arduino end-to-end autonomous repair is not validated for every board/core
- Binaries are not code-signed; first launch may trigger SmartScreen
- Legacy browser frontend remains available but is not the supported UI

## Planned

### Near term

- Add deterministic replay fixtures for DSO packet/frame regression tests
- Validate Pokit Meter and document model-specific capabilities
- Add native long-duration logger controls, export and annotations
- Improve frequency/duty algorithms for noisy or clipped waveforms
- Produce a signed installer and reproducible dependency manifest/SBOM

### Later

- Extend native radio-triggered acquisition to CH3/CH4 and continuous capture
- Cursor measurements, protocol-oriented views and richer trigger types
- Cross-platform validation on Linux and macOS where BLE support permits
- Guardrailed AI diagnosis recipes and repeatable hardware-in-the-loop tests

Track active work in [GitHub Issues](https://github.com/robertbuss85-stack/PokitSuite/issues).

