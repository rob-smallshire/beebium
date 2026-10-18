# Beebium gRPC Server Interface

Beebium provides model-specific server executables that run the BBC Micro emulator as a headless server, exposing video output, keyboard input, and debugging via gRPC.

## Available Executables

There is one server executable per machine variant (`beebium-model-b`, `beebium-model-b-plus`, `beebium-model-b-integra-b` and so on), each containing only the hardware emulation for that machine. The full list, with what each machine provides, is in [cli.md](cli.md); the ROMs each loads by default are in [deployment.md](deployment.md).

## Building

See [building.md](building.md).

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

All core services are defined in `src/service/proto/`. The major services are documented below. Additional services (AudioService, DiscService, IndicatorService, SidewaysService, TubeService, SerialService, CoprocessorDebuggerControl) are defined in their respective proto files; ExtensionUiService, which serves extensions' control panels, is in `src/core/extension-api/proto/extension_ui.proto`. Extension-provided APIs (e.g. `AunService`) are not gRPC services of their own: they are reached through [ExtensionRpc](#extensionrpc).

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
- `cycleCount` - Emulated CPU cycle at which the frame completed

#### CaptureFrame

Returns the frame the machine completed at or after an emulated cycle
(`cycleCount >= afterCycle`): the current frame if it already qualifies,
otherwise the next to complete, never one stamped earlier. Frames completing
while the request waits are examined as they are published, so the one
returned is the first to qualify however busy the server is. Only the latest
frame is held, so a qualifying frame that completed before the request arrived
cannot be returned once a newer one has replaced it. The machine must run for
a later frame to complete; the server waits up to `timeoutMs` (default 5000,
at most 60000) and then fails with `DEADLINE_EXCEEDED`.

```bash
grpcurl -plaintext -d '{"afterCycle": 4000000, "timeoutMs": 2000}' \
  -import-path src/service/proto -proto video.proto \
  localhost:48875 beebium.VideoService/CaptureFrame
```

#### GetScreenText

Reads text from the display, whatever mode is producing it. The caller selects
in pixels -- the one coordinate system every mode shares -- and the server
chooses a reading strategy per band of scanlines, so a split screen is read a
band at a time and the caller never learns which mode produced what.

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto video.proto \
  localhost:48875 beebium.VideoService/GetScreenText
```

Request fields, all optional:
- `region` - `{x, y, width, height}` in frame pixels; the whole display when
  unset, and clipped to it rather than rejected
- `search` - `ALIGNED` reads only text on the character grid, which is what a
  snapped drag wants; `ANYWHERE`, the default, also reads text placed freely
  with VDU 5 and is a strict superset of `ALIGNED`. Honoured by strategies that
  recognise glyphs in pixels
- `layout` - `ROWS` (default) gives each grid row its own line; `FLOWED`
  rejoins a line that wrapped at the right edge
- `characters` - which meaning a MODE 7 byte carries. `CODES`, the default,
  takes the byte at face value, so `[`, `]` and `^` come back as themselves and
  a copied BASIC listing keeps its assembler blocks and its exponentiation.
  `DISPLAYED` reports the glyphs the SAA5050 actually drew for the eleven codes
  where its repertoire differs from ASCII -- arrows, fractions, a division sign
  -- for capturing a teletext page as it looked. Only the caller knows which was
  meant, so the mapping is the server's and the choice is theirs
- `holdId` - read a screen held by `HoldScreen` rather than the live one, so
  the reading describes the still the caller is looking at (see below).
  `NOT_FOUND` when the hold is unknown or has expired

Response:
- `supported` - whether any band of the region had a strategy that could read
  it. Distinct from readable-but-empty: a display that was read and found to
  hold no text is supported with no runs, whereas a display no strategy can
  read is unsupported. MODE 7 is read exactly from the character grid; MODEs
  0-6 are read by recognising glyphs in the pixels
- `runs` - each a piece of text, its pixel bounds, and the cell geometry it was
  read with, so a client can highlight or snap to exactly what it captured
- `runs[].cells` - the run's cells in reading order, each with its own `bounds`
  and a `matched` flag. A client highlighting what was read uses these rather
  than the whole run's bounds, which spans unmatched cells too: an unmatched
  cell had ink no glyph fit and copies as a space, so painting it would dress a
  failed read up as a success. A genuine space *is* matched. Empty from the
  teletext strategy, whose cells are exact characters and all matched
- `text` - the runs joined by `layout`, lines separated with LF. Every cell
  contributes a character, including the unmatched ones, so columns stay
  aligned, and **every row of the region contributes a line, including the
  blank ones** -- the gap between paragraphs is part of what was on screen.
  Trailing blank rows are stripped, because a screen is 25 rows tall whatever
  is written on it and a copy should not end in the empty part. Leading blank
  rows are **kept**: they position everything after them, so dropping them
  would leave a caller that finds a line here and then reads that row back
  with a row-indexed call off by however many went
- `unreadableCells`, `ambiguousCells` - cells a strategy could not identify at
  all, and cells it read but could not pin to one character because the font
  draws two the same. Both zero for MODE 7, whose cells are exact codes

#### GetScreenGeometry

Reports the character grid the display implies, per band, in frame pixels. It
exists separately from `GetScreenText` because snapping a drag has to happen
while the drag is in progress, when the client has nothing to send yet; one
call on mouse-down is ample.

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto video.proto \
  localhost:48875 beebium.VideoService/GetScreenGeometry
```

