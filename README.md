<p align="center">
  <img src="docs/images/beebium-logo.png" alt="The Shape of Beebium: a sunset over a mirrored floor with a cylinder, a cuboid and a pyramid" width="256">
</p>

# Beebium

<p align="center">
  <a href="https://github.com/rob-smallshire/beebium/actions/workflows/ci.yml"><img src="https://github.com/rob-smallshire/beebium/actions/workflows/ci.yml/badge.svg?branch=master" alt="CI"></a>
  <a href="https://github.com/rob-smallshire/beebium/actions/workflows/ci.yml"><img src="https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Frob-smallshire%2Fbeebium%2Fbadges%2Ftests.json" alt="Test count"></a>
</p>

A different take on a BBC Micro emulator, with a Chromium-inspired architecture.

## Overview

Different emulators serve different purposes. Some prioritise nostalgia and game compatibility. Others focus on accuracy or preservation. **Beebium's primary goal is to serve as a foundation for tools for creating new software for the BBC Micro and its coprocessors.**

The architecture reflects this goal: a headless emulation core that runs as a server, with frontends connecting via gRPC. This separation will enable integration with IDEs, test harnesses, continuous integration pipelines, and custom development tools, not just standalone GUI emulators.

Moreover, this architecture also facilitates native GUIs with first-class host platform integration, rather than the compromises required by cross-platform graphics toolkits. The macOS frontend uses Swift and Metal. A Windows frontend can use WinUI 3 and Direct3D. A Linux frontend can use GTK or Qt. Each platform's code remains clean and idiomatic, unencumbered by the complexity of accommodating other platforms.

### Key Features

- **Cycle-accurate 6502 emulation** - NMOS and CMOS variants supported
- **Headless core** - Deterministic, UI-free emulation server
- **Process separation** - Core and frontends communicate via gRPC
- **Platform-native frontends** - Native UI using Cocoa, WinUI, etc. (no SDL/cross-platform libraries)
- **Pluggable peripherals** - Consistent interface for emulated hardware

### Current Status

Beebium is under active development. Current capabilities:

- Boots MOS 1.20 to BASIC prompt ("BBC Computer 32K")
- All display modes working (MODE 0-7)
- Full 6522 VIA emulation with timers and interrupts
- WD1770 disc controller with SSD/DSD image support (`--floppy 0:game.ssd`)
- SN76489 sound chip emulation
- Keyboard input via gRPC
- Audio output via gRPC
- macOS frontend with Metal rendering

## Architecture

```
beebium-model-b / beebium-model-b-plus (C++)
    |
    +-- gRPC + shared memory
    |
    +-- macOS Frontend (Swift/Metal)
    +-- Python Client (pytest integration)
    +-- Debugger (planned)
```

The core maintains double-buffered framebuffers and publishes frames via shared memory. Frontends may drop frames; the core never blocks waiting for acknowledgement.

## Requirements

- CMake 3.16+
- C++20 compiler (Clang 14+, GCC 11+)
- gRPC and Protobuf
- Catch2 v3.x (fetched automatically)

### macOS Frontend

- Xcode 15+
- Swift 5.9+
- grpc-swift package

### Python Client

- Python 3.12+
- grpcio, protobuf

## Building

### Core and Server

```bash
mkdir build && cd build
cmake ..
make
```

### Running Tests

```bash
cd build
ctest --output-on-failure
```

Some integration tests light up only when an optional tool is on the `PATH`;
they **skip cleanly** otherwise:

