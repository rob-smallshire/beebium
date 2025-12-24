# Beebium

A BBC Micro emulator with a modern multi-process architecture.

## Overview

Beebium is a BBC Micro emulator designed with clean separation between the emulation core and user interface. Inspired by Chromium's multi-process architecture, the emulator core runs as a headless server while frontends connect via gRPC for video, audio, and input.

### Key Features

- **Cycle-accurate 6502 emulation** - NMOS and CMOS variants supported
- **Headless core** - Deterministic, UI-free emulation server
- **Process separation** - Core and frontends communicate via gRPC
- **Platform-native frontends** - Native UI using Cocoa, Win32, etc. (no SDL/cross-platform libraries)
- **Pluggable peripherals** - Consistent interface for emulated hardware

### Current Status

Beebium is under active development. Current capabilities:

- Boots MOS 1.20 to BASIC prompt ("BBC Computer 32K")
- Mode 7 teletext display working
- Full 6522 VIA emulation with timers and interrupts
- Keyboard input via gRPC
- macOS frontend with Metal rendering

## Architecture

```
beebium-server (C++)
    |
    +-- gRPC + shared memory
    |
    +-- macOS Frontend (Swift/Metal)
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

### macOS Frontend

```bash
cd clients/macos/Beebium
xcodegen generate  # If using XcodeGen
open Beebium.xcodeproj
```

## Usage

1. Start the emulator server:
   ```bash
   ./build/src/server/beebium-server --mos path/to/MOS120.rom --basic path/to/BASIC2.rom
   ```

2. Launch the macOS frontend and connect to `localhost:50051`

## Project Structure

```
beebium/
├── src/
│   ├── 6502/           # Cycle-accurate 6502 library (C)
│   ├── core/           # Emulator core library (C++)
│   ├── service/        # gRPC service implementations
│   └── server/         # Standalone server executable
├── clients/
│   └── macos/          # Native macOS frontend (Swift)
├── tests/              # Catch2 test suite
├── docs/               # Documentation
└── scripts/            # Development scripts
```

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
- The BBC Micro community at [Stardot](https://stardot.org.uk/)