Takes an optional `holdId`, to report the grid of a held screen rather than the
live one.

Every band reports a grid, including one no strategy can read text from: where
the cells are and what is in them are separate questions. Each band carries
`top`/`bottom`, the cell size, the `columnPitch`/`rowPitch` step, and the grid
origin. The pitch is not the cell size -- MODE 3 and MODE 6 put an
eight-scanline glyph on a ten-scanline pitch and blank the two spare lines.

Observed geometries: MODE 7 is one band of 16x20 cells over 500 scanlines;
MODE 4 is 8x8 over 256; MODE 6 is an 8x8 cell on a `rowPitch` of 10.

#### GetTeletextScreen

Reads the MODE 7 screen as attribute-rich cells: the SAA5050's per-cell state,
resolved by the chip. For plain text, use `GetScreenText` instead -- it reads
every mode, and copy uses it. `GetTeletextScreen` is the way to the one thing
`GetScreenText` does not carry: each cell's foreground and background colour,
character set (alpha / contiguous / separated graphics), concealment, flash,
double-height half, cursor, and whether it is a control code.

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto video.proto \
  localhost:48875 beebium.VideoService/GetTeletextScreen
```

Takes an optional region (`row`, `column`, `rows`, `columns`) and a `layout`.
Returns `active` false in any non-MODE-7 mode, where the cells describe whatever
was last shown in MODE 7 rather than anything current, so a caller must check
it. The attributes are captured after the control codes are resolved, so nothing
downstream re-runs the serial codes to know what a cell looked like; the same
capture feeds the teletext strategy behind `GetScreenText`. The `text` field is
provided for a caller already holding the cells, and is the same conversion
`GetScreenText` returns for a MODE 7 band.

#### GetScreenSource

Where the screen the video shows lies in memory: the memory region it fetches
from (`main_ram`, or `shadow_ram` on a B+ or B+ 128K while ACCCON bit 7 shows
it; never `shadow_ram` on an Integra-B), the CPU address of the first displayed
byte, the bytes a frame displays, and where a read continues past &7FFF. All are
computed from the live 6845 start address and geometry, the System VIA latch's
screen size and the paging, not from a screen mode; see
[video-subsystem.md](video-subsystem.md#which-memory-the-video-reads).

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto video.proto \
  localhost:48875 beebium.VideoService/GetScreenSource
```

#### HoldScreen and ReleaseScreen

Holds the screen as it stands, so a selection reads the still it was drawn on
rather than whatever the machine has drawn since.

A reading depends on four things that move independently: the pixels, the band
geometry, the teletext grid, and the font in RAM, which `VDU 23` can redefine at
any moment. Read at four different instants they describe a screen that never
existed, and on a moving display the text a user copies is not the text they
selected. `HoldScreen` captures them together; `GetScreenText` and
`GetScreenGeometry` then take a `holdId` and read the capture.

