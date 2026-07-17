# Beebium gRPC Server Interface

Beebium provides model-specific server executables that run the BBC Micro emulator as a headless server, exposing video output, keyboard input, and debugging via gRPC.

## Available Executables

| Executable | Machine | MOS ROM | Description |
|------------|---------|---------|-------------|
| `beebium-model-b` | BBC Model B | MOS 1.20 | Original 32K BBC Micro |
| `beebium-model-b-plus` | BBC Model B+ 64K | MOS 2.0 | Enhanced 64K model |

Each executable contains only the hardware emulation needed for that machine type.

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew  # macOS with Homebrew
make -j4
```

Requires gRPC and Protobuf installed:
```bash
brew install grpc protobuf  # macOS
```

## Running the Server

```bash
./src/server/beebium-model-b [options]
```

All arguments are optional. By default, the server loads the appropriate MOS ROM and BBC BASIC II for the machine type.

### Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--mos <filepath>` | Machine-specific | Path to MOS ROM |
| `--rom <slot>:<filepath>` | Slot 15: bbc-basic_2.rom | Load ROM into sideways slot (0-15) |
| `--rom-dir <dirpath>` | Auto-detected | ROM directory path |
| `--port <port>` | 48875 | gRPC server port (use 0 for dynamic allocation) |
| `--wait[=mode]` | - | Delay emulation start (see [cli.md](cli.md)) |
| `--info` | - | Print machine info as JSON and exit |
| `--help` | - | Show usage |

### Examples

```bash
# Use all defaults (recommended)
./beebium-model-b

# Replace BASIC with Forth in slot 15
./beebium-model-b --rom 15:forth.rom

# Add DFS in slot 14
./beebium-model-b --rom 14:dfs.rom

# Multiple ROMs
./beebium-model-b --rom 14:dfs.rom --rom 13:viewsheet.rom

# Override ROM directory
./beebium-model-b --rom-dir /my/roms

# Machine discovery
./beebium-model-b --info
```

The server runs until interrupted with Ctrl+C.

### Machine Discovery

The `--info` flag outputs machine information as JSON:

```json
{
  "executable": "beebium-model-b",
  "machine_type": "ModelB",
  "display_name": "BBC Model B",
  "version": "0.1.0",
  "default_mos_rom": "acorn-mos_1_20.rom",
  "default_language_rom": "bbc-basic_2.rom",
  "default_language_slot": 15
}
```

Frontends can use this to enumerate available machine types.

See [deployment.md](deployment.md) for ROM discovery and installation details.

## gRPC Services

All services are defined in `src/service/proto/`. The major services are documented below. Additional services (AudioService, DiscService, EconetService, IndicatorService, SidewaysService, TubeService) are defined in their respective proto files.

### VideoService

Streams video frames from the emulator.

**Proto file:** `src/service/proto/video.proto`

#### GetConfig

Returns video configuration.

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto video.proto \
  localhost:48875 beebium.VideoService/GetConfig
```

Response:
```json
{
  "width": 736,
  "height": 576,
  "framerateHz": 50
}
```

#### SubscribeFrames

Streams frames as they complete (at VSYNC, ~50Hz).

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto video.proto \
  localhost:48875 beebium.VideoService/SubscribeFrames
```

Each frame contains:
- `frameNumber` - Monotonically increasing frame counter
- `width`, `height` - Frame dimensions (736×576)
- `pixels` - BGRA32 pixel data (~1.7MB per frame)

### KeyboardService

Controls the BBC keyboard matrix.

**Proto file:** `src/service/proto/keyboard.proto`

#### KeyDown / KeyUp

Press or release a key by matrix position.

```bash
# Press key at row 4, column 1
grpcurl -plaintext \
  -import-path src/service/proto -proto keyboard.proto \
  -d '{"row": 4, "column": 1}' \
  localhost:48875 beebium.KeyboardService/KeyDown

# Release the key
grpcurl -plaintext \
  -import-path src/service/proto -proto keyboard.proto \
  -d '{"row": 4, "column": 1}' \
  localhost:48875 beebium.KeyboardService/KeyUp
```

#### GetState

Returns the current keyboard matrix state.

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto keyboard.proto \
  localhost:48875 beebium.KeyboardService/GetState
