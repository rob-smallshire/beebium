# Tube Architecture Evolution

This document traces the evolution of Beebium's Tube coprocessor implementation
from its original dual-process shared memory design, through an in-process
multi-threaded design with lock-free atomics, to the current single-threaded
lockstep model. It is intended as a reference for any future attempt to revisit
the more ambitious architectures, preserving the commit hashes, design
rationale, failure modes, and -- critically -- the correctness fixes discovered
during the single-threaded work that may well have made the earlier designs
viable had they been known at the time.


## Background

The Tube is Acorn's bidirectional FIFO interface connecting a BBC Micro host
processor to a coprocessor (the "parasite"). Four register pairs at &FEE0-&FEEF
provide data and status channels, with interrupt generation for efficient
transfers. The host and parasite have independent clock domains (2 MHz and
typically 3 MHz respectively), bridged asynchronously by the Ferranti Tube ULA.

The Tube's loose coupling -- message-passing FIFOs, no shared memory -- makes it
a natural fit for process separation. This was the original design motivation.


## Timeline Overview

| Period | Architecture | Branch | Status |
|--------|-------------|--------|--------|
| 13 Mar 2026 | Dual-process, POSIX shared memory | `master` | Removed |
| 13 Mar - 6 Apr 2026 | Dual-process in production, pacing work | `master`, `smooth-pacing` | Removed |
| 6-7 Apr 2026 | In-process, multi-threaded with atomics | `tube-extension-redesign` | Removed |
| 8 Apr 2026 | Single-threaded lockstep | `single-threaded-tube` | Generalised in Phase 4 |
| Sep 2026 | Coprocessor contract: lockstep as a contract, coprocessors as plugins | `master` | **Current** |


---

## Phase 1: Dual-Process with Shared Memory (March 2026)

### Vision

Each coprocessor runs as a separate OS process. The Tube FIFOs are implemented
in POSIX shared memory with atomic SPSC (single-producer, single-consumer)
operations. Each process has its own gRPC server for independent debugging. The
architecture mirrors the real hardware's independent clock domains -- the host
emulator and parasite emulator are genuinely asynchronous, synchronised only
through the shared FIFO registers.

This was conceptually faithful to the original hardware and architecturally
elegant, extending Beebium's multi-process philosophy (headless emulator servers
with gRPC frontends) to the coprocessor subsystem.

### Architecture

```
Host Process (beebium-model-b)        Parasite Process (beebium-parasite-65c02)
    |                                      |
    Machine::step()                   ParasiteRunner::run()
    |                                      |
    TubeSocket                        TubeParasitePort
    |                                      |
    TubeHostPort                      65C02 CPU + ParasiteMemoryMap
    |                                      |
    +---------- TubeShared (POSIX shm) ---+
                  |
         Atomic SPSC FIFOs
         Lifecycle mailbox
         Cache-line aligned
```

### Key Components

**TubeUla** -- the authoritative single-threaded reference model implementing
all four Tube register pairs with correct status flag semantics, interrupt
generation (HIRQ, PIRQ, PNMI), and the control register. Used for in-process
unit testing but not for cross-process operation.

**TubeShared** -- the cross-process shared memory layout. Cache-line aligned
structures containing:
- `TubeLatch`: atomic `uint8_t data` + `bool available` + `bool full` (for R1
  H-to-P, R2 both directions, R4 both directions)
- `TubeFifo24`: 24-element atomic SPSC ring buffer with `head`, `tail`, `count`
  (for R1 P-to-H, the OSWRCH channel)
- `TubeReg3`: 2-slot FIFO with packed `atomic<uint16_t>` for count + pending
  flag (for R3 both directions, the NMI bulk transfer channel)
- Lifecycle mailbox: atomic command/ack enum for Reset, Freeze, Shutdown

**TubeHostPort** -- host-side adapter implementing `TubeHostInterface`. Reads
and writes to the shared memory region. Derives status flags from atomic field
state.

**TubeParasitePort** -- parasite-side adapter. Same shared memory, opposite
perspective.

**TubeSharedMemory** -- POSIX `shm_open`/`mmap` wrapper for allocating the
shared region.

**TubeSocket** -- host-side connector abstraction. Supported two modes:
in-process (delegates to an owned `TubeUla`) and shared memory (delegates to
`TubeHostPort`). Installed in the host memory map at &FEE0-&FEEF with HIRQ
routed to the host IRQ aggregator.

**ParasiteMemoryMap** -- 64 KB RAM + 2 KB boot ROM overlay at &F800-&FFFF. Boot
mode latch cleared on first Tube register access. Tube registers punched through
at &FEF8-&FEFF.

**ParasiteCpu** -- wraps the Rockwell 65C02 (C library) with parasite memory
map and Tube interrupt routing (PIRQ to IRQ, PNMI to NMI).

**ParasiteRunner** -- owns the parasite emulation stack (CPU, memory map, Tube
port). Runs in a loop responding to lifecycle mailbox commands. Used in the
standalone parasite executable.

**PacingClock** -- real-time pacing for independent processes. Evolved through
several iterations: fixed quantum, deficit-based PWM, adaptive sleep quantum
with PI controller. Significant cross-platform work for Windows
high-resolution sleep.

### Commit History

Foundation (13 March 2026):

