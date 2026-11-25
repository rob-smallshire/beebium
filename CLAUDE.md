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

Python test module names must be unique across the whole repository, not just within their folder: the test folders have no `__init__.py`, and pytest imports each test file by its bare module name, so two files called `test_debugger.py` in different folders stop collection of the whole suite with "import file mismatch". Give the files of a subsystem a prefix, as `tests/aes/test_aes_*.py` do. Do not import a `conftest.py` as a module; shared helpers go in a uniquely named module (CI checks this).

### Architecture Over Features

Prioritise clean separation of concerns, well-defined interfaces, and maintainable code over feature accumulation. The codebase uses policy-based design with templates for machine variants, allowing compile-time configuration without runtime overhead.

### Cycle Accuracy

The emulator aims for cycle-accurate behaviour where it matters (6502, VIA timers, CRTC timing). Use the extensive BBC Micro documentation in `docs/manuals_text/` as authoritative references.

## Architecture

### Process Model

```
beebium-server, invoked as beebium-model-b, beebium-master-128, ... (one executable; the model names are links to it, or `beebium-server <model>`)
    |
    +-- gRPC Services (VideoService, AudioService, KeyboardService, etc.)
    |
    +-- macOS Frontend (Swift/Metal) via Bonjour discovery
    +-- Linux / Raspberry Pi Frontend (Qt 6 Widgets / QRhi Vulkan) via Avahi discovery
    +-- Python Client (testing, automation)
    +-- Future: Windows frontend
```

### Core Design

- **Headless emulator server**: One multi-call executable, `beebium-server`, holds every machine variant and runs the one named by the name it was invoked as (`beebium-model-b`, `beebium-model-b-plus`, `beebium-model-b-plus-128k`, `beebium-model-b-romram`, `beebium-model-b-atpl-sidewise`, `beebium-model-b-watford-rom-ram`, `beebium-model-b-integra-b`, `beebium-master-128` are relative symlinks to it on Linux and macOS, tiny launchers on Windows) or by its first argument (`beebium-server master-128 start ...`). See `docs/deployment.md`, "One Executable"
- **gRPC for control**: All interaction via well-defined protocol buffers
- **Lock-free video/audio**: `OutputQueue` using moodycamel::ReaderWriterQueue for pixel streaming
- **Policy-based machines**: `Machine<Hardware>` template with one hardware policy per variant (`ModelBHardware`, `ModelBPlusHardware`, `ModelBIntegraBHardware`, ...; see Hardware Policies below)

### gRPC Services

| Service | Proto File | Purpose |
|---------|------------|---------|
| VideoService | video.proto | Frame streaming, display configuration |
| AudioService | audio.proto | Audio sample streaming |
| KeyboardService | keyboard.proto | Key press/release, type-ahead |
| ControllerService | controller.proto | Controllers a person holds (joystick, ...): list, get and set their named axes and buttons |
| DiscService | disc.proto | Disc image mounting, drive status |
| CassetteService | cassette.proto | Cassette deck: insert/eject UEF, CSW and WAV tapes, live audio input (StreamAudio), play/record/stop/seek, deck status stream, the tape's signal map (GetTapeSignalMap), the append point (SeekToAppendPoint), blank tape templates (ListBlankTapes/CreateTape) |
| IndicatorService | indicator.proto | LED status (caps lock, disc activity) |
| SidewaysService | sideways.proto | Sideways ROM/RAM slot management |
| SystemService | system.proto | Pause/resume/reset, machine control, SystemInfo/host fingerprint, WatchServerStatus |
| DebuggerControl | debugger.proto | Breakpoints, memory access, host CPU state |
| CoprocessorDebuggerControl | debugger.proto | Family-agnostic debugger for a Tube coprocessor |
| DeviceInspection | debugger.proto | Reads device state safely under quiesce |
| TubeService | tube.proto | Tube / second-processor control |
| SerialService | serial.proto | MC6850 ACIA / serial-ULA host-serial bridge |
| EconetService | econet.proto | Econet enable/disable, station number, status and events (WatchEconetStatus) |
| EconetTransportService | econet_transport.proto | Econet transport (AUN/Piconet) configuration and discovery |
| PeripheralExtensionService | peripheral_extension.proto | Peripheral/extension discovery and configuration |
| ExtensionUiService | extension_ui.proto | Extension-supplied sidebar views (SubscribeView) and user actions (Dispatch) |
| ExtensionRpc | extension_rpc.proto | Shared channel for plugin-hosted typed RPCs, e.g. AunService (aun.proto), routed to a transport by instance id |

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