```

Response:
```json
{
  "pressedRows": [0, 0, 0, 0, 2, 0, 0, 0, 0, 0]
}
```

Each element is a bitmask of pressed columns for that row.

### DebuggerControl

Execution control, breakpoints, watchpoints, memory access, and CPU state for 6502-based machines. Available on both host and parasite processors.

**Proto file:** `src/service/proto/debugger.proto`

#### Execution Control

| RPC | Description |
|-----|-------------|
| `GetState` | Returns `ExecutionState` (is_running, cycle_count, halt_reason, sequence) |
| `Run` | Start execution. Returns `RunResponse` with success/error. |
| `Stop` | Stop execution. Returns `StopResponse` with the state at stop. |
| `Reset` | Reset the machine (leaves it stopped at cycle 7). |
| `StepInstruction` | Execute N instructions (count in `StepRequest`). Returns cycles/instructions executed. |
| `StepCycle` | Execute N cycles (count in `StepRequest`). Returns cycles/instructions executed. |

```bash
# Get current execution state
grpcurl -plaintext -import-path src/service/proto -proto debugger.proto \
  localhost:48875 beebium.DebuggerControl/GetState

# Step 10 instructions
grpcurl -plaintext -import-path src/service/proto -proto debugger.proto \
  -d '{"count": 10}' \
  localhost:48875 beebium.DebuggerControl/StepInstruction
```

#### Event Streaming

`WatchExecutionState` is a server-streaming RPC that pushes `ExecutionStateEvent` messages whenever execution state changes (breakpoint hit, manual stop, run resumed, step complete, watchpoint hit, or counterpart stop).

The server sends the current state immediately on subscription. Multiple concurrent subscribers are supported; each receives all events independently via per-subscriber fan-out.

Each event contains:
- `reason` -- `StopReason` enum: BREAKPOINT, MANUAL, ERROR, STEP_COMPLETE, WATCHPOINT, COUNTERPART
- `state` -- Current `ExecutionState` (is_running, cycle_count, sequence)
- `message` -- Human-readable description (e.g. "breakpoint at $0402")
- `watchpoint_hit` -- For WATCHPOINT stops: watchpoint_id, address, value, is_write

#### Breakpoints

Breakpoints fire at instruction boundaries when PC falls within a specified address range.

| RPC | Description |
|-----|-------------|
| `AddBreakpoint` | Add a breakpoint. Returns the breakpoint ID. |
| `RemoveBreakpoint` | Remove by ID. Returns success/failure. |
| `ListBreakpoints` | List all breakpoints with live hit counts. |
| `ClearBreakpoints` | Remove all. Returns count removed. |

`AddBreakpointRequest` fields:

| Field | Description |
|-------|-------------|
| `start_address` | Start of range (inclusive) |
| `end_address` | End of range (exclusive). 0 means start+1 (single address). Use 0x10000 for a full-range breakpoint. |
| `condition` | Optional expression. Empty = unconditional. |
| `stop_counterpart` | Signal the other processor (host/parasite) to stop. |

**Condition expressions** are compiled server-side when the breakpoint is added. An invalid expression returns gRPC `INVALID_ARGUMENT` with the parse error as the status detail. Available variables:

| Variable | Description |
|----------|-------------|
| `A`, `X`, `Y`, `SP`, `PC`, `P` | CPU registers |
| `C`, `Z`, `I`, `D`, `V`, `N` | Processor status flags (0 or 1) |
| `cycles` | Machine cycle count (uint64) |
| `hits` | Number of times this breakpoint has been reached |
| `mem[addr]` | Peek memory at address (side-effect-free) |
| `true`, `false` | Boolean literals |

Operators: `==`, `!=`, `<`, `<=`, `>`, `>=`, `+`, `-`, `*`, `/`, `%`, `&&`, `||`, `!`, `(`, `)`. Integer literals: decimal, `0x` hex, `0b` binary.

Examples:
```bash
# Simple breakpoint at $C000
grpcurl -plaintext -import-path src/service/proto -proto debugger.proto \
  -d '{"start_address": 49152}' \
  localhost:48875 beebium.DebuggerControl/AddBreakpoint

# Conditional: stop when A == 0x42
grpcurl -plaintext -import-path src/service/proto -proto debugger.proto \
  -d '{"start_address": 1026, "condition": "A == 0x42"}' \
  localhost:48875 beebium.DebuggerControl/AddBreakpoint

# Hit count: stop on 5th hit
grpcurl -plaintext -import-path src/service/proto -proto debugger.proto \
  -d '{"start_address": 1026, "condition": "hits == 5"}' \
  localhost:48875 beebium.DebuggerControl/AddBreakpoint