```bash
grpcurl -plaintext -d '{"includeFrame": true}' \
  -import-path src/service/proto -proto video.proto \
  localhost:48875 beebium.VideoService/HoldScreen
```

The grid comes back with the hold, so holding costs no more round trips than
fetching the geometry alone did -- and the geometry cannot then describe a
different frame from the pixels. With `includeFrame` the captured still comes
too, in the same shape the frame stream carries, so a client can display exactly
the picture its reads will be made against rather than whichever frame it last
drew.

**The emulator keeps running.** A hold is a copy, not a pause: nothing about
holding a screen reaches the machine or any other client.

Naming a hold that is unknown or has expired fails with `NOT_FOUND`. Falling
back to the live screen would be the very confusion holding exists to prevent,
and it would be silent.

Holds expire (five minutes) so a client that dies does not leak one, and the
server keeps a bounded number, dropping the oldest. A client that has finished
should call `ReleaseScreen` rather than wait for either.

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

#### Type-ahead and guest readiness

`TypeQuickly` queues text, which the server types by pressing keys in the
matrix, paced in emulated time. It presses nothing until the guest is ready for
input: `TypingStatus.guest_ready`, also on every `WatchServerStatus` event, with
a `SERVER_STATUS_GUEST_READY` event when it is set. That is the first time,
after a reset, that the guest removes a character from the MOS keyboard buffer:
an opcode fetch at the address held in the REMV vector (&022C) with X = 0. Every
MOS text-input path (OSRDCH, OSWORD 0, INKEY) makes that call, whatever the
language and with or without a Tube, and the vector is read rather than a MOS
address assumed (IBOS moves it). A reset clears it; a key down at that moment is
let go and pressed again once the guest is ready.

Earlier is too early. Keys pressed while the MOS resets are lost (with
`*RUN !BOOT` typed 0.0, 0.1 and 0.3 emulated seconds after power-on a Model B
said "No such variable", "File not found" and "Syntax error"), and, while the
ROMs take their reset service calls, read as reset options (an Integra-B started
without its filing system). The MOS enabling the keyboard interrupt is not
enough either: it scans once straight afterwards and ignores a key already down.

A guest that never asks the MOS for typed input -- a program scanning the
keyboard itself, a machine stopped at "Language?" -- holds the queue, and a
`TypingStatus` with `guest_ready` false and characters pending says so.
`KeyDown`/`KeyUp`, for game control, are never held.

### DebuggerControl

Execution control, breakpoints, watchpoints, memory access, and CPU state for 6502-based machines. Available on both host and parasite processors.

**Proto file:** `src/service/proto/debugger.proto`

#### Execution Control

| RPC | Description |
|-----|-------------|
| `GetState` | Returns `ExecutionState` (is_running, cycle_count, halt_reason, sequence) |
| `Run` | Start execution. Returns `RunResponse` with success/error. Resumes as a debugger's continue does: the breakpoints at the instruction the CPU is stopped on are stepped over (not disabled, just not reported again for that instruction), so the run stops at their next hit. `RunRequest.stop_at_current` evaluates them instead, so one there stops the CPU again at once. |
| `Stop` | Stop execution. Returns `StopResponse` with the state at stop. |
| `Reset` | Reset the machine (leaves it stopped at cycle 7). |
| `StepInstruction` | Execute N instructions (count in `StepRequest`). Returns cycles/instructions executed. Refused on a running CPU, which it never stops itself: "machine is running; call debugger.stop() before stepping" (`StepCycle` alike). |
| `StepCycle` | Execute N cycles (count in `StepRequest`). Returns cycles/instructions executed. May stop part-way through an instruction. |

