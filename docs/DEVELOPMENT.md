# Development guide

## Architecture

The supported frontend is a C++17 Qt Quick application. `PokitController` owns discovery, service initialization, acquisition state, watchdogs, metrics and the file-based automation bridge. QML renders the DMM and oscilloscope views. The vendored Dokit library provides the Pokit BLE protocol implementation and is compiled into `QtPokit.dll` by the top-level native build.

The MCP server is a separate Python process. The native app atomically publishes a bounded JSON snapshot to `data/pokit_live.json`; MCP writes commands to `data/pokit_command.json`. This separation prevents an AI client from owning the BLE handle and avoids competing Windows Bluetooth sessions.

## Acquisition lifecycle

1. Discovery finds a supported Pokit device.
2. Device services initialize and the visible feature starts automatically.
3. Tab changes stop the old service and activate the new one.
4. Watchdogs detect stale acquisition or link loss, then retry or reconnect.
5. DSO packets are assembled into a complete frame before UI/telemetry publication.

Time/div selects an acquisition window and derived sample rate/count within Pokit hardware limits. It is not merely a visual zoom.

## Contribution workflow

1. Create a focused branch and keep generated builds/data untracked.
2. Build the native target and run the offline MCP contract test.
3. Clearly state whether a change was tested with physical hardware.
4. Do not add credentials, personal paths, captured private sketches, serial logs, or telemetry to commits.

Formatting follows the existing C++/QML/Python style. New behavior should preserve automatic feature activation and must not open a second BLE owner.

## Release packaging

Tags matching `v*` run the release workflow. It compiles from source, deploys Qt runtime dependencies, creates a ZIP and publishes it as a GitHub Release asset. Releases are currently unsigned.