- Never include emojis
- Attribution trailers (Co-Authored-By, session links) at the end of the message are allowed
- Mentioning AI assistance in a commit message, a comment or a document is fine; there is no rule against it and nothing to check for

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
│   ├── server/                  # The one server executable: a dispatch main plus one run_<model>() per variant
│   │   ├── main.cpp
│   │   ├── main_model_b.cpp
│   │   └── main_<model>.cpp
│   ├── discovery/               # Service advertisement and browse (Bonjour/Avahi/mDNS)
│   └── extensions/              # Plugins: Econet transports (aun, piconet), SCSI, serial, coprocessors, Music 5000
├── clients/
│   ├── macos/                   # Swift/Metal frontend
│   │   └── Beebium/
│   ├── linux/                   # Qt 6 frontend for Linux and Raspberry Pi (src/app, src/connection, src/display, src/preset, tests)
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

- `beebium-server`: the one server executable for every machine variant (Model B, B+, B+ 128K, the notional ROM/RAM, ATPL Sidewise, Watford ROM/RAM and Integra-B boards, and the Master 128; MOS 3.20, with preset `master-128-mos-3-50` for MOS 3.50). Its POST_BUILD step creates the eight `beebium-<model>` links beside it, so `build/src/server/beebium-model-b` and friends exist as before
- `beebium-servers`: `beebium-server` plus every plugin

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
- `ModelBRomRamBoardHardware`: Model B with a notional 16-slot ROM/RAM board
- `ModelBAtplSidewiseHardware`: Model B with the ATPL Sidewise board (slot 15 RAM-or-ROM, write-protect)
- `ModelBWatfordRomRamHardware`: Model B with the Watford Electronics ROM/RAM board
- `ModelBIntegraBHardware`: Model B with the Computech Integra-B board (shadow/private RAM, real-time clock)
- `Master128Hardware`: BBC Master 128 (LYNNE, HAZEL, ANDY, readable ROMSEL and ACCCON, the MC146818 on the System VIA, the keypad, the 1770 at &FE24, two Tubes, cartridge slots), on the `Cmos65C02` CPU policy

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

Two controllers for the Model B's FDC socket, chosen with `--fdc`:
- Intel 8271 (`acorn-8271`): the original Model B controller, FM only, for DFS 0.90 to 1.22; modelled on the reverse-engineered chip behaviour
- WD1770 (`acorn-1770`): Type I-IV command set, for DFS 2.xx; built into the B+
- SSD/DSD/HFE image formats with automatic format detection
- A DFS ROM manifest warns when a fitted DFS needs the other controller

## Documentation

### Component Documentation (`docs/`)

