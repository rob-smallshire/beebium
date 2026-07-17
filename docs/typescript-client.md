# Beebium TypeScript Client

TypeScript/Node.js client for the Beebium BBC Micro emulator. Provides a
typed async interface for controlling emulator instances via gRPC, suitable
for automated testing, tooling, and scripting.

## Architecture

### How Beebium works

Beebium separates the emulation core from frontends. The core runs as a
headless server process (`beebium-model-b`, `beebium-model-b-plus`, etc.)
that exposes its functionality through gRPC services. Clients connect over
the network or localhost and interact with the emulator through well-defined
protocol buffer messages.

```
  beebium-model-b (server process)
       |
       +-- gRPC services on localhost:PORT
       |
       +--- TypeScript client (this library)
       +--- Python client
       +--- macOS GUI (Swift/Metal)
```

The TypeScript client is one of several frontends. It can either launch and
manage a server process itself, or connect to an already-running server
started by another means (e.g. the macOS GUI, the command line, or another
client).

### Client structure

The client is organised as a `Beebium` facade class with lazy subsystem
accessors, each wrapping one or more gRPC service stubs:

```
Beebium (facade)
 |
 +-- .debugger     Debugger    -- execution control, stepping, breakpoints
 +-- .cpu          CPU         -- 6502 register read/write
 +-- .memory       Memory      -- address space and region-based access
 +-- .keyboard     Keyboard    -- text input, matrix keys, break, links
 +-- .video        Video       -- frame capture and streaming
 +-- .system       System      -- identity, provenance, status, shutdown
 +-- .disc         Disc        -- floppy drive management
 +-- .econet       Econet      -- Econet/AUN networking
 +-- .tube         Tube        -- Tube coprocessor management
 +-- .systemVia    Via         -- System VIA 6522 state
 +-- .userVia      Via         -- User VIA 6522 state
 +-- .crtc         Crtc        -- MC6845 CRTC state
 +-- .videoUla     VideoUla    -- Video ULA state
 +-- .addressableLatch  AddressableLatch  -- IC32 latch state
 +-- .sound        Sound       -- SN76489 sound chip state
 +-- .tubeUla      TubeUlaInspection  -- Tube ULA register state
 +-- .basic        Basic       -- BBC BASIC workflow helpers
```

Each subsystem is created on first access and reuses its gRPC stub for the
lifetime of the connection.

### Source layout

```
clients/beebium-typescript-client/
  package.json              NPM package definition
  tsconfig.json             TypeScript 5+ strict mode, ESM
  vitest.config.ts          Test runner configuration
  scripts/
    generate-protos.sh      Regenerate proto stubs from .proto files
  src/
    index.ts                Barrel exports
    client.ts               Beebium facade (connect/launch, subsystem getters)
    connection.ts           gRPC channel and lazy service stubs
    server-process.ts       Subprocess lifecycle management
    exceptions.ts           Error class hierarchy
    call-utils.ts           Promisification of callback-based gRPC calls
    stream-utils.ts         AsyncIterable wrapper for server-streaming RPCs

    debugger.ts             Execution control, stepping, breakpoints
    cpu.ts                  Register read/write, flag accessors
    memory.ts               Address space, regions, bus/peek, PC-context
    keyboard.ts             Text input, matrix keys, break, links
    keyboard-map.ts         BBC keyboard matrix mapping table
    system.ts               Identity, provenance, status, shutdown
    video.ts                Frame capture, streaming
    disc.ts                 Floppy drives, insert/eject, events
    econet.ts               Econet hardware, AUN peers
    tube.ts                 Tube coprocessor status
    via.ts                  System/User VIA 6522 state
    crtc.ts                 MC6845 CRTC registers, timing state
    video-ula.ts            Video ULA control, palette
    latch.ts                Addressable latch (IC32) state
    sound.ts                SN76489 channels
    tube-ula.ts             Tube ULA registers, FIFOs, interrupts
    basic.ts                BBC BASIC workflow helpers
    screen.ts               Mode 7 screen reading
    strings.ts              C-str, Pascal-str, padded-str encoding

    generated/              ts-proto output (gitignored, regenerated)
  tests/
    *.test.ts               Unit, mock, and integration tests
```

### gRPC service mapping

The server exposes these gRPC services. The client wraps them into the
subsystem classes listed above:

| gRPC Service       | Proto File     | Client Subsystems                     |
|--------------------|----------------|---------------------------------------|
| DebuggerControl    | debugger.proto | Debugger, CPU, Memory                 |
| DeviceInspection   | debugger.proto | Via, Crtc, VideoUla, Latch, Sound, TubeUla |
| SystemService      | system.proto   | System                                |
| KeyboardService    | keyboard.proto | Keyboard                              |
| VideoService       | video.proto    | Video                                 |
| DiscService        | disc.proto     | Disc, Drive                           |
| EconetService      | econet.proto   | Econet                                |
| TubeService        | tube.proto     | Tube                                  |

## Requirements

- **Node.js 18+** (ESM support required)
- **protoc** (Protocol Buffers compiler) -- needed to regenerate proto stubs
- A built **beebium server executable** (for integration tests and launch mode)
- **ROM files** (the server needs MOS and BASIC ROMs to boot)

## Setup

```bash
cd clients/beebium-typescript-client
npm install
npm run generate-protos   # requires protoc on PATH
npm run build             # TypeScript compilation (optional -- vitest handles .ts directly)
```

### Environment variables

| Variable | Purpose |
|----------|---------|
| `BEEBIUM_SERVER` | Path to the server executable. Used by `ServerProcess` when no explicit path is given. Falls back to the default build directory. |
| `BEEBIUM_ROM_DIR` | ROM directory path. Passed through to the server process via environment inheritance. |

## Connecting to a running server

If a beebium server is already running (e.g. started from the command line
or by the macOS GUI), connect to it by target address:

```typescript
import { Beebium } from "@beebium/client";

// Connect to the default port (0xBEEB = 48875)
const bbc = await Beebium.connect();

// Connect to a specific address
const bbc = await Beebium.connect("localhost:12345");

// With a custom timeout (milliseconds)
const bbc = await Beebium.connect("localhost:48875", 10000);

try {
    const identity = await bbc.system.getIdentity();
    console.log(`Connected to ${identity.modelName} (${identity.uuid})`);
} finally {
    await bbc.close();
}
```

`connect()` waits for the gRPC channel to be ready before returning. If the
server is not reachable within the timeout, a `TimeoutError` is thrown.

## Launching a new server

The client can spawn and manage a server process. The server is
automatically stopped when the client is closed.

```typescript
import { Beebium } from "@beebium/client";

const bbc = await Beebium.launch({ model: "B" });

try {
    // Server is running, connection is established
    console.log(`Server at ${bbc.target}`);
    // ... work with the emulator ...
} finally {
    await bbc.close();  // stops the server process
}
```

`launch()` allocates a free port (using `--port 0`), starts the server with
`--wait=api` (machine paused until a client sends a command), and connects.

### Launch options

```typescript
const bbc = await Beebium.launch({
    model: "B",                          // "B", "B+", or "B-RomRam"
    executableFilepath: "/path/to/beebium-model-b",  // explicit path
    args: ["--fdc", "acorn-1770"],       // extra server CLI arguments
    timeout: 10000,                      // server startup timeout (ms)
    timeoutMs: 5000,                     // gRPC connection timeout (ms)
    provenanceType: "my-test-harness",   // identifies the launcher
    provenanceVersion: "1.0.0",
});
```

If `executableFilepath` is not specified, `ServerProcess` checks:
1. The `BEEBIUM_SERVER` environment variable
2. A default build directory (`build/src/server/`)

### Async dispose

If your runtime supports `Symbol.asyncDispose` (Node.js 20+ with
`--harmony-using` or Node.js 22+), you can use `await using`:

```typescript
await using bbc = await Beebium.launch({ model: "B" });
// bbc.close() is called automatically when the block exits
```

## Debugger control

The server starts with the machine paused when `--wait=api` is used (the
default for `launch()`). Use the debugger to control execution:

```typescript
// Query execution state
const state = await bbc.debugger.getState();
console.log(`Running: ${state.isRunning}, Cycles: ${state.cycleCount}`);

// Execution control
await bbc.debugger.run();        // resume free-running execution
await bbc.debugger.stop();       // pause
await bbc.debugger.reset();      // reset the machine (stays paused)

// Stepping (machine must be stopped)
const result = await bbc.debugger.step(100);          // step 100 instructions
console.log(`Executed ${result.cyclesExecuted} cycles`);

const result2 = await bbc.debugger.stepCycles(50);    // step 50 CPU cycles

// Convenience queries
if (await bbc.debugger.isRunning()) { ... }
if (await bbc.debugger.isStopped()) { ... }
```

