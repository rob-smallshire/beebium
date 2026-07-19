# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Project Overview

Beebium is a BBC Micro emulator with a radically different architecture inspired by Chromium's multi-process model. Rather than a monolithic application, Beebium separates the emulation core from frontends via gRPC, enabling platform-native UIs and parallel development of components.

## Working Principles

### Test-First Development

Write tests before implementation. The test suite is comprehensive, spanning three languages: ~200 C++ (Catch2) files under `tests/`, plus the Python (`clients/beebium-python-client/tests`) and TypeScript (`clients/beebium-typescript-client/tests`) client suites — several thousand test cases in total, covering 6502 instructions, VIA timing, disc operations, sound chip behaviour, video rendering, keyboard input, and gRPC services. Rather than quoting a total here (it rots), count them: `ctest --test-dir build --show-only=json-v1` for C++, `pytest --collect-only -q` in `clients/beebium-python-client`, and `npx vitest list` in `clients/beebium-typescript-client`. When adding new functionality:

1. Write a failing test that specifies the expected behaviour
2. Implement the minimum code to pass the test
3. Refactor while keeping tests green

### Architecture Over Features

Prioritise clean separation of concerns, well-defined interfaces, and maintainable code over feature accumulation. The codebase uses policy-based design with templates for machine variants, allowing compile-time configuration without runtime overhead.

### Cycle Accuracy

The emulator aims for cycle-accurate behaviour where it matters (6502, VIA timers, CRTC timing). Use the extensive BBC Micro documentation in `docs/manuals_text/` as authoritative references.

## Architecture

### Process Model

```
beebium-model-b (or -model-b-plus, -model-b-plus-128k, -model-b-romram)
    |
    +-- gRPC Services (VideoService, AudioService, KeyboardService, etc.)
    |
    +-- macOS Frontend (Swift/Metal) via Bonjour discovery
    +-- Python Client (testing, automation)
    +-- Future: Windows, Linux frontends
```

### Core Design

- **Headless emulator servers**: Each machine variant is a separate executable (`beebium-model-b`, `beebium-model-b-plus`, `beebium-model-b-plus-128k`, `beebium-model-b-romram`)
- **gRPC for control**: All interaction via well-defined protocol buffers
- **Lock-free video/audio**: `OutputQueue` using moodycamel::ReaderWriterQueue for pixel streaming
- **Policy-based machines**: `Machine<Hardware>` template with `ModelBHardware`, `ModelBPlusHardware`, `ModelBPlus128KHardware`, `ModelBRomRamBoardHardware`

### gRPC Services

| Service | Proto File | Purpose |
|---------|------------|---------|
| VideoService | video.proto | Frame streaming, display configuration |
| AudioService | audio.proto | Audio sample streaming |
| KeyboardService | keyboard.proto | Key press/release, type-ahead |
| DiscService | disc.proto | Disc image mounting, drive status |
| DebuggerService | debugger.proto | Breakpoints, memory access, machine state |
| SystemService | system.proto | Pause/resume/reset, machine control |
| IndicatorService | indicator.proto | LED status (caps lock, disc activity) |
| SidewaysService | sideways.proto | Sideways ROM management |

### Video Pipeline

- CRTC6845 generates timing signals
- VideoULA converts memory to pixel batches
- FrameRenderer assembles batches into BGRA32 framebuffer
- Double-buffered with VSYNC synchronisation
- Mode 7 teletext via SAA5050 character generator

### Clock Architecture

See `docs/clock-architecture.md` for the timing model. Key components:
- `ClockBinding` synchronises peripheral clocks
- `CpuBinding` manages 6502 cycle timing
- `VideoBinding` handles display timing
- `BusStretching` implements 1MHz bus delays

## Code Style

- **C++20** for core library (concepts, std::span, constexpr)
- **C11** for 6502 library (pure C, no C++ dependencies)
- **Swift** for macOS frontend
- **Python** for integration tests and automation
- File extensions: `.h/.c` for C, `.hpp/.cpp` for C++
- Use `_filename`, `_filepath`, `_dirpath`, `_dirname` suffixes (not ambiguous `_file` or `_dir`)

## Git Commit Messages

- Never include "Claude", "Opus", "Anthropic", or any AI attribution
- Never include emojis
- Never include "Co-Authored-By" lines referencing AI assistants
- Never include "Generated with" lines

