# Installation

## Release package (recommended)

Download `PokitSuite-Windows-x64.zip` from the [latest release](https://github.com/robertbuss85-stack/PokitSuite/releases/latest), extract it to a writable directory, and launch `PokitSuiteNative.exe`. Keep all DLLs and plugin directories next to the executable.

Requirements:

- Windows 10 or 11, x64
- Bluetooth Low Energy adapter enabled in Windows
- Pokit Pro with adequate battery charge
- Python 3.10+ only when the MCP server is used

If Windows SmartScreen warns about the unsigned first release, verify the archive checksum on the release page and choose the standard “Run anyway” flow only if the publisher URL is this repository. Code signing is on the roadmap.

## MCP installation

```powershell
py -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python mcp\pokitsuite_mcp.py
```

The MCP process uses stdio and normally appears idle when launched in a terminal. Configure it in an MCP client instead; see [MCP.md](MCP.md).

## Source build

Install Qt 6.8+ for MSVC, CMake 3.21+, Ninja, and an MSVC C++17 toolchain. From a Developer PowerShell:

```powershell
cmake -S native -B native/build -G Ninja `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 `
  -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --parallel
```

To produce a portable directory:

```powershell
New-Item -ItemType Directory dist/PokitSuite
Copy-Item native/build/PokitSuiteNative.exe,native/build/QtPokit.dll dist/PokitSuite/
& C:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe `
  --release --qmldir native/qml dist/PokitSuite/PokitSuiteNative.exe
```