| Hash | Description |
|------|-------------|
| `9aae635e96dc` | Add Tube subsystem design documentation |
| `d090397316ef` | Refactor Tube subsystem architecture: implement shared memory model |
| `505626211184` | Add Tube ULA register model with comprehensive tests (Phase 1) |
| `42e2e0c08a7e` | Fix Tube ULA status registers to match App Note 004 |
| `cd37dbbf5111` | Integrate TubeSocket into hardware policies with memory map and HIRQ routing |
| `8db4a5f96c4f` | TubeSocket open bus emulation and MOS 1.20 Tube detection tests |
| `5df531d456d6` | Add TubeHostInterface concept, TubeShared layout, and TubeHostPort (Phase 2) |
| `d109c08892ed` | Add cross-thread tests for TubeShared atomic visibility |
| `270849a5efe8` | Add TubeParasitePort: parasite-side adapter for shared memory Tube ULA |
| `fbcd85e5184d` | Add ParasiteMemoryMap with boot mode ROM overlay |
| `15b369ff5871` | Add ParasiteCpu: wire Rockwell 65C02 to parasite memory map and Tube port |
| `70e5d035b319` | Add ParasiteRunner: execution loop with lifecycle mailbox handling |
| `38f0f4e73b22` | Add parasite boot integration tests with real Tube 6502 Client ROM v1.10 |
| `5841a5dd87b5` | Add end-to-end tests for host/parasite Tube communication |
| `65a57785264f` | Add shared memory mode to TubeSocket (Phase 2) |

Standalone parasite executable and integration (14-15 March 2026):

| Hash | Description |
|------|-------------|
| `479e3ed2994e` | Add Tube parasite executable and boot screen integration tests |
| `97a13c443dcf` | Include Tube parasite executable in CI artifacts |
| `9f5c4042710a` | Fix Tube parasite executable discovery on Windows |

CE2023 investigation and cross-process stress testing (18 March 2026):

| Hash | Description |
|------|-------------|
| `0ac5c604730` | CE2023: output divergence is non-deterministic concurrency race |
| `cb5cc0c94e92` | Add R1 H-to-P cross-thread stress tests |
| `dc73fc771b24` | Add cross-process R1 H-to-P tests via fork and POSIX shared memory |
| `b34982b9b7cb` | Add multi-level cross-process R1 transfer tests |
| `56677f35a04a` | Document CE2023 root cause: 6502 multi-cycle TOCTOU in R1 latch read |
| `a45a2f0a841f` | CE2023: standalone test confirms pure 65C02 emulation bug |

Pacing system evolution (March-April 2026):

| Hash | Description |
|------|-------------|
| `a35192d` | Switch to deficit-based PWM pacing: vary cycles, not sleep |
| `7940c75` | Exclude execution time from deficit calculation |
| `1d3a628599ac` | Rewrite pacing with adaptive sleep quantum and deficit controller |
| `2c33064` | Update pacing documentation with final solution |
| `93fa52b` | Always run at least 1 cycle per tick to prevent zero-cycle stalls |
| `50b771afc22f` | Add PlatformSleep abstraction for high-resolution sleep on Windows |
| `89fca35b05bc` | Fix Windows pacing by calibrating sleep quantum correctly |
| `5264173dd3b1` | Add I/O-pending flag to skip pacing sleep during Tube handshakes |

### What Worked

- Clean architectural separation between host and parasite
- Independent gRPC debuggers for each processor
- R1, R2, and R4 register protocols functioned correctly for simple operations
- CE2023 boot sequence (once the page-cross bug was understood)
- Cross-thread and cross-process SPSC correctness validated by stress tests
- The pacing system was refined to work well for general emulation

### What Did Not Work

**R3 NMI transfer byte-doubling.** The fundamental problem: R3 bulk transfers
via NMI require ~10-26 us per byte. The PacingClock operates with ~1 ms quanta
-- three orders of magnitude too coarse. During a SAVE operation, the host
process reads R3 faster than the parasite process can write, because OS thread
scheduling determines who runs when. When the host reads an empty FIFO, it gets
the stale latch value, inserting duplicate bytes.

### The Five Failed R3 Fix Attempts

These are documented in detail in `docs/discussion/tube-r3-pacing-investigation.md`.
Branch: `l3fs-econet-and-tube-r3-fix` (preserved as
`abandoned-l3fs-econet-and-tube-r3-fix`).

**Attempt 1: Pending flag (separate atomic).** Added `atomic<uint8_t> pending`
to `TubeReg3`, set by producer at threshold, cleared by consumer at zero.
Failed: race condition between `count.fetch_add` and `pending.store` -- two
separate atomics cannot be updated atomically together.

**Attempt 2: Deferred count decrement (read_phase tracking).** Track consumer
read phase (0 or 1), decrement count by 2 when phase wraps. Partially worked
(first ~9 bytes correct) but read_phase got out of sync with the V-flag
protocol state machine. Also caused CE2023 boot hangs.