### Breakpoints

```typescript
// Add a breakpoint, get its ID
const bpId = await bbc.debugger.addBreakpoint(0xC000);

// Run until a breakpoint fires (sets temp breakpoint, runs, waits, cleans up)
const state = await bbc.debugger.runUntil(0xC000);

// List, remove, clear
const breakpoints = await bbc.debugger.listBreakpoints();
await bbc.debugger.removeBreakpoint(bpId);
await bbc.debugger.clearBreakpoints();
```

## CPU registers

```typescript
// Read all registers at once
const regs = await bbc.cpu.getRegisters();
console.log(`A=${regs.a.toString(16)} X=${regs.x.toString(16)} PC=${regs.pc.toString(16)}`);

// Read individual registers
const pc = await bbc.cpu.getPc();
const a = await bbc.cpu.getA();

// Write registers
await bbc.cpu.setPc(0x0400);
await bbc.cpu.setA(0x42);
await bbc.cpu.setRegisters({ a: 0x11, x: 0x22, y: 0x33 });

// Flag accessors (pure functions operating on the P register value)
import { carry, zero, negative } from "@beebium/client";
if (carry(regs.p)) console.log("Carry set");
if (zero(regs.p))  console.log("Zero set");
```

The `Registers` interface also includes interrupt handler tracking fields:
`inNmiHandler`, `inIrqHandler`, `nmiPending`, `irqPending`,
`deviceIrqFlags`, `deviceNmiFlags`.

## Memory access

Memory access is explicit about side effects. The `bus` accessor reads and
writes through the memory bus (like the real 6502), which may trigger
hardware side effects on I/O addresses. The `peek` accessor reads without
side effects, safe for inspecting I/O registers.

### Address space (16-bit flat)

```typescript
// Read without side effects (peek)
const byte = await bbc.memory.address.peek.readByte(0xFE4D);
const data = await bbc.memory.address.peek.read(0x8000, 256);

// Read/write through the bus (may trigger hardware side effects)
const val = await bbc.memory.address.bus.readByte(0x1000);
await bbc.memory.address.bus.writeByte(0x1000, 0x42);
await bbc.memory.address.bus.write(0x2000, Buffer.from([0xA9, 0x42, 0x60]));

// File I/O
await bbc.memory.address.bus.load(0x1900, "/path/to/program.bin");
await bbc.memory.address.peek.save(0x1900, 0x1000, "/path/to/dump.bin");

// Fill a range
await bbc.memory.address.bus.fill(0x1000, 0x2000, 0x00);
```

### PC-context access (Model B+ shadow RAM)

On the BBC Model B+, memory at 0x3000-0x7FFF is routed differently
depending on the executing code's location. Use `withPc()` to simulate
access from a given program counter:

```typescript
// What MOS code (PC=0xD000) would see at 0x5000
const mosView = await bbc.memory.address.peek.withPc(0xD000).readByte(0x5000);

// What user code (PC=0x1000) would see at the same address
const userView = await bbc.memory.address.peek.withPc(0x1000).readByte(0x5000);
```

On Model B (no shadow RAM), `withPc()` has no effect.

### Named regions

Access memory regions directly, bypassing bank switching:

```typescript
// Read from main RAM
const byte = await bbc.memory.region("main_ram").peek.readByte(0x1234);

// Read from a sideways ROM bank (even if not currently selected)
const data = await bbc.memory.region("bank_4").peek.read(0x8000, 16384);

// Write to a region
await bbc.memory.region("main_ram").bus.writeByte(0x0400, 0x42);

// Discover available regions
const regions = await bbc.memory.getRegions();
for (const r of regions) {
    console.log(`${r.name}: base=0x${r.baseAddress.toString(16)}, size=${r.size}`);
}

// Get machine type
const machineType = await bbc.memory.getMachineType();
```

## Keyboard input

### Type-ahead queue

The server maintains a cycle-paced type-ahead queue. Text is enqueued
immediately and typed into the machine as it runs:

```typescript
await bbc.keyboard.type("PRINT 42\r");   // \r for RETURN
await bbc.keyboard.type("10 FOR I=1 TO 10\r20 PRINT I\r30 NEXT\rRUN\r");

// Convenience methods
await bbc.keyboard.pressReturn();
await bbc.keyboard.pressEscape();
await bbc.keyboard.pressDelete();
await bbc.keyboard.pressSpace();

// Wait for the queue to drain
await bbc.keyboard.waitForTyping();

// Check queue status
const status = await bbc.keyboard.typingStatus();
console.log(`Pending: ${status.pendingCharacters}`);

// Clear the queue
await bbc.keyboard.clearTyping();
```