# Cycle budget: full-range breakpoint that stops after 1000 cycles
grpcurl -plaintext -import-path src/service/proto -proto debugger.proto \
  -d '{"start_address": 0, "end_address": 65536, "condition": "cycles >= 1000"}' \
  localhost:48875 beebium.DebuggerControl/AddBreakpoint

# Memory predicate: stop when $0500 reaches $0A
grpcurl -plaintext -import-path src/service/proto -proto debugger.proto \
  -d '{"start_address": 0, "end_address": 65536, "condition": "mem[0x0500] == 0x0A"}' \
  localhost:48875 beebium.DebuggerControl/AddBreakpoint
```

#### Watchpoints

Watchpoints fire when a memory address within a range is read, written, or either, depending on the watchpoint type. They support the same condition expressions as breakpoints.

| RPC | Description |
|-----|-------------|
| `AddWatchpoint` | Add a watchpoint. Returns the watchpoint ID. |
| `RemoveWatchpoint` | Remove by ID. Returns success/failure. |
| `ListWatchpoints` | List all watchpoints with live hit counts. |
| `ClearWatchpoints` | Remove all. Returns count removed. |

`AddWatchpointRequest` fields:

| Field | Description |
|-------|-------------|
| `start_address` | Start of range (inclusive) |
| `end_address` | End of range (exclusive) |
| `type` | `WATCHPOINT_READ` (0), `WATCHPOINT_WRITE` (1), or `WATCHPOINT_BOTH` (2) |
| `condition` | Optional expression. Same syntax as breakpoint conditions. |
| `stop_counterpart` | Signal the other processor to stop. |

When a watchpoint fires, the `ExecutionStateEvent` includes a `WatchpointHitInfo` with the watchpoint ID, the accessed address, the value, and whether it was a write.

#### Memory Access

Three access modes for the 16-bit address space:

| RPC | Side effects | Use case |
|-----|-------------|----------|
| `ReadMemory` | Yes | Trigger hardware behaviour (e.g. clear VIA interrupt flags) |
| `PeekMemory` | No | Inspect memory without disturbing hardware state |
| `WriteMemory` | Yes | Write data to memory-mapped hardware or RAM |

All three accept an optional `simulated_pc` field for Model B+ shadow RAM routing. When set, memory access is routed as if code at that PC were executing (VDU driver code at $C000-$DFFF sees shadow RAM at $3000-$7FFF).

#### Memory Regions

Named regions provide direct access to physical memory areas, bypassing the address-space mapper. This is essential for inspecting shadow RAM, sideways ROM banks, or other hardware that occupies the same address range.

| RPC | Description |
|-----|-------------|
| `GetMemoryRegions` | List all regions with names, sizes, and properties |
| `PeekRegion` | Side-effect-free read from a named region |
| `ReadRegion` | Side-effecting read from a named region |
| `WriteRegion` | Write to a named region |

Each `MemoryRegionInfo` describes a region's name (e.g. `"main_ram"`, `"shadow_ram"`, `"bank_0"`), base address, size, and whether it is readable, writable, has side effects, is populated, and is currently active.

#### CPU State

| RPC | Description |
|-----|-------------|
| `Get6502State` | Returns all registers, flags, interrupt handler tracking, and device IRQ/NMI state |
| `Set6502State` | Set one or more registers (all fields are optional) |

`Cpu6502State` includes: A, X, Y, SP, PC, P, in_nmi_handler, in_irq_handler, nmi_pending, irq_pending, device_irq_flags, device_nmi_flags.

### DeviceInspection

Read-only access to BBC Micro peripheral state. Host-only; parasite returns UNIMPLEMENTED.

**Proto file:** `src/service/proto/debugger.proto`

| RPC | Returns | Description |
|-----|---------|-------------|
| `GetSystemViaState` | `ViaState` | System VIA (IC1): keyboard scanning, sound, timers, interrupts |
| `GetUserViaState` | `ViaState` | User VIA (IC69): user port, printer, timer interrupts |
| `GetCrtcState` | `CrtcState` | 6845 CRTC: 18 registers, timing counters, sync state |
| `GetVideoUlaState` | `VideoUlaState` | Video ULA: control register and 16-entry palette |
| `GetAddressableLatchState` | `AddressableLatchState` | IC32 latch: screen base, LEDs, sound/speech enables |
| `GetSoundGeneratorState` | `SoundGeneratorState` | SN76489: 4 channels with frequency, volume, LFSR state |
| `GetTubeState` | `TubeState` | Tube ULA: control flags, all registers, FIFOs, status, interrupts, transfer counters |

```bash
# Inspect System VIA state
grpcurl -plaintext -import-path src/service/proto -proto debugger.proto \
  localhost:48875 beebium.DeviceInspection/GetSystemViaState