- `client-specification.md` - The normative client behavioural specification: what any Beebium client lets a user accomplish and how each capability behaves, derived from the macOS client (the first implementation, not the canon), with Core and optional conformance profiles, mandatory versus platform-latitude versus macOS-only marked, testable scenarios, and the proto-to-capability map; Linux and later clients are built against it
- `clock-architecture.md` - Timing model and synchronisation
- `disc-subsystem.md` - Disc controller and image formats
- `cassette.md` - The cassette interface as built: the Serial ULA's cassette side, the UEF and CSW tape sources, WAV through the front-end model, live audio input, the deck, the signal map and the append point, CassetteService, the motor LED, presets and machine directories, `--tape`, where the GUI clients' deck is specified, and testing
- `live-input-routing.md` - Routing another program's audio into the cassette deck's live input: the macOS procedure with BlackHole and a Multi-Output Device (volume sliders, sample rate, pick the device again after a change), the PipeWire monitor route on Linux, and how to measure what the virtual device receives
- `sound-subsystem.md` - SN76489 and audio pipeline
- `video-subsystem.md` - Display rendering pipeline
- `grpc-server.md` - Service API documentation
- `machine-directory.md` - A machine's directory, transient or saved: layout, machine.json, the document file, battery-backed state (nvram/, extensions/<id>/nvram/), the lock, and Save of a running machine
- `lifecycle-management.md` - Connection liveness (heartbeat, graceful/crash/unreachable), client teardown, window/server shutdown, and the rules behind them
- `screen-text-library.md` - Reading text off the screen: the standalone recognition library and its CLI
- `tube-subsystem.md` - The Tube as built: hardware reference, the coprocessor contract in summary, execution, debugger, plugins and firmware
- `tube-coprocessor-contract.md` - The normative coprocessor contract and the step-by-step record of the architecture programme
- `coprocessor-extension-guide.md` - How to add a new coprocessor (Z80, 6809, NS32016, 80186...) as a plugin
- `emulation-thread-ownership.md` - Who owns emulated state and how a service or extension reaches it safely from a gRPC thread: the pause/quiesce primitive, the busy scope, and the TSan witnesses that enforce it
- `keyboard.md` - Keyboard matrix and input handling
- `keyboard-mapping-files.md` - The cross-client keyboard mapping file format (version 2): physical keys named by W3C code values, host modifiers, BBC keys by catalogue name, platform escape entries, resolution order, the built-in mappings, conformance corpus, and the conversion from the macOS client's version 1 form (#231)
- `frontend-modifier-keys.md` - How a front-end must translate host modifier keys (SHIFT/CTRL) to avoid stuck-modifier desync; platform-independent rules plus macOS specifics, for future front-ends
- `frontend-local-server-gating.md` - How a front-end discovers whether the server shares its filesystem (SystemInfo.host_fingerprint), and which path-exchanging features must be gated on it; platform-independent rules plus macOS specifics, for future front-ends
- `frontend-sleep-wake.md` - How a front-end survives host sleep: present-on-frame for display sleep, and a platform-abstracted wake signal driving active reconnect for full system sleep; for future front-ends
- `performance.md` - Measuring what a headless server costs on a slow host (a Raspberry Pi above all): the three numbers, the pacing statistics, thread names, the profiling build, `tools/perf/profile_server.py`, the Pi baseline and what an SPSC ring needs
- `indicators.md` - LED status indicators
- `printer-port.md` - The parallel printer port as built: the hardware and the MOS driver, `PrinterPortDevice` and the `CentronicsReceiver` handshake, busy versus offline, and the four sinks (`file-printer`, `job-file-printer`, `tcp-printer` over raw TCP 9100, `host-parallel-printer` on the host's lp device) with field notes on network printers and USB-to-IEEE 1284 adapters
- `analogue-port.md` - The uPD7002 analogue-to-digital converter as built: the registers at &FEC0-&FEDF, status semantics, the measured conversion-time model (8-bit line, 12-bit sawtooth) in 1 MHz clocks, EOC into System VIA CB1, the no-reset rule across Break, the unplugged-port decision, inspection and testing; the port and its device seam; controllers (devices a person holds, publishing named axes and buttons a client sets through ControllerService), the built-in joystick, and the Voltmace Delta 14B/1 adapter in the analogue and user ports together (#98)
- `music-5000.md` - The Hybrid Music 5000 / 3000 synthesiser as built: its architecture as a plugin on the 1 MHz bus and a source of audio frames, the framework it rests on, its lifecycle and configuration, JIM paging, the register map and the sixteen-channel DSP modelled from the original schematic and the AM6070 datasheet, the fourth-order analogue output filter and the 46875 to 48000 Hz resampler, the audio frame field it fills, presets, and the known simplifications; `docs/music5000/` holds the reference material and `hardware-model.md`, the normative register-transfer description
- `master-128.md` - The BBC Master 128 as built: the memory and the ROMSEL/ACCCON paging rules (X, E's SYNC rule, Y, D, TST, IRR, ITU, IFJ), the SHEILA and 1 MHz maps and the stale data bus, the MC146818 on the System VIA with the Welcome Guide CMOS seed and the master-128-cmos unit, the keypad, the 1770's Master latch, the two Tubes and placement with `at`, FRED/JIM by IFJ, the cartridge slots whose cartridges provide their banks, the modem connector, LK18/LK19, the presets, the debugger names, the tests, and what is unverified (#189)
- `sideways-slots.md` - Sideways ROM/RAM topology, validation rules, motherboard links
- `deployment.md` - Installed layout and runtime ROM/preset discovery
- `python-debugger-repl.md` - Driving a running server from a Python REPL as a command-line debugger: connect, stop and run, registers and disassembly, peek/bus and named-region memory access, stepping, conditional breakpoints and watchpoints with the machine's condition names, run-for-cycles and run_to, with a transcript from a Master 128 session
- `preset-schema.md` - The preset file format: every key, path resolution, which models accept what, CLI-over-preset precedence, resolve-preset and describe-preset-schema
- `packaging.md` - Server distribution: the self-contained static `.deb`/`.tar.gz` bundles, platforms, CI, status
- `versioning-and-compatibility.md` - Monorepo versioning (bump-my-version) and the protocol fingerprint compatibility handshake

### Design Discussions (`docs/discussion/`)

- `debugger-capabilities.md` - Debugging features roadmap
- `raspberry-pi-linux-client-discussion.md` and `linux-client-architecture.md` - The Linux / Raspberry Pi client: the three-contract approach (server capabilities, client specification, platform presentation) and the Qt 6 / Vulkan architecture note (non-normative)
- `memory-interface-design.md` - Python debugger API
- `master-128-research.md` - The BBC Master 128 against the B and B+ (65SC12, ACCCON and the shadow rule, the SHEILA map, the 1770 latch, the MC146818 on the System VIA, the keypad, the internal and external Tube, cartridges, the modem connector), how B2, B-Em, BeebEm and beebjit model it, what Beebium adds, decisions, and what to measure on a real Master (#189)

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
- Eight machine variants (Model B, B+, B+ 128K, Model B with the notional ROM/RAM, ATPL Sidewise, Watford ROM/RAM and Integra-B boards, and the Master 128)
- 6522 VIA (System and User) with full timer and interrupt support
- 6845 CRTC with display timing
- Video ULA with all display modes (0-7)
- SN76489 sound chip
- Intel 8271 and WD1770 disc controllers with SSD/DSD/HFE support
- Cassette interface (UEF and CSW tapes, and WAV recordings through a model of the LM324 input stages) through the Serial ULA's data separator and high-tone detector, with the deck, CassetteService and the motor LED; recording on UEF tapes through the Serial ULA's tone synthesiser, written back to the tape's file; blank tape templates (C15 to C90) with `list-blank-tapes`/`create-tape`; live audio input streamed by a client (StreamAudio) through a primed ring
- Hybrid Music 5000 / 3000 synthesiser on the 1 MHz bus (plugin `hybrid-music-5000`), with the board's analogue output filter; JIM paging is owned by the bus port so several 1 MHz bus devices coexist
- Keyboard matrix with type-ahead
- Tube coprocessors as plugins: Acorn 6502 Second Processor (3 MHz) and 65C102 Co-processor (4 MHz), single-threaded host-time-driven contract, family-agnostic debugger
- gRPC service layer (19 services)
- macOS frontend with Metal rendering
- Linux / Raspberry Pi frontend (Qt 6 Widgets, QRhi on Vulkan) built against `docs/client-specification.md`: all eight sidebar modes, the New Machine configurator, the preset library, screen text selection and Copy As, Meta / Ctrl+Alt application shortcuts; per-distribution `.deb` packages (Debian 13, Ubuntu 26.04)
- Service advertisement AND browse/discovery over mDNS on all platforms: macOS (Bonjour), Linux (Avahi, dlopen'd), Windows (dual-provider: Apple Bonjour dnssd.dll when installed, else native DnsService*, selected at runtime). Full bidirectional AUN peer discovery everywhere
- Econet: MC6854 ADLC with the four-way handshake, and two transports as plugins: AUN (UDP; mDNS peer discovery, the per-user aun-map.json map file, `--station auto`) and Piconet (a USB bridge to a real Econet network). See `docs/networking.md`
- Python test client

### Future Work

- Windows frontend
- Linux client: Flatpak packaging
- CRT shader pipeline
