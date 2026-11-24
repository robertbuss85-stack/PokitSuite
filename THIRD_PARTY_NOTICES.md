# Third-party notices

PokitSuite is distributed under GPL-3.0-or-later. The following components retain their own licenses.

## Dokit / QtPokit

- Project: Dokit by Paul Colby and contributors
- Source: https://github.com/pcolby/dokit
- Vendored location: `native/vendor/dokit`
- License: LGPL-3.0-or-later; full notices and license texts are preserved in the vendored directory
- Snapshot: Dokit 0.6.x development lineage
- Local modification: DSO metadata decoding accepts compatible 17–22 byte payload variants observed on Pokit Pro firmware

QtPokit is compiled as a separate shared library. Recipients may replace or rebuild it from the included corresponding source.

## Qt

- Project: Qt 6
- Source and licensing: https://www.qt.io/download-open-source
- License: modules used by the binary are available under LGPL-3.0/GPL terms (module-specific notices apply)

Release archives deploy Qt shared libraries and plugins using `windeployqt`; they are dynamically linked. Qt source corresponding to a release is available from the Qt project. Users retain reverse-engineering rights necessary to debug modifications to LGPL components.

## Python MCP SDK

- Project: Model Context Protocol Python SDK
- Source: https://github.com/modelcontextprotocol/python-sdk
- License: MIT
- Installed by users from `requirements.txt`; not bundled in the native ZIP

## Trademarks

Pokit is a trademark of Pokit Innovations. Tektronix is a trademark of Tektronix, Inc. OpenAI and Codex are trademarks of OpenAI. Names are used only for identification, attribution or design inspiration; no endorsement is implied.