**Register writes need an instruction boundary.** `StepCycle` can leave the CPU between the cycles of an instruction. A register write there would corrupt the in-flight instruction (it would fetch its remaining operands from a new PC, or overwrite the written value with its own result), so `SetCpuState` refuses it with `FAILED_PRECONDITION`: "CPU is mid-instruction after StepCycle; call StepInstruction(1) to reach the next instruction boundary before writing registers". `StepInstruction(1)` from that state completes only the in-flight instruction. A freshly reset CPU is in its reset sequence and is refused the same way; `Reset` itself leaves the machine at a boundary. A halted CPU has no instruction in flight and accepts writes. The rule applies to `CoprocessorDebuggerControl` too.

**Stops land on instruction boundaries.** A running CPU only ever stops at an instruction boundary (or while halted, which has none), so `StepCycle` is the only way to reach a mid-instruction state. A breakpoint stops before the instruction at its address. A `Stop`, or a watchpoint that fires on one of an instruction's bus accesses, completes that instruction first, with breakpoint and watchpoint callbacks suppressed, so the stop reports post-instruction state. The access a watchpoint reports has therefore already happened, and a read-modify-write instruction's second access does not fire the watchpoint again. `Stop` returns once the CPU has stopped.

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

**When conditions are evaluated.** A condition is normally evaluated at instruction boundaries. A `cycles >= N` budget therefore stops at the first instruction boundary at or after cycle N, never inside an instruction, and can be up to one instruction late (the state is always that of a completed instruction). While the CPU is halted -- the Break/reset line held, or the CPU jammed on a KIL opcode -- there are no instruction boundaries, so a **whole-address-space** (`start_address` 0, `end_address` 0x10000) breakpoint that **carries a condition** is evaluated on every cycle instead, and a cycle budget stops at exactly cycle N (a halted CPU has no instruction in flight to finish). Unconditional or partial-range breakpoints are not evaluated while the CPU is halted: the PC is static then and they would fire spuriously. This is what lets a client's cycle-budget wait (`run_until_or_timeout`) complete on a machine stopped at Break or on a jam.

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

When a watchpoint fires, the `ExecutionStateEvent` includes a `WatchpointHitInfo` with the watchpoint ID, the accessed address, the value, and whether it was a write. The machine stops at the end of the accessing instruction (see "Stops land on instruction boundaries" above).

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
| `GetCrtcState` | `CrtcState` | 6845 CRTC: 18 registers, timing counters, sync state, and the beam's field parity and cycles since the field's vsync |
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

Response (abridged):
```json
{
  "identity": {
    "uuid": "6f1c...",
    "name": "Station 80 (AUN, Model B) #2",
    "modelType": "ModelB",
    "modelName": "BBC Model B",
    "description": "Station 80 (AUN)",
    "descriptionTemplate": "Station {econet-station} (AUN)"
  },
  "clockSpeedHz": 2000000,
  "protocolFingerprint": "...",
  "machineDirpath": "/Users/me/Library/Application Support/Beebium/machines/6f1c...",
  "machineSaved": false
}
```

`machineDirpath` is the machine's directory on the server's host: a saved
machine's, or the transient one a machine not yet saved runs in;
`machineSaved` says which, so a front-end offers Save for the one and Save
As for the other (see [machine-directory.md](machine-directory.md)).