## Project Structure

```
beebium/
├── src/
│   ├── 6502/                    # Standalone C library (ported from B2)
│   │   ├── include/6502/6502.h
│   │   └── src/6502.c
│   ├── core/                    # Emulation core (C++)
│   │   ├── include/beebium/
│   │   │   ├── Machine.hpp, Machines.hpp
│   │   │   ├── ModelBHardware.hpp, ModelBPlusHardware.hpp
│   │   │   ├── ModelBPlus128KHardware.hpp, ModelBRomRamBoardHardware.hpp
│   │   │   ├── Via6522.hpp, KeyboardMatrix.hpp
│   │   │   ├── FrameBuffer.hpp, FrameRenderer.hpp
│   │   │   ├── AudioBuffer.hpp, OutputQueue.hpp
│   │   │   ├── Clock*.hpp, *Binding.hpp
│   │   │   └── devices/
│   │   │       ├── Crtc6845.hpp, VideoUla.hpp
│   │   │       ├── Sn76489.hpp, BankedMemory.hpp
│   │   │       └── Ram.hpp, Rom.hpp
│   │   └── src/
│   ├── service/                 # gRPC service implementations
│   │   ├── proto/*.proto
│   │   ├── include/beebium/service/
│   │   └── src/
│   ├── server/                  # Server executables
│   │   ├── main_model_b.cpp
│   │   ├── main_model_b_plus.cpp
│   │   └── main_model_b_romram.cpp
│   └── discovery/               # Service advertisement (Bonjour/Avahi/mDNS)
├── clients/
│   ├── macos/                   # Swift/Metal frontend
│   │   └── Beebium/
│   └── python/                  # Python client for testing/automation
│       ├── src/
│       └── tests/
├── tests/                       # C++ unit tests (Catch2)
│   ├── test_6502.cpp            # Klaus functional test
│   ├── test_via6522.cpp         # VIA timer/interrupt tests
│   ├── test_sn76489*.cpp        # Sound chip tests
│   ├── test_grpc_*.cpp          # gRPC service tests
│   └── assets/                  # Test ROMs, disc images
├── docs/
│   ├── *.md                     # Component documentation
│   ├── discussion/              # Design discussions
│   └── manuals_text/            # BBC Micro manual transcripts
└── CMakeLists.txt
```

## Build System

### Prerequisites

- CMake 3.16+
- C++20 compiler (GCC 10+, Clang 12+, MSVC 2019+)
- vcpkg for dependencies (gRPC, protobuf)

### Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
make -j$(nproc)
```

### Running Tests

```bash
cd build
ctest --output-on-failure
```

### Build Options

- `-DBEEBIUM_BUILD_TESTS=ON` (default): Build tests
- `-DBEEBIUM_ENABLE_SANITIZERS=ON`: Enable ASan/UBSan

### Build Targets

- `beebium-model-b`: BBC Model B server
- `beebium-model-b-plus`: BBC Model B+ server (64K, integral BASIC/DFS)
- `beebium-model-b-plus-128k`: BBC Model B+ 128K server (adds four 16K sideways RAM banks W/X/Y/Z per AN 030)
- `beebium-model-b-romram`: Model B with ROM/RAM board
- `beebium-servers`: All server executables

## Key Components

### 6502 Library (`src/6502/`)

Pure C library for 6502 emulation. Supports NMOS and CMOS variants with cycle-accurate timing. Validated against Klaus Dormann's functional test suite.

### Machine Template (`Machine.hpp`)

```cpp
template <typename Hardware>
class Machine {
    // Policy-based design: Hardware defines memory map, peripherals
};
```

### Hardware Policies

- `ModelBHardware`: Standard BBC Model B (32K RAM, 16K sideways ROM)
- `ModelBPlusHardware`: Model B+ (64K RAM, shadow modes, integral BASIC/DFS)
- `ModelBPlus128KHardware`: B+ 128K (64K main RAM + 64K sideways RAM in four banks W/X/Y/Z)
- `ModelBRomRamBoardHardware`: Model B with sideways RAM

### VIA Implementation (`Via6522.hpp`)

Full 6522 VIA emulation including:
- T1/T2 timers with one-shot and continuous modes
- Port A/B with DDR and handshaking
- Interrupt flag/enable registers

### Sound Chip (`Sn76489.hpp`)

Texas Instruments SN76489 emulation with:
- 3 tone channels + 1 noise channel
- Verified against MAME reference implementation

### Disc Controller (`devices/` + `docs/disc-subsystem.md`)

WD1770 controller with:
- Type I-IV command set
- SSD/DSD image format support
- Automatic format detection

## Documentation

### Component Documentation (`docs/`)

- `clock-architecture.md` - Timing model and synchronisation
- `disc-subsystem.md` - Disc controller and image formats
- `sound-subsystem.md` - SN76489 and audio pipeline
- `video-subsystem.md` - Display rendering pipeline
- `grpc-server.md` - Service API documentation
- `lifecycle-management.md` - Connection liveness (heartbeat, graceful/crash/unreachable), client teardown, window/server shutdown, and the rules behind them
- `screen-text-library.md` - Reading text off the screen: the standalone recognition library and its CLI
- `keyboard.md` - Keyboard matrix and input handling
- `indicators.md` - LED status indicators
- `sideways-slots.md` - Sideways ROM/RAM topology, validation rules, motherboard links
- `deployment.md` - Installed layout and runtime ROM/preset discovery
- `packaging.md` - Server distribution: the self-contained static `.deb`/`.tar.gz` bundles, platforms, CI, status
- `versioning-and-compatibility.md` - Monorepo versioning (bump-my-version) and the protocol fingerprint compatibility handshake

### Design Discussions (`docs/discussion/`)

- `debugger-capabilities.md` - Debugging features roadmap
- `memory-interface-design.md` - Python debugger API

### Reference Material (`docs/manuals_text/`)

Complete transcripts of BBC Micro documentation:
- BBC User Guide
- Advanced User Guide (hardware details, OS calls)

## Reference Codebases

| Codebase | Location | Notes |
|----------|----------|-------|
| B2 | `/Users/rjs/Code/b2` | Architecture inspiration, 6502 source |
| BeebEm-Mac | `/Users/rjs/Code/beebem-mac` | Peripheral reference |
| B-Em | `/Users/rjs/Code/b-em` | Tube co-processor patterns |

## Known Issues & Workarounds

### Protobuf Map Hash Bug (x86_64 macOS)

**Problem**: `google::protobuf::Map::find()`, `count()`, and `at()` fail to locate keys that exist and are accessible via iteration. This manifests on x86_64 macOS but not arm64.

**Root cause**: Since protobuf v22, `google::protobuf::Map` uses `absl::hash` with a non-deterministic seed. After gRPC serialization/deserialization, the hash buckets can become inconsistent, causing lookups to fail even though the data is intact.

**Symptoms**:
- `map.size()` returns correct count
- Iteration shows correct keys and values
- `map.find(key)` returns `end()`
- `map.count(key)` returns 0
- `map.at(key)` throws "key not found"

**Workaround**: Use iteration-based lookup instead of hash-based lookup. See `tests/test_grpc_indicator.cpp` for helper functions: `proto_map_find()`, `proto_map_contains()`, `proto_map_get()`.

**References**:
- https://github.com/protocolbuffers/protobuf/issues/15069
- https://github.com/protocolbuffers/protobuf/issues/18097

**Affected code**: Any C++ code that receives a `google::protobuf::Map` via gRPC and uses `find()`/`count()`/`at()` for lookup. Currently worked around in indicator service tests; may need similar treatment in client code.

## Current State (February 2026)

### Fully Implemented

- 6502 CPU (NMOS/CMOS, cycle-accurate)
- Four machine variants (Model B, B+, B+ 128K, B with ROM/RAM board)
- 6522 VIA (System and User) with full timer and interrupt support
- 6845 CRTC with display timing
- Video ULA with all display modes (0-7)
- SN76489 sound chip
- WD1770 disc controller with SSD/DSD support
- Keyboard matrix with type-ahead
- gRPC service layer (8 services)
- macOS frontend with Metal rendering
- Service advertisement AND browse/discovery over mDNS on all platforms: macOS (Bonjour), Linux (Avahi, dlopen'd), Windows (dual-provider: Apple Bonjour dnssd.dll when installed, else native DnsService*, selected at runtime). Full bidirectional AUN peer discovery everywhere
- Python test client

### Future Work

- Tube co-processor support
- Econet/AUN networking
- Additional platform frontends (Windows, Linux)
- CRT shader pipeline