# Inspect CRTC registers
grpcurl -plaintext -import-path src/service/proto -proto debugger.proto \
  localhost:48875 beebium.DeviceInspection/GetCrtcState
```

### SystemService

Provides system information and server lifecycle events.

**Proto file:** `src/service/proto/system.proto`

#### GetSystemInfo

Returns machine identification.

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto system.proto \
  localhost:48875 beebium.SystemService/GetSystemInfo
```

Response:
```json
{
  "machineType": "ModelB",
  "machineDisplayName": "BBC Model B"
}
```

#### WatchServerStatus (Server Streaming)

Subscribe to server lifecycle events. The server sends `SERVER_STATUS_READY` immediately upon subscription, then `SERVER_STATUS_SHUTTING_DOWN` when the server receives a shutdown signal (Ctrl+C or SIGTERM).

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto system.proto \
  localhost:48875 beebium.SystemService/WatchServerStatus
```

Events:
```json
{"status": "SERVER_STATUS_READY", "message": "Server ready"}
```
```json
{"status": "SERVER_STATUS_SHUTTING_DOWN", "message": "Server shutting down", "shutdownGraceMs": 5000}
```

Use this to detect server shutdown and cleanly disconnect clients. The `shutdownGraceMs` field indicates how long clients have to finish pending operations before the server terminates.

## BBC Keyboard Matrix

The BBC Micro uses a 10×10 keyboard matrix. Common key positions:

| Key | Row | Column |
|-----|-----|--------|
| SHIFT | 0 | 0 |
| CTRL | 0 | 1 |
| A | 4 | 1 |
| B | 6 | 4 |
| RETURN | 4 | 9 |
| SPACE | 6 | 2 |

For the complete matrix, see `docs/keyboard-and-display.md`.

## Testing

### C++ Tests

The test suite includes gRPC client tests for each service:

```bash
cd build
./tests/test_grpc_video      # Video service tests
./tests/test_grpc_keyboard   # Keyboard service tests
./tests/test_grpc_debugger   # Debugger service tests (48 tests)
./tests/test_grpc_indicator  # Indicator service tests
./tests/test_expression      # Condition expression compiler tests
```

These tests start a local server, connect as a gRPC client, and verify the services work correctly.

### Python Tests

The Python client includes integration tests for the debugger:

```bash
cd clients/beebium-python-client
source .venv/bin/activate
python -m pytest tests/test_debugger.py -v
```

### TypeScript Tests

The TypeScript client has integration tests for all services:

```bash
cd clients/beebium-typescript-client
npx vitest run tests/integration.test.ts           # 48 general integration tests
npx vitest run tests/debugger-integration.test.ts   # 24 debugger-specific tests
```

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Client (grpcurl, Python, Swift, etc.)          │
└─────────────────────────────────────────────────┘
                      │ gRPC (port 48875)
                      ▼
┌─────────────────────────────────────────────────┐
│  beebium-model-b / beebium-model-b-plus         │
│  ├── Server<MachineType>                        │
│  │   ├── VideoService (frame streaming)         │
│  │   ├── KeyboardService (input handling)       │
│  │   ├── SystemService (identity, lifecycle)    │
│  │   ├── DebuggerControl (execution, memory,    │
│  │   │    breakpoints, watchpoints, CPU state)  │
│  │   └── DeviceInspection (VIA, CRTC, ULA,     │
│  │        sound, latch, Tube)                   │
│  └── Render thread (PixelBatch → FrameBuffer)   │
└─────────────────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────┐
│  beebium_core                                   │
│  ├── Machine<CpuPolicy, MemoryPolicy>           │
│  ├── OutputQueue<PixelBatch> (video pipeline)   │
│  └── SystemViaPeripheral (keyboard matrix)      │
└─────────────────────────────────────────────────┘
```

The server runs three threads:
1. **Main thread** - Emulation loop (~2MHz)
2. **Render thread** - Consumes PixelBatch queue, renders to FrameBuffer
3. **gRPC threads** - Handle client connections

## Frame Format

Frames are 736×576 pixels in BGRA32 format (4 bytes per pixel):
- Byte 0: Blue (0-255)
- Byte 1: Green (0-255)
- Byte 2: Red (0-255)
- Byte 3: Alpha (always 255)

Total frame size: 736 × 576 × 4 = 1,695,744 bytes (~1.7MB)

The frame includes overscan areas; the active display area depends on the video mode.