`identity.name` is the machine's name: plain text, never a template, and what
titles and the mDNS announcement show. A saved machine's is its document's
name (its directory's name without `.beebium-machine`); a machine not yet saved
has the name it was launched with, else its preset's name and its launcher's
ordinal. `identity.description` is shown beside the name: the rendering of
`identity.descriptionTemplate`, a template (#153) in which `{key}` stands for a
placeholder's current value, re-rendered about once a second, so a placeholder
such as `{econet-station}` keeps it current; both are empty when the machine
has no description. See [cli.md](cli.md#machine-name-and-description).

#### SetMachineName

Renames the machine, to a plain name, and returns its identity. A machine not
yet saved simply takes the name. A saved machine is named by its document, so
its document is renamed: a [MoveMachine](#movemachine) to
`<parent>/<name>.beebium-machine`, after which `SystemInfo.machineDirpath`
reports the new directory. A name that cannot name a file on every host is
refused with `INVALID_ARGUMENT`, saying why, and one whose directory already
exists with `ALREADY_EXISTS`; nothing changes then. An empty name is
refused with `INVALID_ARGUMENT`; the same name succeeds and changes nothing. A
rename is announced to `WatchServerStatus` watchers and re-published over mDNS
at once.

#### SetMachineDescription

Sets the description template (empty for none) and returns the identity with
its new rendering, plus a `report` (a `DescriptionTemplateReport`) of what the
rendering could not substitute, so a client can warn without parsing the
template: `unknownKeys` (each `{...}` that is not a known key, rendered as
written), `inapplicableKeys` (known keys that do not apply to this machine,
rendered empty) and `malformed` (an unterminated `{...` or a lone `}`, rendered
literally). Only the caller gets the report; a client watching the status
stream that wants one calls `PreviewMachineDescription`. A saved machine keeps
it in its `machine.json`. The change is announced to `WatchServerStatus`
watchers.

```bash
grpcurl -plaintext -d '{"descriptionTemplate": "Station {econet-station} {econet-sttion}"}' \
  -import-path src/service/proto -proto system.proto \
  localhost:48875 beebium.SystemService/SetMachineDescription
```

```json
{
  "identity": {"description": "Station 80 {econet-sttion}",
               "descriptionTemplate": "Station {econet-station} {econet-sttion}", "...": "..."},
  "report": {"unknownKeys": ["econet-sttion"]}
}
```

#### ListDescriptionPlaceholders

Lists every placeholder a description template can use on this server -- the
core's and any loaded extension's -- with its `key`, `label`, `description`,
`group` (a picker heading), `insertion` (the text a picker inserts, `{key}`),
current `value`, and `applicable` (false when this machine cannot have a
value). Clients carry no list of keys; they ask. Every key begins with its
owner's domain (`machine-`, `econet-`, ...).

```json
{"placeholders": [
  {"key": "machine-model", "label": "Machine model", "group": "Machine",
   "insertion": "{machine-model}", "value": "BBC Model B", "applicable": true,
   "description": "..."},
  {"key": "econet-station", "label": "Econet station", "group": "Econet",
   "insertion": "{econet-station}", "value": "80", "applicable": true,
   "description": "..."}
]}
```

#### PreviewMachineDescription

Renders a description template against the current values without changing
anything, for a live preview while the user edits. Returns `description` and
the same `report` as `SetMachineDescription`.

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
{"status": "SERVER_STATUS_IDENTITY_CHANGED", "identity": {"name": "Lab Beeb", "description": "Station 81 (AUN)", "...": "..."}}
```
```json
{"status": "SERVER_STATUS_SHUTTING_DOWN", "message": "Server shutting down", "shutdownGraceMs": 5000}
```

`SERVER_STATUS_IDENTITY_CHANGED` carries the new identity whenever its uuid,
name or description changes: a `SetMachineName`, `MoveMachine` or `SetMachineDescription`, a
re-rendering of the description in which a placeholder's value changed (a
renumbered station, at its next Break), or a Save that gives the machine a
document (its name) or a Save As (a new uuid and name). Every event, heartbeats included, carries `templateMediaWritten`: true
while a machine not yet saved has written to the copies of the media its
preset named, which are lost unless it is saved, so a front-end can ask
whether to save it before closing. It is always false for a saved machine, and
a Save clears it. Use this to detect server
shutdown and cleanly disconnect clients. The `shutdownGraceMs` field indicates how long clients have to finish pending operations before the server terminates.

#### SaveMachine

Saves the machine without stopping it. With emulation paused, the server
flushes the drives, makes the destination whole beside it and renames it into
place, re-points every medium open in the old directory at its copy, and moves
the machine's lock; the machine then runs on from the saved directory. Three
forms, by the machine and `destinationDirpath`:

| Machine | Destination | Result |
|---------|-------------|--------|
| not yet saved | a new directory | saved there with its uuid; its transient directory is removed |
| saved | empty | its battery-backed state, screen and description written in place |
| saved | a new directory | Save As: a copy with a new uuid, from which the machine runs on |

The response gives the machine's directory and identity now. A destination
that exists is refused with `ALREADY_EXISTS`, unless `replaceExisting`: then
a saved machine there, which no process is running and which is not this
machine's own, is replaced once the new one is whole (put back if anything
fails); any other is refused with `FAILED_PRECONDITION`, saying why. A machine
not yet saved with no destination is `INVALID_ARGUMENT`; a directory that
cannot be written is `INTERNAL`, with the machine left where it was.

```bash
grpcurl -plaintext -d '{"destinationDirpath": "/Users/me/Documents/Elite.beebium-machine"}' \
  -import-path src/service/proto -proto system.proto \
  localhost:48875 beebium.SystemService/SaveMachine
```

Two things a Save As does not change. An Econet transport keeps the uuid it
was given when the machine started (the AUN transport's identity on the
network) until the next start. The directory the machine leaves keeps its
media as they were written up to the moment of the Save -- media are written
continuously -- while its battery-backed state and screen are as at its last
Save.

#### MoveMachine

Moves a saved machine's directory to `destinationDirpath`, the new directory
in full, without stopping the machine, and returns its directory and identity
now, as `SaveMachine` does. This is what the macOS title bar's Name and Where
do. The machine keeps its uuid and runs on from the new directory: its open
media are re-pointed, its lock and document file follow, its name becomes the
new document name (announced as `SERVER_STATUS_IDENTITY_CHANGED` and
re-published over mDNS), and the old directory is gone. On one volume the
directory is renamed; across volumes, and always on Windows, which will not
rename a directory holding open files, it is copied as by Save As but keeping
its uuid and creation time, and the old one removed.

Moving to the machine's own directory succeeds and changes nothing. Refusals,
each leaving the machine where it was: `FAILED_PRECONDITION` for a machine not
yet saved (save it first); `INVALID_ARGUMENT` for a destination whose last
component is not `<name>.beebium-machine` with a name that can name a file on
every host, or that lies inside the machine's own directory; `NOT_FOUND` when
the destination's parent does not exist (it is not made); `ALREADY_EXISTS`
when the destination exists; `INTERNAL`, with a message, when the move fails.
A closed machine is moved with the file manager or `mv`.

```bash
grpcurl -plaintext -d '{"destinationDirpath": "/Users/me/Archive/Elite.beebium-machine"}' \
  -import-path src/service/proto -proto system.proto \
  localhost:48875 beebium.SystemService/MoveMachine
```

#### ResetBattery

Forgets the machine's battery-backed state, as taking out the battery does,
and returns the units forgotten (`resetUnits`: the machine's own, such as
`integra-b-rtc`, and its extensions', as `<instance id>/<unit>`, such as
`clock/saf3019p`). It takes effect when the machine is next started: the
running machine is unchanged, and neither a shutdown nor a Save keeps what it
holds. The command-line form is `reset-battery`.

### EconetService

Transport-agnostic Econet control and status: whether the hardware is fitted,
the station number, the ADLC and four-way-handshake state. Anything specific to
one transport lives on that transport's own API (see
[EconetTransportService](#econettransportservice) and [ExtensionRpc](#extensionrpc)).

**Proto file:** `src/service/proto/econet.proto`

| RPC | Behaviour |
|-----|-----------|
| `GetEconetStatus` | One `GetEconetStatusResponse`: `has_econet_socket`, `enabled`, `station_id` (the configured number: what the station links present now; 0 when not fitted), `station_in_force` (the number the guest is using: what it read from the links on its first read since the last reset, so a renumber shows here at the next Break; a change is pending while the two differ), `aun_mode`, `connected` (the backend's link state: socket bound and cable plugged for AUN, device responding for Piconet), `adlc`, `handshake`, the speed gate (`requires_real_time`, `gated_by_speed`), the AUN station-number collisions currently in effect (`aun_station_collision_count`, `aun_last_station_collision`; a gauge, not a running total), `station_min` and `station_max` (the station numbers a station may have, inclusive, as `EnableEconet` and `SetStationId` enforce them; given whenever `has_econet_socket`, fitted or not, so a station field needs no range of its own), and diagnostic counters. |
| `WatchEconetStatus` | Server stream of the same message: a snapshot at once, then one whenever the status changes, no more often than `min_interval_ms` (default 50). Use it instead of polling. |
| `EnableEconet` | Fit the hardware at `station_id` (`station_min`..`station_max`, 1-254). `no_network` fits it with no carrier. Otherwise, when a transport was configured at launch, that transport is brought up through its own backend, with `aun_port` (0 = 32768) written into its `port` parameter; with no transport configured, a bare AUN socket on net 0 is bound. `actual_aun_port` reports the bound port. Fails if Econet is already fitted. |
| `DisableEconet` | Remove the hardware; the transport stops announcing and browsing. |
| `SetStationId` | Change the station number (1-254) while fitted. The station register changes at once and an AUN transport re-announces and re-filters at once; the guest's network ROM reads the number at its next Break, when `station_in_force` follows and `WatchEconetStatus` pushes. |
| `SubscribeEconetEvents` | Server stream of `EconetEvent`s from now on: frames sent and received (typed, after the four-way handshake, with the AUN `handle`), handshake and connection changes. `max_batch` (default 64) and `min_interval_ms` (default 20). A gap in `sequence` means events were lost. `FAILED_PRECONDITION` when the machine has no Econet socket or Econet is not fitted. |

Errors in the unary RPCs are reported in the response's `success` / `error`
fields, not as a gRPC status.

```bash
grpcurl -plaintext -import-path src/service/proto -proto econet.proto \
  localhost:48875 beebium.EconetService/GetEconetStatus
```

### EconetTransportService

Says which Econet transport extension is loaded, and gives the instance id a
client needs for that transport's API and control panel.

**Proto file:** `src/service/proto/econet_transport.proto`

| RPC | Behaviour |
|-----|-----------|
| `ListTransports` | One `EconetTransport` per loaded transport (`--aun`, `--piconet`, or a preset's `econet.transport`): `name` (`"aun"`, `"piconet"`), `description`, `active`, `id` (the opaque per-instance id; pass it as `ExtensionRpc`'s `extension_id` and as the `ExtensionUiService` subscription key), `has_ui`. On BBC machines at most one is loaded, and it is active. |
| `GetActiveTransport` | The single active transport, or an empty response when none is configured. |

```bash
grpcurl -plaintext -import-path src/service/proto -proto econet_transport.proto \
  localhost:48875 beebium.EconetTransportService/ListTransports
```

```json
{
  "transports": [
    {
      "name": "aun",
      "description": "AUN (Acorn Universal Networking) UDP econet transport",
      "active": true,
      "id": "c62a8575-74a5-4777-8023-b636530ddae7",
      "hasUi": true
    }
  ]
}
```

### PeripheralExtensionService

Lists the peripheral extensions the running server loaded, and the machine's
attachment points. Everything a frontend shows about them comes from here -- the
points' names, where each extension is attached, each parameter's label, choices
and fields -- so a frontend needs no table of its own.

**Proto file:** `src/service/proto/peripheral_extension.proto`

| RPC | Behaviour |
|-----|-----------|
| `ListExtensions` | `extensions`: one `ExtensionInfo` per loaded instance, providers first. `attachment_points`: the machine's own connectors, then any an extension provides, in the catalogue's presentation order. |

`ExtensionInfo` carries the instance's `name` (type), `id`, `label` (the
instance's name), `display_name` (the manifest's), `description`, `has_ui`,
`config`, `storage_devices`, `attaches_to` and `provides`, and:

- `attachments`: one per `attaches_to` point, in that order, with the
  `provider_id` of the instance the extension is attached to there -- as the
  server chose it, by a `provider_of` parameter or else the last provider
  loaded -- or empty for the machine's own connector.
- `parameters`: each manifest parameter with every property
  `describe-extension --json` gives it (`label`, `is_list`, `default_value`,
  `default_from`, `effective_default`, `min`/`max`, `choices`, `choices_from`,
  `excludes`, `bundled`, `unique`, `provider_of`, `item_format`, `fields`).
- `groups`: the manifest's groups of alternative parameters.

`AttachmentPointInfo` gives `id`, `display_name`, `description`,
`min_occupancy`, `max_occupancy` (absent when unbounded) and `provided_by` (the
providing instances' ids; empty for the machine's own), the same names and
descriptions `list-attachment-points` and `describe-preset-schema` give.

```bash
grpcurl -plaintext -import-path src/service/proto -proto peripheral_extension.proto \
  localhost:48875 beebium.PeripheralExtensionService/ListExtensions
```

### ExtensionRpc

The one core-hosted service through which clients call APIs that extensions
provide. Extensions never host gRPC services; the core tunnels each call's
serialized request and response to the extension as opaque bytes, so a plugin
never links gRPC.

**Proto file:** `src/service/proto/extension_rpc.proto`

| RPC | Behaviour |
|-----|-----------|
| `Invoke` | Unary. `InvokeRequest` names `extension_id`, `service`, `method` and carries the serialized request in `payload`; the response's `payload` is the serialized reply. |
| `ServerStream` | The same request, answered by a stream of `InvokeResponse`s. |

Routing: a non-empty `extension_id` selects that instance, whether a peripheral
extension's id (from `PeripheralExtensionService`) or a transport's id (from
`EconetTransportService`). An empty `extension_id` routes by `service` when
exactly one loaded instance offers it, and is `FAILED_PRECONDITION` listing the
candidate ids when several do. An unknown id, or no instance offering the
service, is `NOT_FOUND`; an unknown method is `UNIMPLEMENTED`. A non-OK reply
carries its error in the gRPC status with an empty payload. The request's
`metadata` map is not passed to the extension.

The AUN transport registers `AunService` (`src/extensions/aun/aun.proto`), reachable only when AUN is the loaded transport:

| Method | Behaviour |
|--------|-----------|
| `GetStatus` | `connected`, `local_port` (0 until the socket is up), `peer_count`, `map_file_path` (on the server's host; empty with `map-file=none`), `map_file_entry_count`, `map_file_error`, `discovery_mode` (`on`, `announce`, `browse`, `off`). |
| `SetConnected` | Plug or unplug the simulated cable (DCD). Accepted before the socket exists and applied when it comes up; `error` then says so. |
| `AddPeer`, `RemovePeer` | Add or remove a peer with provenance Api (net 0-255, stn 1-254, IPv4 `ip_address`, `port` 0 = 32768). Work with no socket up; the peer is applied when one is. |
| `ListPeers` | The resolved routing table, each entry with the `source` it won from: Api > Launch > MapFile > Discovered > Subnet. |
| `ReloadMap` | Re-read the map file now (`reloaded` is false only when the map file is disabled). |
| `AddMapPeer`, `RemoveMapPeer`, `AddMapSubnet`, `RemoveMapSubnet` | Edit the server's map file (written atomically on the server's host, then applied at once). Add is add-or-replace; Remove reports `removed`. |
| `ListMap` | The map file's entries as written, with labels and host resolution, as distinct from `ListPeers`. |

The Piconet plugin registers `PiconetService`
(`src/extensions/piconet/piconet_service.proto`) the same way. Clients normally
use the typed wrappers (`bbc.transport[Aun]` in Python, `Aun` in TypeScript);
with `grpcurl`, the payload is base64 protobuf:

```bash
grpcurl -plaintext -import-path src/service/proto -proto extension_rpc.proto \
  -d '{"service": "AunService", "method": "GetStatus"}' \
  localhost:48875 beebium.ExtensionRpc/Invoke
```

See [networking.md](networking.md) for the AUN peer table, map file and
discovery, and [howto_write_an_econet_transport.md](howto_write_an_econet_transport.md)
for registering a transport's dispatcher.

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

Every service has C++ gRPC tests under `tests/` (`test_grpc_*.cpp`), which start a server in-process and exercise it as a client. The Python and TypeScript clients have their own suites, which run against the built servers. After building (see [building.md](building.md)):

```bash
ctest --test-dir build --output-on-failure                 # C++
(cd clients/beebium-python-client && uv run pytest)        # Python client
(cd clients/beebium-typescript-client && npm test)         # TypeScript client
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