### Character and matrix-level input

For precise control, press and release individual keys:

```typescript
// Character-level (handles SHIFT automatically)
await bbc.keyboard.keyDown("A");   // presses SHIFT + A
await bbc.keyboard.keyUp("A");    // releases A then SHIFT

// Matrix-level (row 0-9, column 0-9)
await bbc.keyboard.matrixDown(4, 1);  // 'A' key in the BBC matrix
await bbc.keyboard.matrixUp(4, 1);

// Modifiers
await bbc.keyboard.shiftDown();
await bbc.keyboard.shiftUp();
await bbc.keyboard.ctrlDown();
await bbc.keyboard.ctrlUp();

// Release everything
await bbc.keyboard.releaseAll();
```

### Break key and keyboard links

```typescript
// Break key (directly connected to reset circuit, not the keyboard matrix)
await bbc.keyboard.pressBreak();                // press and release
await bbc.keyboard.ctrlBreak();                 // Ctrl+Break (hard reset)

// Keyboard links (startup DIP switches)
const links = await bbc.keyboard.getLinks();    // raw 8-bit value
await bbc.keyboard.setLinks(0xF7);              // set to Mode 0

const mode = await bbc.keyboard.getStartupScreenMode();   // 0-7
await bbc.keyboard.setStartupScreenMode(0);               // boot into Mode 0
```

## Device inspection

All device state reads are side-effect-free snapshots.

### VIA (6522)

```typescript
const sysVia = await bbc.systemVia.getState();
console.log(`T1 counter: 0x${sysVia.t1c.toString(16)}`);
console.log(`T1 latch: 0x${sysVia.t1l.toString(16)}`);
console.log(`IFR: 0x${sysVia.ifr.toString(16)}, IER: 0x${sysVia.ier.toString(16)}`);

const usrVia = await bbc.userVia.getState();

// ACR/IFR flag helpers (pure functions)
import { t1Continuous, ifrT1 } from "@beebium/client";
if (t1Continuous(sysVia.acr)) console.log("T1 free-running");
if (ifrT1(sysVia.ifr)) console.log("T1 interrupt pending");
```

### CRTC (MC6845)

```typescript
const crtc = await bbc.crtc.getState();
console.log(`Registers: ${crtc.registers}`);        // 18 registers (R0-R17)
console.log(`Screen start: 0x${crtc.screenStart.toString(16)}`);
console.log(`VSync: ${crtc.inVsync}, HSync: ${crtc.inHsync}`);

// Register accessor helpers
import { htotal, vsyncWidth, isInterlaced } from "@beebium/client";
console.log(`H total: ${htotal(crtc)}`);
console.log(`V sync width: ${vsyncWidth(crtc)}`);
```

### Video ULA, Addressable Latch, Sound

```typescript
const ula = await bbc.videoUla.getState();
console.log(`Control: 0x${ula.control.toString(16)}, Palette: ${ula.palette}`);

const latch = await bbc.addressableLatch.getState();
console.log(`Caps Lock LED: ${latch.capsLockLed}`);

const sound = await bbc.sound.getState();
for (const ch of sound.channels) {
    console.log(`${ch.channelName}: vol=${ch.volume}, freq=${ch.frequencyHz}Hz`);
}
```

## System information

```typescript
// Machine identity
const identity = await bbc.system.getIdentity();
console.log(`${identity.modelName} (${identity.uuid})`);

// Launch provenance (who started this server)
const prov = await bbc.system.getProvenance();
console.log(`Launched by: ${prov.type} v${prov.version}`);

// Server status streaming
for await (const event of bbc.system.watchStatus()) {
    console.log(`Status: ${event.status} - ${event.message}`);
    if (event.status === ServerStatus.SHUTTING_DOWN) break;
}

// Shutdown
import { ShutdownMode } from "@beebium/client";
const response = await bbc.system.requestShutdown(ShutdownMode.GRACEFUL);
```

## Disc drives

