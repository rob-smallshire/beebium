# Tube Debugging Cookbook

Techniques developed during the CE2023 investigation that are reusable for
diagnosing Tube co-processor bugs. These are general-purpose patterns, not
specific to any one game.

## Snapshot-and-Replay Testing

**Problem**: A Tube bug manifests deep into a game's boot sequence. The full
boot takes 30 seconds of emulated time, making iteration slow.

**Technique**: Capture a memory snapshot at a known point (e.g. decompressor
entry), then replay just the relevant code with synthetic I/O. This reduces
a 30-second boot to sub-second execution.

**How to implement**:

1. Run the full boot in the interleaved trace test until the parasite
   reaches the target PC.
2. Dump the full 64K parasite RAM and CPU register state.
3. Create a standalone test that loads the snapshot, sets up a minimal
   `TubeShared` + `TubeHostPort` + `TubeParasitePort` + `ParasiteMemoryMap`
   + `ParasiteCpu`, loads the RAM, sets the CPU registers, and runs.
4. Feed I/O data (e.g. R1 bytes) synthetically via `TubeHostPort::host_write()`.

**Key details**:
- The stub ROM only needs correct reset/NMI/IRQ vectors matching the snapshot.
- Disable boot ROM with `memory.read(0xFEF8)` before loading the snapshot.
- Set all CPU registers including `opcode_pc`, `pc`, `s.b.h` (stack page), `dbus`.

## Eager I/O Feeding

**Problem**: A timing-dependent bug needs to be made deterministic for
analysis.

**Technique**: Feed I/O data as fast as the consumer drains it, ensuring the
latch is always full. This eliminates timing as a variable: if the bug
reproduces, it's not a race condition.

**Pattern**:

```cpp
auto feed_r1 = [&]() {
    while (shared.r1_h2p.ready.load(std::memory_order_acquire) == 0
           && idx < data.size()) {
        host.host_write(1, data[idx++]);
        return;  // Feed one byte at a time
    }
};

feed_r1();  // Prime the latch
for (int i = 0; i < max_instructions; ++i) {
    cpu.step_instruction();
    feed_r1();  // Refill after each instruction
}
```

Note: eager feeding may produce different results from real-time execution if
the code under test is sensitive to whether the latch is full during
intermediate bus cycles (e.g. page-cross fixup reads).

## Watchpoint Trace Analysis

**Problem**: Need to find spurious reads or writes to specific I/O addresses
among millions of instructions.

**Technique**: Use `ParasiteCpu`'s watchpoint mechanism to record
pseudo-entries in the instruction trace when specific addresses are accessed.
Then post-process the trace to identify unexpected callers.

**Setup**:

```cpp
cpu.trace().resize(16 * 1024 * 1024);
cpu.trace().set_enabled(true);
cpu.set_watch_read_addr(0xFEF9);   // Watch R1 data reads
cpu.set_watch_write_addr(0x0CE6);  // Watch writes to a specific address
```

**Trace entries**: Watchpoint hits are recorded with pseudo-opcodes:
- `opcode = 0xFE`: read watchpoint hit. `pc` = instruction that caused the
  read. `a` = value read. `cycle` field stores `ad.w` (base address register).
- `opcode = 0xFF`: write watchpoint hit. `pc` = target address. `a` = value
  written.

**Post-processing**:

```cpp
for (size_t i = 0; i < trace.available(); ++i) {
    auto& e = trace[i];
    if (e.opcode == 0xFE && e.pc != expected_caller) {
        // Spurious read -- not from the legitimate instruction
    }
}
```

Context around each hit can be found by scanning backwards in the trace for
the preceding instructions (which have normal opcodes).

## Interleaved Execution Testing

**Problem**: A bug may depend on the relative timing of host and parasite
execution, but multi-process testing is hard to instrument.

**Technique**: Run both host and parasite on the same thread with
configurable batch sizes. This eliminates concurrency while preserving the
interleaving that triggers timing-dependent bugs.

**Pattern**:

```cpp
bool run_until_hang(int host_batch, int parasite_batch, int max_rounds) {
    for (int round = 0; round < max_rounds; ++round) {
        for (int i = 0; i < host_batch; ++i)
            machine.step();
        for (int i = 0; i < parasite_batch; ++i)
            parasite->step_instruction();

        // Check for hang
        if (parasite->pc() == expected_hang_addr)
            return true;
    }
    return false;
}
```

**Useful ratios**:
- **1:1** -- tightest coupling, similar to jsbeeb/B-Em. If a bug reproduces
  here, it's not a concurrency issue.
- **2:3** -- matches real hardware clock ratio (2 MHz host : 3 MHz parasite).
- **100:1** -- host far ahead. Useful for checking whether the bug depends
  on the host having written I/O data before the parasite reads it.

## Full-Boot Integration Testing

**Problem**: Need to verify that a game actually boots and reaches a specific
screen, not just that individual subsystems work.

