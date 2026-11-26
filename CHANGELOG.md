# Changelog

All notable public changes are documented here. The project follows semantic versioning from its first public release.

## Unreleased

### Added

- Native Pokit Pro radio-triggered single acquisitions with CH1 or CH2 as the trigger master
- Acknowledged follower arming before master start, a fresh radio group for each shot, and atomic publication of complete frames with matching acquisition numbers
- Trigger-origin and sampling-rate metadata for a shared display time axis
- Radio master/follower/stop packet and legacy/extended metadata regression tests

### Fixed

- Recreate invalid GATT service objects after a Bluetooth disconnect
- Avoid unnecessary DSO characteristic-value discovery on Windows

### Validation

- Windows native build, DSO protocol regression tests and MCP contract tests passed
- Two physical Pokit Pro devices returned matching 1,000-sample captures with either channel as master, including repeated immediate captures, a rising-edge capture, and stop/re-arm
- Channel skew, jitter and A/B encoder accuracy still require measurement against a shared reference signal. Radio mode currently supports single acquisitions from 100 µs/div.

## [0.3.0] - 2026-11-25

### Added

- Independent BLE discovery, reconnect and watchdog handling for a second Pokit Pro
- Simultaneous CH1/CH2 live multimeter values and scrolling histories
- Concurrent CH1/CH2 DSO acquisition, metrics, overlay rendering and MCP telemetry
- Repeated discovery while CH1 keeps measuring, allowing a sleeping CH2 to join later
- Devices tab with locally saved red, light gray, yellow and green markers keyed by Bluetooth ID or MAC address
- Per-channel Pokit Pro switch detection and separate multimeter mode selection, including diode, capacitance, resistance, continuity and low current

### Fixed

- Distinguish two devices when Windows supplies an empty Bluetooth UUID
- Recover missing multimeter readings by reading the characteristic directly and reconnecting a stalled channel without restarting its peer
- Keep multimeter readouts fixed in size and show `OL` for firmware overrange values instead of plotting them

### Compatibility and validation

- The existing single-device workflow and MCP telemetry fields remain supported
- Confirmed live DC voltage readings and DSO acquisition on two Pokit Pro devices. Long-running Bluetooth stability and all combination-switch modes still need hardware validation.

## [0.2.0] - 2026-11-24

### Added

- Scope capture library with selectable saved reference curves
- PDF measurement reports containing a scope plot and derived-value table
- CSV sample tables and JSON waveform archives for every saved capture
- Cyan reference overlay for direct comparison with the live CH1 waveform

## [0.1.0] - 2026-11-24

### Added

- Native C++/Qt Pokit Pro application with DMM and TDS 220-inspired DSO
- Robust BLE reconnection, acquisition watchdogs and automatic feature switching
- True timebase-driven DSO acquisition, full-frame rendering and signal metrics
- Local MCP server with 17 measurement, scope and Arduino tools
- English/German documentation, GitHub Pages, CI and release packaging

### Known limitations

- One physical Pokit Pro/one channel tested
- Logger UI, multi-device channels, code signing and broad hardware-in-loop coverage remain open