```typescript
// Controller status
const status = await bbc.disc.getStatus();
if (status.hasDiscController) {
    console.log(`Controller: ${status.controllerType}`);
}

// Insert a disc image
const meta = await bbc.disc.drive(0).insert("/path/to/game.ssd");
console.log(`${meta.name}: ${meta.sides} side(s), ${meta.tracksPerSide} tracks`);

// Eject (safe eject waits for motor to stop)
await bbc.disc.drive(0).eject();

// Drive status
if (await bbc.disc.drive(0).isLoaded()) {
    console.log(`Track: ${(await bbc.disc.drive(0).getStatus()).currentTrack}`);
}

// Stream disc events
for await (const event of bbc.disc.events()) {
    console.log(`Drive ${event.drive}: ${event.type}`);
}
```

## BBC BASIC helpers

High-level functions for common BASIC workflows:

```typescript
// Wait for the BASIC prompt (polls Mode 7 screen memory)
await bbc.basic.waitForPrompt();

// Wait for specific text on screen
await bbc.basic.waitForText("BBC Computer 32K");

// Read screen text (Mode 7)
const text = await bbc.basic.readScreenText(0, 5);

// Type and execute
await bbc.basic.typeAndEnter("PRINT 42");

// Get BASIC memory pointers
const page = await bbc.basic.getPage();
const top = await bbc.basic.getTop();
const himem = await bbc.basic.getHimem();
```

## String encoding utilities

Functions for encoding and decoding strings in BBC Micro memory formats:

```typescript
import { cStr, parseCStr, pascalStr, parsePascalStr, paddedStr, parsePaddedStr } from "@beebium/client";

// Null-terminated (C-style)
await bbc.memory.address.bus.write(0x1000, cStr("HELLO"));
const name = parseCStr(await bbc.memory.address.bus.read(0x1000, 256));

// Length-prefixed (Pascal-style, max 255 bytes)
await bbc.memory.address.bus.write(0x2000, pascalStr("WORLD"));
const msg = parsePascalStr(await bbc.memory.address.bus.read(0x2000, 256));

// Fixed-width padded
await bbc.memory.address.bus.write(0x3000, paddedStr("TEST", 8));
const field = parsePaddedStr(await bbc.memory.address.bus.read(0x3000, 8));
```

## Error handling

All errors extend `BeebiumError`:

| Error Class | When thrown |
|------------|------------|
| `ConnectionError` | gRPC connection failure or call error |
| `TimeoutError` | Connection or operation timeout |
| `ServerStartupError` | Server process failed to start |
| `ServerNotFoundError` | Server executable not found |
| `DebuggerError` | Debugger operation failed (e.g. step while running) |
| `MemoryAccessError` | Memory write failed |
| `DiscError` | Disc operation failed |
| `EconetError` | Econet operation failed |

```typescript
import { BeebiumError, TimeoutError } from "@beebium/client";

try {
    const bbc = await Beebium.connect("localhost:99999", 2000);
} catch (e) {
    if (e instanceof TimeoutError) {
        console.log("Server not reachable");
    }
}
```

## Testing

```bash
npm run test:unit          # unit + mock tests only (no server needed)
npm run test:integration   # integration tests (needs server + ROMs)
npm test                   # all tests
```

Integration tests use `BEEBIUM_SERVER` and `BEEBIUM_ROM_DIR` environment
variables to locate the server executable and ROM files. Each integration
test launches a fresh server instance so there is no shared emulator state
between tests.

## Relationship to the Python client

The TypeScript client mirrors the Python client's API surface. The same
subsystem names, method signatures, and data structures are used, adapted to
TypeScript idioms (async/await instead of synchronous calls, interfaces
instead of dataclasses, `Buffer` instead of `bytes`). Code written against
one client can be straightforwardly translated to the other.

Key differences from the Python client:
- All gRPC calls are async (`await` required)
- Memory accessors use explicit method calls (`readByte`, `writeByte`)
  rather than Python's subscript notation (`bus[addr]`)
- Flag accessors are standalone functions rather than properties on state
  objects
- Server streaming uses `AsyncIterable` (consumed with `for await`)
- No pytest integration; uses vitest for testing

## Relationship to the oracle client

The `oracle/` directory contains an earlier, partial TypeScript client used
for differential testing against jsbeeb. The oracle client covers only
DebuggerControl and SystemService, and includes jsbeeb-specific
normalization (PC-1 adjustment, VIA timer doubling) that is not appropriate
for a general-purpose client. The `clients/beebium-typescript-client/` client is a clean,
complete replacement that should be used for all new TypeScript work.