**Attempt 3: Sticky data_available flag.** Added `atomic<uint8_t>
data_available` matching reference emulators' pattern exactly -- set by producer
at threshold, cleared by consumer at zero. Status register semantics became
correct, but SAVE still failed because of the timing gap (the status flags were
now right, but the data still wasn't there in time).

**Attempt 4: io_pending wakeup.** Set `io_pending_parasite` after host reads
R3 P-to-H data, waking the other process's PacingClock. PacingClock polled
every 100 us -- still too slow. When set unconditionally, it prevented the other
process from sleeping, causing 99.9% CPU.

**Attempt 5: Sleep-wait in dequeue.** When P-to-H FIFO is empty and M flag set,
spin with yield or sleep. All variants caused unacceptable pacing regression
because the M flag stays set between transfers -- the MOS idle polling loop hits
the sleep path thousands of times per second.

**Key insight from the five attempts:** The cross-process timing gap during R3
NMI transfers is fundamental to the dual-process model. Reference emulators
(B-Em, BeebEm, B2) avoid the problem entirely because both CPUs share a thread
with deterministic interleaving.

### Files Removed in Phase 2

The following files were removed when the cross-process infrastructure was
deleted (commit `21801d299e71`):

- `src/core/include/beebium/tube/TubeShared.hpp`
- `src/core/include/beebium/tube/TubeSharedMemory.hpp`
- `src/core/include/beebium/tube/TubeHostPort.hpp`
- `src/core/include/beebium/tube/TubeParasitePort.hpp`
- `src/core/src/TubeHostPort.cpp`
- `src/core/src/TubeParasitePort.cpp`
- `src/core/src/TubeSharedMemory.cpp`
- Standalone parasite server executable (`src/server/main_parasite_65c02.cpp`)
- Test files: `test_tube_cross_process.cpp`, `test_grpc_tube.cpp`,
  `test_parasite_grpc.cpp`, `test_tube_shared.cpp`,
  `test_tube_shared_memory.cpp`, `test_tube_shared_threads.cpp`

All removed code is recoverable from the commits listed above or from the
`tube-extension-redesign` branch prior to `21801d2`.


---

## Phase 2: In-Process Multi-Threaded (6-7 April 2026)

### Motivation

The dual-process R3 timing gap was judged unfixable after five attempts. The
redesign document (`docs/tube-architecture-redesign.md`, commit `6aedf51`) laid
out the case for moving to a single-process, dual-threaded architecture where
the coprocessor runs as a Peripheral Extension plugin. The extension owns the
bridging hardware (Tube ULA), the parasite CPU, memory map, boot ROM, and its
own execution thread.

Design principles from the redesign document:

1. The host sees only the Tube connector -- eight bytes at &FEE0-&FEEF
2. The bridging hardware belongs to the extension (Acorn used the Tube ULA;
   third parties used back-to-back VIAs, VIA+PIA, etc.)
3. The extension owns its thread -- host and parasite are asynchronous
4. `TubeHostBackend` is the host-facing contract
5. Correctness first, then performance

### Architecture

```
Machine<Hardware>  (host thread)
    |
    +-- TubeSocket  (&FEE0-&FEEF, IrqBinding bit 2)
    |       |
    |       +-- TubeHostBackend*  (owned by extension)
    |
    +-- ExtensionRegistry
            |
            +-- SecondProcessor65C02Extension  (Peripheral Extension)
                    |
                    +-- owns TubeUla  (thread-safe, lock-free)
                    +-- owns ParasiteRunner  (CPU, memory map, boot ROM)
                    +-- owns std::thread  (parasite execution)
                    +-- owns PacingClock  (3 MHz parasite pacing)
                    +-- provides gRPC DebuggerService
                    +-- provides gRPC TubeService
```

The extension `attaches_to("tube")` and receives a `TubeSocket` reference via
`ExtensionContext`. It installs its `TubeUla` (as `TubeHostBackend*`) into the
socket. The host memory map and IRQ routing work unchanged.

### Sub-Phases

#### 2a. Thread-Safe TubeUla with Mutex

Commit `b3bb0843ff3b` -- the first step was making the single-threaded
`TubeUla` safe for concurrent access. A single `std::mutex` protects all
register state. Every public method (`host_read`, `host_write`, `parasite_read`,
`parasite_write`) acquires the lock. Interrupt outputs (`hirq_`, `pirq_`,
`pnmi_level_`) cached as `std::atomic<bool>` so polling doesn't require the
lock.

Three concurrent access tests were added: R2 handshake, R1 FIFO stress, R3
NMI-style transfer.

Rationale for starting with a mutex: Tube register access is not on the hot
path. Even at R3 NMI transfer rates (~100,000 accesses/second), a mutex
acquisition every 10 us is negligible. The hot path is the CPU instruction loop,
which does not touch the mutex.

#### 2b. Bus Stretching: Spin-Wait with Yield

Commit `0fcf5214318f` -- replaced the single-threaded deferred-write mechanism
with a spin-wait. When the host thread writes to a full register, it releases
the mutex, yields, re-acquires, and checks again. The extension's thread drains
the register concurrently.

```cpp
host_write(offset, value):
    lock(mutex)
    if register is full:
        while register is full:
            unlock(mutex)
            yield()
            lock(mutex)
    write value
    update_interrupts()
    unlock(mutex)
```

#### 2c. Lock-Free Atomics

Commit `3155bb9a1c03` -- replaced the mutex with lock-free atomics for
performance (though profiling had not shown the mutex as a bottleneck). All
register fields became atomic with acquire/release ordering:

- H-to-P and P-to-H latches: `atomic<uint8_t> data`, `atomic<bool> available`,
  `atomic<bool> full`
- R1 P-to-H FIFO: atomic SPSC ring buffer with `fetch_add`/`fetch_sub` on
  count
- R3 registers: packed `atomic<uint16_t>` for count + pending, updated via
  `compare_exchange_weak` loops
- Bus stretching: spin-waits on atomic flags with `std::this_thread::yield()`

This reduced per-register-access cost from ~100 ns (mutex) to ~5-10 ns
(atomic), but introduced significant complexity -- particularly the CAS loops
for R3 state and the split interrupt update functions needed because different
threads called different sides.

#### 2d. Extension Framework Integration

| Hash | Description |
|------|-------------|
| `ae3c517b8f23` | Register "tube" extension point, add `install_backend` to TubeSocket |
| `4cc2d1c28c19` | Introduce `TubeParasiteBackend` interface for parasite-side abstraction |
| `01bdc52097783` | Add in-process tests: TubeUla-backed ParasiteRunner |
| `08cb08fa9a53` | Implement Acorn 65C02 coprocessor as Peripheral Extension |
| `ef25257e420f` | Wire extension into ServerMain via `--tube-65c02` CLI flag |

The `SecondProcessor65C02Extension` owns:
- `TubeUla` (thread-safe bridging hardware)
- `ParasiteRunner` (CPU + memory map + boot ROM)
- `std::thread` (parasite execution)
- `PacingClock` (3 MHz real-time pacing)

It loads the Acorn Tube 6502 Client ROM v1.10 (2 KB) and installs the TubeUla
as the host-side backend via `TubeSocket::install_backend()`.

#### 2e. Debugger Integration

| Hash | Description |
|------|-------------|
| `d9796b03aa02` | Add parasite DebuggerService to extension |
| `95ef69c2ae7a` | Expose parasite debugger via `ParasiteDebuggerControl` gRPC service |
| `2583e2ec5348` | Implement cross-processor debugger stop (breakpoint in one halts both) |
| `592219823d7d` | Update Python and TypeScript clients for single-server architecture |
| `8b2f69d1bcf6` | Rename `CoupledSystem` to `TubeSystem` in clients |

Both host and parasite debuggers coexist on the same gRPC server. Breakpoints
and watchpoints on either processor can trigger a coordinated stop of both via
`stop_counterpart` callbacks.

#### 2f. Cross-Process Infrastructure Removal

Commit `21801d299e71` -- 38 files changed, +155/-7671 lines. Removed all
cross-process Tube infrastructure (TubeShared, TubeHostPort, TubeParasitePort,
TubeSharedMemory, standalone parasite executable). All tests migrated to
in-process TubeUla.

#### 2g. R3 Correctness Fixes

| Hash | Description |
|------|-------------|
| `46f5a6c2953d` | Fix R3 status flag hysteresis with packed atomic state |
| `e6fc1e0e5b12` | Fix data race on `prev_pnmi_` in lock-free TubeUla |
| `ba12311e841b` | Fix R3 P-to-H transfer corruption with split interrupts |

The R3 split-interrupt fix (`ba12311`) was particularly important: it split
`update_interrupts()` into host-thread and parasite-thread versions, moved PNMI
edge detection to the parasite thread only, changed `pnmi()` to return level
(not edge), and added bus-stretch for R3 P-to-H reads when M flag is set and
FIFO is empty.

#### 2h. CE2023 Page-Cross Fix

Commit `da1125fad3e1` -- route page-cross fixup reads through `peek()` to
prevent Tube register side effects. The 65C02's `LDA ($33),Y` instruction with
a page-crossing pointer generates a dummy read at an intermediate address. If
this address falls in the Tube register space (&FEF8-&FEFF), the read consumes
data from a latch, corrupting the data stream. The fix checks
`M6502ReadType_Uninteresting` in `ParasiteCpu::tick()` and routes those reads
through `memory_.peek()` instead of `memory_.read()`.

This fix is **architecture-independent** -- it applies equally to dual-process,
multi-threaded, and single-threaded designs.

#### 2i. Merge

Commit `a583e5879634` (7 April 2026, 20:50) -- merge `tube-extension-redesign`
to master. 91 files changed, +5062/-9149.

### What Worked

- Extension framework integration was clean and natural
- TubeUla as owned-by-extension correctly separates concerns
- Debugger integration with coordinated cross-processor stops
- R1, R2, R4 register protocols
- CE2023 boot sequence (with page-cross fix)
- DFS SAVE byte-doubling was eliminated (the in-process timing was close enough
  for R3 NMI transfers)

### What Did Not Work

**Non-deterministic R2 protocol desynchronisation.** During complex sequences
involving OSWORD &72 SCSI operations followed by ADFS filing system selection
(OSBYTE &8F), the R2 command/response protocol lost synchronisation. The
parasite would send an R2 command that the host never read because the host had
already returned from the Tube Host Code main loop. Transfer counters showed 461
R2 exchanges and 16,384 R3 bytes before the deadlock, but the desync point was
non-deterministic.

The root cause was the same class of problem as Phase 1: OS thread scheduling
has no relationship to the real hardware's timing. The two threads could
interleave register accesses in ways that never occur on real hardware.

The `prev_pnmi_` data race (`e6fc1e0`) was another symptom -- the PNMI edge
detection logic was being called from both threads, and the `prev_pnmi_` field
was a plain `bool` in the lock-free implementation, creating an undetected race.

**Complexity of lock-free atomics.** The CAS loops for R3 state, the split
interrupt functions for different threads, the atomic memory ordering
annotations throughout -- all added significant cognitive overhead without
solving the fundamental timing problem. The mutex version was simpler and would
have been adequate.

### Key Files (as of merge commit a583e58)

```
src/core/include/beebium/tube/
    TubeUla.hpp           -- Lock-free atomic register model
    TubeSocket.hpp        -- Host-side connector with backend dispatch
    TubeHostBackend.hpp   -- Host-facing interface contract
    TubeParasiteBackend.hpp -- Parasite-facing interface contract
    TubeConcepts.hpp      -- C++20 concepts
    ParasiteCpu.hpp       -- 65C02 wrapper with interrupt routing
    ParasiteMemoryMap.hpp -- 64 KB RAM + boot ROM overlay
    ParasiteRunner.hpp    -- Threaded execution with lifecycle management

src/extensions/acorn-65c02-coprocessor/
    SecondProcessor65C02Extension.hpp/cpp  -- Extension plugin
    ParasiteDebuggerAdapter.hpp           -- gRPC debugger adapter
    roms/acorn-tube-6502_1_10.rom         -- Boot ROM
```


---

## Phase 3: Single-Threaded Lockstep (8 April 2026)

### Motivation

The multi-threaded R2 protocol desync was the final straw. The design document
`docs/discussion/tube-single-threaded-migration.md` diagnosed the problem: OS
thread scheduling determines the relative timing of register accesses, and this
scheduling has no relationship to real hardware timing. Every reference emulator
that works correctly (B-Em, BeebEm, B2, jsbeeb) runs both CPUs in the same
thread with deterministic interleaving.

The solution: replace dual-threaded execution with single-threaded interleaving
in `Machine::step()`. Both CPUs tick within the main emulation loop. The
parasite ticks before the host each cycle, with a fractional clock accumulator
for the 3:2 ratio (3 MHz parasite, 2 MHz host).

### Architecture

```
Machine::step()
    |
    +-- TubeSocket::tick_parasite()
    |       |
    |       +-- ParasiteCpu::tick()  [1 or 2 times per host cycle]
    |       |       |
    |       |       +-- parasite_read/write -> TubeUla (plain structs)
    |       |
    |       +-- update parasite IRQ/NMI from TubeUla state
    |
    +-- CpuBinding::tick()  [host CPU]
    |       |
    |       +-- host_read/write -> TubeUla (plain structs)
    |
    +-- update host IRQ from TubeUla state
    +-- check Tube bus stretch -> if stretched, tick parasite until cleared
```

No threads. No atomics. No spin-waits. Deterministic execution order.

### Implementation Commits

| Hash | Date | Description |
|------|------|-------------|
| `e01e41ef8dc1` | 8 Apr 14:55 | Convert Tube tests from threaded to single-threaded interleaving |
| `72fc1ebd578d` | 8 Apr 14:59 | De-atomicise TubeUla and add deferred bus stretch mechanism |
| `f4601b4ae265` | 8 Apr 15:06 | Add single-threaded parasite ticking in Machine::step() |
| `beb37885a07f` | 8 Apr 15:12 | Simplify ParasiteRunner for single-threaded operation |
| `2a3e930c7f1f` | 8 Apr 15:27 | Rewrite extension and boot tests for single-threaded model |
| `65059cb7677` | 8 Apr 08:28 | Fix parasite client returning host CPU and memory state |
| `ee6d002be2fa` | 8 Apr 18:05 | Remove TubeSystem from integration test code paths |
| `d806300d0a4a` | 8 Apr 21:25 | Fix R3 paired transfer synchronisation -- resolve WFSINIT hang |
| `faff3607e4d5` | 8 Apr 21:49 | Rewrite WFSINIT test as end-to-end completion test |

### Key Changes

#### De-Atomicise TubeUla (72fc1eb)

Replaced all atomic data structures with plain equivalents:

| Atomic type | Plain replacement |
|-------------|-------------------|
| `AtomicLatch` (atomic data + flags) | `Latch` (plain `uint8_t`, `bool`) |
| `AtomicFifo24` (atomic SPSC ring) | `Fifo24` (plain array + counters) |
| `AtomicReg3` (packed atomic CAS) | `Reg3` (plain fields, no CAS) |
| `std::atomic<bool>` interrupt outputs | plain `bool` |
| `std::atomic<uint8_t>` control flags | plain `uint8_t` |
| `std::atomic<uint64_t>` transfer counters | plain `uint64_t` |

Removed all `std::memory_order_*` annotations, `.load()/.store()/.fetch_add()`
calls, `compare_exchange_weak` loops, `std::this_thread::yield()` spin-waits,
and `std::atomic_thread_fence` in `soft_reset()`.

Merged `update_host_interrupts()` and `update_parasite_interrupts()` into a
single `update_interrupts()` with unified PNMI edge detection:

```cpp
void TubeUla::update_interrupts() {
    // HIRQ: Q=1 and R4 P-to-H has data
    hirq_ = (flags & FLAG_Q) && r4_p2h_.available;

    // PIRQ: (I=1 and R1 H-to-P has data) or (J=1 and R4 H-to-P has data)
    pirq_ = ((flags & FLAG_I) && r1_h2p_.available)
          || ((flags & FLAG_J) && r4_h2p_.available);

    // PNMI: M=1 and R3 conditions met
    bool new_pnmi = false;
    if (flags & FLAG_M) {
        uint8_t threshold = (flags & FLAG_V) ? 2 : 1;
        new_pnmi = (r3_h2p_.pending || r3_h2p_.count >= threshold)
                || !r3_p2h_.pending;
    }
    // Edge detection -- single-threaded, no race
    if (new_pnmi && !prev_pnmi_) pnmi_edge_ = true;
    if (!new_pnmi) pnmi_edge_ = false;
    prev_pnmi_ = new_pnmi;
}
```

#### Deferred Bus Stretching (72fc1eb)

In single-threaded mode, a spin-wait would deadlock (the parasite can't drain
the register while the host thread is spinning). Instead, writes to full
registers are deferred:

```cpp
case 1: {  // R1 H-to-P data
    if (r1_h2p_.full) {
        host_stretched_ = true;
        pending_offset_ = offset;
        pending_value_ = value;
        return;  // Machine will retry after parasite ticks
    }
    // ... write normally ...
}
```

`try_complete_stretch()` checks if the blocking condition has cleared and
re-executes the deferred operation. `Machine::step()` calls
`tick_parasite_stretch()` during bus stretch, running the parasite until the
stretch clears.

Read-side bus stretching (R3 P-to-H with M flag) was initially handled via
the same deferred mechanism but later changed to inline pumping in
`TubeSocket::read()` (commit `d806300`).

#### Parasite Ticking in Machine::step() (f4601b4)

The `ParasiteTickable` interface:

```cpp
class ParasiteTickable {
public:
    virtual ~ParasiteTickable() = default;
    virtual void tick() = 0;
    virtual bool is_paused() const = 0;
};
```

`TubeSocket` gains:
- `install_parasite(ParasiteTickable*, int numerator, int denominator)` -- sets
  clock ratio (default 3:2 for 3 MHz parasite / 2 MHz host)
- `tick_parasite()` -- fractional accumulator: accumulates numerator each host
  cycle, ticks parasite once per denominator reached
- `tick_parasite_stretch()` -- ticks parasite unconditionally during bus stretch

`Machine::step()` calls `tube_socket_.tick_parasite()` before the host CPU tick
each cycle, and enters a stretch loop if `tube_stretched()` returns true after
a Tube register access.

The `SecondProcessor65C02Extension` no longer creates a `std::thread` or
`PacingClock`. It installs `ParasiteRunner` as the tickable on `TubeSocket`.

#### ParasiteRunner Simplification (beb3788)

Removed all threading primitives:
- `std::mutex`, `std::condition_variable`, `wait_if_paused()`
- `request_shutdown()`, `shutdown_requested()`, `in_run_` flag
- `std::atomic<bool> paused_` replaced with plain `bool`
- `std::atomic<uint64_t> sequence_` replaced with plain `uint64_t`
- `RunGuard` and pause-wait loops removed from `run()`

#### Test Conversion (e01e41e)

Replaced 33 `std::thread` instances across 10 test files with single-threaded
interleaved tick loops. Host operations checked each parasite tick iteration.
`test_tube_r3_race.cpp` removed (tested threading-specific race conditions).
Test names changed from "threaded" to "interleaved".

#### R3 Paired Transfer Synchronisation Fix (d806300)

This was the critical fix that resolved the WFSINIT hang and was the last major
correctness fix applied. Two bugs:

1. **Parasite R3 status `DATA_AVAILABLE` (bit 7) incorrectly reflected H-to-P
   FIFO state instead of the PNMI condition.** For type 6/7 R3 paired
   transfers, the parasite reads R3 in a polling loop waiting for bit 7
   (DATA_AVAILABLE). The correct semantics: bit 7 should reflect the PNMI
   condition (data ready OR output FIFO drained), not just whether the H-to-P
   FIFO has data. Fix: derive bit 7 from the same condition used for PNMI
   generation. Verified against B2's `ReadParasiteTube4` and BeebEm's
   `ReadTubeFromParasiteSide` case 5.

2. **Read-side bus stretching used the deferred-read mechanism, but the host
   couldn't make progress while waiting for data.** Fix: handle inline in
   `TubeSocket::read()` by pumping the parasite until data appears. This is
   analogous to write-side stretch handling but for the read direction.

### Resume Race Condition

Documented in `docs/discussion/tube-single-threaded-resume-race.md`. The
single-threaded model revealed an ordering issue: when `TubeSystem` (the Python
test helper) resumed execution after a breakpoint, it called
`ensure_running()` on the host before unpausing the parasite. In the
single-threaded model, `Machine::step()` immediately starts ticking the
parasite, which is still paused and produces no progress. Fix: reverse the
resume order -- unpause parasite first, then resume host.

### What Worked

- All register protocols correct and deterministic
- CE2023 boots and runs
- DFS SAVE with no byte-doubling
- WFSINIT completes (after R3 paired transfer fix)
- Simple ADFS operations work
- Boot sequence reliable
- Dramatically simpler code -- no atomics, no threading primitives, no races

### The $025F Failures (9 April 2026, resolved)

Immediately after the migration, `test_osword_72_then_adfs_select_with_tube`
and three related tests (file load, WFSINIT) failed with a shared symptom:
the MOS Tube present flag at $025F was cleared mid-session during complex
OSWORD &72 and ADFS select sequences. Bus stretch timing and tick ordering
were suspected.

The cause was in the test harness, not the emulator. The Python
`TubeSystem.run()` resumed the host before unpausing the parasite, so the
host ran alone for one gRPC round-trip after every chunk boundary. The Tube
Host Code's finite polling loops timed out and MOS cleared $025F. See
`docs/discussion/tube-single-threaded-resume-race.md`. The resume order was
reversed and `TubeSystem` was then removed from the integration paths
(`ee6d002b`); the WFSINIT hang had the separate R3 paired-transfer cause
fixed in `d806300`. As of September 2026 every test in the wfsinit,
tube-save and l3fs integration suites passes, including a watchpoint test
on $025F itself.

### Key Files (Current)

```
src/core/include/beebium/tube/
    TubeUla.hpp            -- Plain struct register model (no atomics)
    TubeSocket.hpp         -- Host connector + parasite ticking + clock ratio
    TubeHostBackend.hpp    -- Host-facing interface
    TubeParasiteBackend.hpp -- Parasite-facing interface
    ParasiteTickable.hpp   -- Single-threaded tick interface
    ParasiteCpu.hpp        -- 65C02 wrapper with peek routing
    ParasiteMemoryMap.hpp  -- 64 KB RAM + boot ROM
    ParasiteRunner.hpp     -- Simplified, no threading primitives

src/core/src/
    TubeUla.cpp            -- All register logic, deferred stretch, trace
    ParasiteCpu.cpp        -- NMI suppression, peek routing
    ParasiteRunner.cpp     -- tick(), breakpoints, watchpoints

src/extensions/acorn-65c02-coprocessor/
    SecondProcessor65C02Extension.hpp/cpp  -- No thread, installs tickable
```


---

## Phase 4: The Coprocessor Contract (September 2026)

The lockstep model was kept and generalised. Its execution order became a
stated contract between the core and a coprocessor extension, so that the
thread count is an execution strategy and coprocessors are added by adding
a plugin. The full record, with the reasoning for each step, is
`docs/tube-coprocessor-contract.md`; the as-built description is
`docs/tube-subsystem.md`; adding a coprocessor is
`docs/coprocessor-extension-guide.md`.

| Step | Content |
|------|---------|
| 1 | `Coprocessor` interface driven by host time in host cycles; `CoprocessorClock` exact rational conversion; ratio owned by the coprocessor. Fixed two lockstep defects: the parasite frozen during host 1MHz stretches, and a double advance on the cycle completing a Tube stretch. |
| 1b | The 65C02 coprocessor became a plugin. `CoprocessorExtension` and `TubeInspection` interfaces; the server holds no concrete coprocessor type. |
| 1c | The 65C102 4 MHz coprocessor as a second plugin from the same class. |
| 1e | Firmware packaged with the plugin and declared in its manifest. |
| 2 | The skew bound, 8 host cycles, named and tested. |
| 3 | Batching: the coprocessor runs in batches within the bound, exactly before any host Tube access. Emulation thread busy time fell from 23% to 16% at the BASIC prompt. |
| 1d | A family-agnostic debugger: a `CpuDescriptor` per CPU, one debugger service for host and coprocessor, 32-bit addresses throughout, "parasite" retired from the code. |

Before any of it, a period pin-level test program for the Tube ULA was
transliterated into `tests/test_tube_ula_vectors.cpp` and found two ULA
defects (empty-FIFO reads and the coprocessor R3 status layout), which were
fixed first. The multi-process target was dropped as poorly motivated: the
things wanted from it, isolation, independent debugging and plugability,
are all delivered by the extension boundary, and a multi-threaded strategy
remains available under the same contract should a coprocessor ever
justify it.


## Architecture-Independent Correctness Fixes

These fixes apply to any Tube architecture. If the dual-process or
multi-threaded designs were revisited, all of these must be carried forward.

### 1. R3 Status Flag Hysteresis

**Problem:** Recomputing status from `count >= threshold` on every read produces
transient false-empty in V=1 (two-byte) mode. After reading byte 1 of a pair
(count 2 to 1), `count < threshold(2)` falsely indicates "no data".

**Fix:** Maintain sticky status flags set/cleared as side-effects of FIFO
operations, matching B-Em, BeebEm, and B2. In the cross-process model, this
requires packing count and data_available into a single `atomic<uint16_t>` with
CAS update. In the single-threaded model, plain bools suffice.

**Commits:** `46f5a6c` (packed atomic for cross-process), `72fc1eb` (plain for
single-threaded).

**Reference code:**
- BeebEm: `ReadTubeFromHostSide` case 5 -- clears status when `R3PHPtr == 0`
- B-Em: same pattern with `ph3pos` counters
- B2: `WriteFIFO3`/`ReadFIFO3` with `UpdatePNMI`

### 2. Page-Cross Dummy Read Peek Routing

**Problem:** The 65C02's `LDA (zp),Y` with a page-crossing pointer generates a
dummy read at the uncorrected intermediate address. If this address falls in
Tube register space (&FEF8-&FEFF), the read has side effects -- consuming data
from a latch and corrupting the data stream. This is the root cause of the
CE2023 (Chuckie Egg 2023) hang.

**Fix:** Check `M6502ReadType_Uninteresting` in `ParasiteCpu::tick()` and route
those reads through `memory_.peek()` (side-effect-free) instead of
`memory_.read()`.

**Commit:** `da1125f`

**Caveat:** The 6502 library does not consistently classify page-cross dummy
reads across all addressing modes. `LDA (zp),Y` uses
`M6502ReadType_Instruction` for the fixup cycle, not `Uninteresting`. This may
require further refinement.

**Cross-emulator note:** jsbeeb avoids the problem by accident (single-threaded
JS event loop means the R1 latch is always empty during the dummy read).
PiTubeDirect avoids it by construction: neither its ARM-assembly 65tube core
nor its lib6502 core issues the fixup-cycle read at all; indexed addressing
computes the final effective address and loads once, so no spurious access
can reach the Tube registers (see `docs/discussion/cross-emulator-tube-analysis.md`).
B2 had the same bug and was fixed via PR #569 (tom-seddon/b2) using the same
peek approach.

### 3. R3 Paired Transfer Synchronisation (PNMI and DATA_AVAILABLE)

**Problem:** Parasite R3 status register bit 7 (DATA_AVAILABLE) was reflecting
H-to-P FIFO occupancy rather than the PNMI condition. For type 6/7 paired
transfers, the parasite polls R3 status in a tight loop. The correct bit 7
semantics: reflect the PNMI condition (H-to-P data ready OR P-to-H output FIFO
drained), which is the same condition that generates PNMI.

Also: the P-to-H "space available" condition for PNMI should be `count == 0`
(FIFO fully drained), not `count < threshold`. BeebEm's `UpdateR3Interrupt`
uses `ph3pos == 0`.

**Fix:** Derive status bit 7 from the PNMI condition. Change P-to-H space
condition from `count < threshold` to `count == 0`.

**Commit:** `d806300`

**In a multi-threaded context:** This fix would need to be applied to whatever
thread-safe register model is used. The PNMI condition calculation must be
atomic with respect to the status register read.

### 4. NMI Nesting Suppression

**Problem:** During R3 NMI transfers, the parasite's NMI handler can be
re-triggered before completing if the PNMI edge fires again while the handler
is executing. This causes NMI nesting with a corrupted stack.

**Fix:** `ParasiteCpu` tracks whether the NMI handler is executing and
suppresses NMI assertion during handler execution to prevent spurious edges.

### 5. Read-Side Bus Stretching

**Problem:** When the host reads R3 P-to-H with M flag set and the FIFO is
empty, real hardware bus-stretches the host until the parasite writes data. In
a single-threaded model, the deferred-read mechanism doesn't work well because
the host can't make progress while waiting. In a multi-threaded model, a
spin-wait with yield is appropriate.

**Single-threaded fix:** Handle inline in `TubeSocket::read()` by pumping the
parasite until data appears (commit `d806300`).

**Multi-threaded equivalent:** Spin-wait with yield in `host_read()`, similar
to write-side bus stretching.


---

## Cross-Emulator Comparison

How other emulators handle the same problems, from
`docs/discussion/cross-emulator-tube-analysis.md`:

| Emulator | Architecture | Threading | R3 handling | CE2023 |
|----------|-------------|-----------|-------------|--------|
| B-Em | Single-process, batched | Single thread | Sticky flags, deterministic | Works |
| BeebEm | Single-process | Single thread | `R3PHPtr`/`R3HPPtr` counters | Works |
| B2 | Single-process | Single thread | `WriteFIFO3`/`ReadFIFO3` | Fixed (PR #569) |
| jsbeeb | Single-process, JS event loop | Single thread | Implicit determinism | Works (by accident) |
| PiTubeDirect | Real BBC + Pi GPIO | VideoCore handshake, ARM FIQ, one emulation core | Host is the only clock; ordered via FIQ | Works (no fixup read issued) |
| MAME | Single-process | Single thread | Unknown | Hangs |

The pattern is clear: emulators where both CPUs share deterministic interleaving
within a single thread handle the Tube correctly. MAME is the exception --
single-threaded but still hangs on CE2023, suggesting a different bug in its
implementation.


---

## Branches and Recovery Points

| Branch | Contains | Status |
|--------|----------|--------|
| `single-threaded-tube` | Current lockstep implementation | Active HEAD |
| `tube-extension-redesign` | Multi-threaded extension architecture (pre-single-threaded) | Preserved |
| `abandoned-l3fs-econet-and-tube-r3-fix` | Five R3 fix attempts on dual-process model | Preserved |
| `master` | Merged extension redesign (7 Apr), before single-threaded migration | Stable |

To recover the multi-threaded extension code: check out `tube-extension-redesign`
at any commit before `e01e41e` (the test conversion to single-threaded).

To recover the cross-process infrastructure: the removal commit is `21801d2`.
`git show 21801d2` shows exactly what was deleted. All original code is in
commits prior to this on `tube-extension-redesign`.

To recover the dual-process shared memory model: check out any commit on
`master` between `65a5778` (13 March, shared memory TubeSocket) and `a583e58`
(7 April, merge that replaced it).


---

## Lessons Learned

### The fundamental timing problem

The R3 NMI transfer protocol requires byte-level synchronisation between host
and parasite within ~10-26 us windows. Any architecture where the two processors
run asynchronously with coarser-than-microsecond scheduling granularity will
exhibit byte-doubling on R3 P-to-H transfers. This is not a bug in the
implementation -- it is an inherent limitation of asynchronous execution with
discrete pacing quanta.

### Correctness fixes that may rescue asynchronous designs

The correctness fixes discovered during single-threaded development address
problems that existed independently of the threading model:

1. **R3 status hysteresis** was wrong in both `TubeShared` (cross-process) and
   `TubeUla` (in-process). Fixing it (sticky flags) is necessary regardless of
   architecture.

2. **Page-cross peek routing** was needed in all architectures. The 65C02 CPU
   generates side-effect-producing reads during fixup cycles in every execution
   model.

3. **R3 paired transfer PNMI condition** was wrong in the status register. This
   affected polling loops in every architecture.

If these three fixes were backported to the lock-free atomic `TubeUla` (Phase 2)
or even to the cross-process `TubeShared` (Phase 1), the R3 transfer semantics
would be correct. The remaining question would be whether the timing gap
manifests in practice given correct status semantics. The sticky data_available
flag (Attempt 3 from the R3 investigation) was the correct status fix -- it just
wasn't sufficient alone in the cross-process model. Combined with the PNMI
condition fix and perhaps a read-side bus stretch, it might work.

### What a future multi-threaded design would need

1. All four architecture-independent fixes from the section above
2. A bus-stretch mechanism for R3 P-to-H reads (spin-wait with yield, not
   deferred) to handle the host reading faster than the parasite writes
3. The R3 status register must derive bit 7 from the PNMI condition, not from
   raw FIFO occupancy
4. The PNMI edge detection must be atomic -- either protected by a mutex or
   confined to a single thread
5. Interrupt output caching (`hirq_`, `pirq_`, `pnmi_level_` as atomics) to
   avoid lock contention on the polling path
6. Consider a mutex rather than lock-free atomics -- the access frequency
   (~100,000/s) does not justify the complexity of CAS loops

### What a future multi-process design would need

All of the above, plus:

1. Replace pacing-based timing with explicit synchronisation at R3 transfer
   boundaries -- the pacing system cannot provide microsecond-level coordination
2. Consider a shared semaphore or eventfd for cross-process wakeup on R3
   writes, replacing the io_pending polling approach
3. The packed `atomic<uint16_t>` for R3 count + data_available (from the
   hysteresis fix) is essential to avoid the two-atomic race
4. Lifecycle management for the standalone parasite process (orphan cleanup,
   graceful shutdown, crash isolation)

### Architectural elegance vs practical correctness

The dual-process model was architecturally beautiful -- conceptually faithful to
real hardware, clean process isolation, independent debugging. The multi-threaded
extension model was a pragmatic middle ground -- same-process but still
asynchronous. The single-threaded lockstep model is the least elegant but the
most correct.

The irony is that the correctness problems may not have been fundamental to the
more ambitious architectures. The status flag bugs, the PNMI condition bug, and
the page-cross dummy read bug were all independent of the threading model. They
were discovered during single-threaded development only because the
single-threaded model made them reproducible and debuggable. If they had been
found and fixed earlier, the multi-threaded design might have worked.

This is worth remembering for any future attempt.


---

## Related Documentation

| Document | Location | Content |
|----------|----------|---------|
| Tube subsystem reference | `docs/tube-subsystem.md` | Hardware registers, protocol, control flags |
| Architecture redesign plan | `docs/tube-architecture-redesign.md` | Six-phase dual-process to extension migration |
| R3 pacing investigation | `docs/discussion/tube-r3-pacing-investigation.md` | Five failed fix attempts with analysis |
| Single-threaded migration | `docs/discussion/tube-single-threaded-migration.md` | Detailed before/after with code changes |
| CE2023 hang analysis | `docs/discussion/chuckie-egg-2023-tube-hang.md` | Page-cross dummy read root cause |
| Cross-emulator analysis | `docs/discussion/cross-emulator-tube-analysis.md` | PiTubeDirect, jsbeeb, B-Em, Beebium comparison |
| Resume race condition | `docs/discussion/tube-single-threaded-resume-race.md` | TubeSystem resume ordering fix |
| Emulating the Tube | `docs/discussion/emulating-the-tube.md` | Original architectural thinking |
| Extension framework | `docs/peripheral-extension-framework.md` | Extension points, manifests, CLI integration |
| Tube Application Note | `docs/datasheets/Tube_Application_Note_004.pdf` | Acorn hardware reference |