- [`beebasm`](https://github.com/stardot/beebasm) — assembles real 6502 programs
  onto auto-booting disc images so guest software can be driven end to end (see
  [Testing with real 6502 code](docs/testing-from-disc.md), the
  `beebasm → bootable disc → autoboot` pattern).
- `pySerial` (fetched automatically via [`uv`](https://docs.astral.sh/uv/)) —
  drives the RFC 2217 serial endpoints as a real external client.
- [`tcpser`](https://github.com/go4retro/tcpser) — exercises the IP232 serial
  bridge against a real modem server.

### macOS Frontend

The macOS app **embeds the headless server executables in its bundle**, so the
servers must be built before (or together with) the app. The convenience script
does both, and optionally launches the result:

```bash
scripts/build-macos-app.sh          # build servers (CMake) + app (Xcode)
scripts/build-macos-app.sh --run    # ... and launch it
```

The same thing is available through the normal CMake build interface, as the
`macos-app` and `macos-run` targets (macOS only; they delegate to the script and
embed servers from the configured build tree):

```bash
cmake --build build --target macos-app    # build servers + app
cmake --build build --target macos-run    # ... and launch it
```

`cmake --build <dir>` resolves `<dir>` against your **current** directory, so use
the path to your build tree — a bare `build` only works when a `build/` sits
right there. From inside the build tree (e.g. `build/src/server`) point it at the
root explicitly:

```bash
cmake --build /path/to/beebium/build --target macos-app   # absolute, from anywhere
cd build && make macos-app                                # from the build-tree root
```

Note `make macos-app` only works from the **top** of the build tree (`build/`),
not a subdirectory — CMake emits custom targets into the top-level Makefile only.

To work in Xcode directly, build the servers once, then open the project:

```bash
cmake --build build --target beebium-servers   # the four server executables
cd clients/macos/Beebium
xcodegen generate                              # regenerate after adding/removing files
open Beebium.xcodeproj                         # then build & run from Xcode
```

The Xcode build embeds servers from `$BEEBIUM_SERVERS_BUILD_DIR`, which
**defaults to `build/src/server`** when unset — so a normal build always bundles
a freshly built server. Point it elsewhere to bundle a different build:

```bash
BEEBIUM_SERVERS_BUILD_DIR=$PWD/build-release/src/server \
  xcodebuild build -scheme Beebium -configuration Release
```

If no servers are found there, the build prints a warning and keeps whatever is
already bundled (it won't fail the build, but the app can't launch a machine
until you build them). For self-contained distribution builds, see
[docs/macos-app-packaging.md](docs/macos-app-packaging.md).

### Python Client

```bash
pip install -e clients/beebium-python-client
```

## Usage

1. Start the emulator server:
   ```bash
   ./build/src/server/beebium-model-b
   ```

   To load a disc image:
   ```bash
   ./build/src/server/beebium-model-b --floppy 0:game.ssd
   ```

2. Launch the macOS frontend and connect to `localhost:48875`

### Python Client

The Python client (`clients/beebium-python-client`) enables programmatic control of the emulator for testing and automation. Useful for integration testing BBC Micro software in CI pipelines.

```python
from beebium import Beebium
from beebium.screen import read_mode7_screen, linearise, lined

with Beebium.connect() as bbc:
    bbc.debugger.stop()
    bbc.keyboard.type("PRINT 2+2\r")
    print(linearise(read_mode7_screen(bbc), lined))  # scroll-corrected MODE 7 text
```

## Project Structure

```
beebium/
├── src/
│   ├── 6502/           # Cycle-accurate 6502 library (C)
│   ├── core/           # Emulator core library (C++)
│   ├── service/        # gRPC service implementations
│   └── server/         # Standalone server executable
├── clients/
│   ├── macos/          # Native macOS frontend (Swift)
│   └── python/         # Python client library
├── tests/              # Catch2 test suite
├── docs/               # Documentation
└── scripts/            # Development scripts
```

## Documentation

Component and subsystem documentation lives in [`docs/`](docs/). Some entry points:

- [Deployment and Resource Discovery](docs/deployment.md) — installed layout and how a server finds ROMs and presets at runtime
- [Packaging and Distribution](docs/packaging.md) — the self-contained static `.deb`/`.tar.gz` server bundles, supported platforms (incl. arm64 / Raspberry Pi and Arch), how they are built and validated, and what is done vs still to do
- [Versioning and Protocol Compatibility](docs/versioning-and-compatibility.md) — monorepo single-version releases with `bump-my-version`, and the protocol fingerprint handshake that keeps clients and servers compatible
- [gRPC Server Interface](docs/grpc-server.md) — the service API
- [Building](docs/building.md) — build prerequisites and options

## Development

After cloning, install the git hooks:

```bash
./scripts/install-hooks
```

This sets up a pre-commit hook that verifies copyright notices in source files.

## License

Beebium is licensed under the [GNU General Public License v3.0](COPYING.txt).

## Acknowledgments

- **Tom Seddon** - The 6502 library is ported from [B2](https://github.com/tom-seddon/b2), Tom's excellent BBC Micro emulator
- **Matt Godbolt** - Creator of [jsbeeb](https://github.com/mattgodbolt/jsbeeb), from which VIA timing tests and 1MHz bus stretching logic were adapted
- **Chris Evans** (@scarybeasts) - Hardware-validated VIA timing data measured on real BBC Master hardware, and CRTC 6845 edge case tests from [beebjit](https://github.com/scarybeasts/beebjit)
- **Nicola Salmoria and MAME contributors** - Hardware-verified SN76489/SN76496 sound chip behaviors documented in [MAME](https://github.com/mamedev/mame), including LFSR tap positions, noise reset behavior, and chip variant differences
- The BBC Micro community at [Stardot](https://stardot.org.uk/)

## Third-Party Libraries

- **moodycamel::ReaderWriterQueue** - Lock-free single-producer, single-consumer queue
  - Copyright (c) 2013-2021, Cameron Desrochers
  - Simplified BSD License
  - https://github.com/cameron314/readerwriterqueue
- **stb_image_write** - Single-file PNG/BMP/TGA/JPEG/HDR image writer
  - Copyright (c) 2017, Sean Barrett
  - MIT License / Public Domain
  - https://github.com/nothings/stb