**Technique**: Boot the full machine (host + parasite) with real ROMs and a
disc image, inject keyboard input, and detect progress by polling the
parasite PC or checking host screen memory.

**Key components**:
- `TestFixture`: sets up `ModelB` machine + `ParasiteRunner` with ROMs,
  disc image, Tube shared memory. Uses `set_auto_boot(true)` for `*EXEC
  !BOOT`.
- `run_until_hang()`: interleaved stepping with configurable batch sizes and
  hang detection (polls parasite PC for known hang addresses).
- `FrameRenderer`: processes video output to keep the host's frame counter
  advancing (required for correct VIA timing).

**Hang detection**: Poll the parasite PC periodically. If it stays at a
known poll-loop address (e.g. `BPL $09D1` for R1 status polling) for
many consecutive checks, it's hung.

**Success detection**: Check whether the parasite PC moves past the expected
completion point (e.g. decompressor exit at `$0816`), or whether the host
screen contains expected text (e.g. "Loading").

## Cross-Emulator Differential Testing

**Problem**: Beebium diverges from correct behaviour, but the divergence
point is unknown among millions of instructions.

**Technique**: Run the same workload on jsbeeb (as an oracle) and Beebium,
capture matching trace formats, and compare.

**jsbeeb oracle setup** (TypeScript, in `oracle/`):

```typescript
const oracle = new JsbeebOracle();
await oracle.initialize('B1770', { tube: true });
await oracle.loadDisc(0, discPath);
oracle.reset();
await oracle.runCycles(16_000_000);   // Boot to BASIC
await oracle.type("*EXEC !BOOT\r");
await oracle.runUntilParasiteAddress(targetPC, timeoutSec);
```

**Instruction trace capture** (jsbeeb):

```typescript
parasiteCpu._debugInstruction = (pc) => {
    trace.push({ pc, a: parasiteCpu.a, x: parasiteCpu.x, ... });
    return trace.length >= limit;
};
processor.execute(hostCycles);
```

**Binary trace format**: 8 bytes per entry (PC:u16le, A:u8, X:u8, Y:u8,
SP:u8, P:u8, opcode:u8). Same format for both jsbeeb and Beebium traces,
enabling byte-for-byte comparison.

**P register masking**: Mask bits 4-5 (`& 0xCF`) when comparing P registers,
as the B and unused flags may differ between implementations without
indicating a real divergence.

**Know where the oracle premise stops holding.** jsbeeb is a *source of truth*
only where the hardware defines the behaviour in question. Where it does not,
each emulator has picked a defensible value and a divergence proves nothing
about either. The worked example is the 6845 clock rate at reset: jsbeeb runs it
at 2MHz, Beebium at 1MHz, and SHEILA &20 turns out to be write-only with **no
reset value defined in any hardware manual** -- so the state is indeterminate on
real hardware, and the four emulators surveyed split two-two because there is
nothing to be right about. Hours went into that divergence before the manuals
settled that it was not a defect.

Before treating a divergence as a Beebium bug, ask what the hardware
documentation says. If it is silent, the disagreement is undefined behaviour
and the only question worth asking is whether real software can reach the state
(usually it cannot -- MOS initialises the hardware first). This is also why the
harness compares cycle counts but never asserts on them.

The corollary is where to point it: differential testing pays off on questions
the hardware *does* define -- was this byte delivered through this register? --
and misleads on initial state and cycle-exact timing. Its real catch was a Tube
data-loss bug, a data-path question.

See `oracle/README.md` for the harness's capabilities and current state, and
`oracle/CYCLE_DIFFERENCE_INVESTIGATION.md` for the full analysis.

## Register Trace Analysis

**Problem**: Transfer counter totals match, but the data stream is corrupted.
Need to see per-byte read/write values at the Tube register level.

**Technique**: Use `TubeParasitePort` and `TubeHostPort` register traces.

```cpp
parasite->tube_port().register_trace().resize(1 * 1024 * 1024);
parasite->tube_port().register_trace().set_enabled(true);
host_port->register_trace().resize(1 * 1024 * 1024);
host_port->register_trace().set_enabled(true);
```

Each entry records: register offset, direction (read/write), value, and
cycle count. Filter by offset and direction to isolate specific register
traffic (e.g. R1 H->P data reads only).

## Transfer Counter Verification

**Problem**: Suspecting data loss in the Tube FIFO -- bytes written but
never read, or vice versa.

**Technique**: `TubeShared::counters` provides atomic per-register
write/read counts. After a transfer completes, check that writes equal
reads for each register direction.

```cpp
printf("R1 H2P: w=%llu r=%llu\n",
    shared.counters.r1_h2p_writes.load(),
    shared.counters.r1_h2p_reads.load());
```

If writes and reads match but the data is still wrong, the corruption is
in the values (not lost bytes) -- look at the register trace for the actual
byte values.
