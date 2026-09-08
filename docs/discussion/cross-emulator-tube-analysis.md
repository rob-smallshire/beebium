# Cross-Emulator Tube Implementation Analysis

## Purpose

This document analyses how four emulators (PiTubeDirect, jsbeeb, B-Em, and
Beebium) implement the parasite side of the Tube ULA, with particular focus
on how each handles page-cross fixup cycles and their implications for the
CE2023 (Chuckie Egg 2023) Tube hang bug.

The CE2023 root cause is documented in `chuckie-egg-2023-tube-hang.md`: the
LZ decompressor's `LDA ($33),Y` at `$09EB` with pointer `$FEFE` and Y=`$FB`
generates a page-cross fixup cycle.  The intermediate address `$FEF9` (Tube
R1 data register) is on the bus before the high byte is corrected to produce
the final address `$FFF9`.  If this fixup-cycle read has Tube register side
effects, it consumes the R1 latch byte and shifts the data stream.

## Emulator Compatibility Matrix

| Emulator | Architecture | CE2023 Result |
|----------|-------------|---------------|
| Real hardware | Separate clock domains | Works |
| PiTubeDirect | Pi GPIO + real BBC host | Works |
| jsbeeb | Single-process JS event loop | Works |
| B-Em | Single-process, batched | Works |
| Beebium | Multi-process or interleaved | **Hangs** |
| B2 | Multi-process | **Hangs** |
| MAME | Single-process | **Hangs** |

---

## PiTubeDirect

Source: `/Users/rjs/Code/PiTubeDirect`

### Architecture

PiTubeDirect uses a Raspberry Pi physically connected to a real BBC Micro
via GPIO on the Tube connector.  The Pi emulates both the Tube ULA and the
co-processor CPU.  Two processors within the Pi are involved:

- **VideoCore GPU** (`vidcore/tubevc.s`): Polls Tube connector GPIO pins in
  a tight loop.  Detects nTUBE assertion, reads address/data/RnW pins,
  drives the data bus for host reads, and posts mailbox messages to the ARM
  when data register accesses occur.

- **ARM CPU**: Runs the 6502 co-processor emulation on the main thread.
  Receives host-side Tube events via FIQ (Fast Interrupt Request) from the
  VidCore mailbox.  The FIQ handler (`tube.S` line 20) calls
  `tube_io_handler()` which dispatches to `tube_host_read()`/
  `tube_host_write()`.

The real BBC Micro host runs independently at 2 MHz.  The emulated parasite
runs at up to 274 MHz on a Pi Zero -- a 137x speed ratio.

### 6502 Emulator Implementations

Three parasite CPU implementations exist:

**Assembly interpreter** (`copro-65tubeasm.S`): The fast one (274 MHz).
Instruction-level, not cycle-accurate.  Uses ARM registers for 6502 state
(A in r6, X in r7, Y in r8, SP in r9, PC in r10).

**C interpreter** (`lib6502.c` via `copro-lib6502.c`): Slower.  Uses
per-address callback tables for memory access.  Callbacks for `$FEF8-$FEFF`
are installed at startup (line 166-169):
```c
for (; addr <= 0xfeff; addr++) {
    M6502_setCallback(mpu, read,  addr, copro_lib6502_tube_read);
    M6502_setCallback(mpu, write, addr, copro_lib6502_tube_write);
}
```

**JIT compiler** (`copro-65tubejit.c`): Uses `exec_65tubejit()` from
assembly.  Tube register handling details not examined.

### Tube ULA Register Model

`tube-ula.c` implements the Tube register set.  Key design choices:

**Register storage split**: Host-side status and data registers (`HSTAT1-4`,
`PH1_0`, `PH2`, `PH3_0`, `PH4`) are stored in I/O-mapped memory at
`0x7e0000a0` (8 consecutive 32-bit words in the ARM/GPU mailbox region).
Parasite-side status and data (`PSTAT1-4`, `hp1-4`) are local C variables.

The I/O space placement is critical for timing -- the comment at line 24
explains that L2-cached memory caused ~200 ns of unpredictable latency,
while I/O space gives deterministic ~50 ns setup margin.

**R1 host-to-parasite latch** (`tube_host_write` case 1, line 364):
```c
hp1 = val;
PSTAT1 |= 0x80;                    // Set data-available
HSTAT1 &= (uint32_t)~HBIT_6;      // Clear space-available
if (HSTAT1 & HBIT_1) tube_irq |= IRQ_BIT;
```

Simple single-byte latch.  No FIFO in the H->P direction for R1.

**R1 parasite read** (`tube_parasite_read` case 1, line 490):
```c
temp = hp1;
if (PSTAT1 & 0x80) {
    PSTAT1 &= (uint8_t)~0x80;     // Clear data-available
    HSTAT1 |= HBIT_6;             // Set space-available for host
    if (!(PSTAT4 & 128)) tube_irq &= ~IRQ_BIT;
}
```

Every call unconditionally returns `hp1` and clears the data-available flag
if set.  **No read-type filtering.**

### Page-Cross Fixup Cycles: Not Implemented

Neither PiTubeDirect 6502 emulator performs page-cross fixup-cycle reads.

The C emulator's `indy` macro (`lib6502.c` line 257) computes the final
address directly:
```c
#define indy(ticks)
  tick(ticks);
  {
    byte tmp= MEM(PC++);
    ea= getwordzp(tmp);
    tickIf((ticks == 5) && ((ea >> 8) != ((ea + Y) >> 8)));
    ea += Y;
  }
```

`tickIf` adjusts the cycle count but performs no bus access.  The
intermediate address (base pointer high byte with corrected low byte) is
never placed on the bus.

The assembly emulator's `EA_INDIRECT_Y_LOAD_FETCH_NEXT_STAGE_0` macro
(line 462) similarly computes the final address in one step:
```asm
.macro EA_INDIRECT_Y_LOAD_FETCH_NEXT_STAGE_0 reg=temp1
    EA_INDIRECT              // ldrh operand, [operand] -- read 16-bit ptr
    EA_INDIRECT_Y            // add operand, operand, regY, lsr #24
    FETCH_NEXT_STAGE_0
    LOAD_BYTE SIGN \reg      // ldrsb reg, [operand] -- direct memory load
.endm
```

### Tube Address Routing: Absolute Modes Only (Assembly Emulator)

The assembly emulator has a further property: it only routes reads to Tube
registers when using absolute addressing modes.

`LOAD_BYTE` (line 376) is a direct ARM memory load with no Tube check:
```asm
.macro LOAD_BYTE sign=SIGN reg=temp1 src=operand
    ldrsb \reg, [\src]
.endm
```

`LOAD_ABSOLUTE` (line 551) includes a Tube address range check:
```asm
.macro LOAD_ABSOLUTE nocarry=carry sign=SIGN reg=temp1
    lsr     temp2, operand, #5
    LOAD_BYTE \sign \reg
    teq     tregs, temp2          // tregs = 0xFEE0 >> 5
    bleq    tube_load_handler     // Call tube_parasite_read if $FEE0-$FEFF
.endm
```

`tregs` (ARM register r3) permanently holds `0xFEE0 >> 5`.  The `teq`
matches addresses `$FEE0-$FEFF` (the 32-byte range including Tube registers
and bank select).

Only `LOAD_ABSOLUTE` and `STORE_ABSOLUTE` check for Tube addresses.  All
indirect addressing modes (`(zp),Y`, `(zp,X)`, `(zp)`) use `LOAD_BYTE` /
`STORE_BYTE`, which bypass the Tube entirely.

This means even if a `LDA ($33),Y` computes a final effective address in
`$FEF8-$FEFF`, the assembly emulator reads from RAM, not the Tube ULA.

The C emulator does not have this property -- its `getMemory()` callback
fires for all addresses regardless of addressing mode.

### Synchronisation: Interrupt Masking

All `tube_parasite_read/write` calls are wrapped in interrupt disable/enable
(`tube-ula.c` line 484):
```c
int cpsr = _disable_interrupts();
// ... critical section ...
if ((cpsr & 0xc0) != 0xc0) {
    _set_interrupts(cpsr);
}
```

This prevents the FIQ handler (which processes host-side Tube accesses) from
modifying Tube state during a parasite access.  No atomics or locks are
needed because the ARM runs single-threaded for emulation purposes.

### Host-Side Dummy Read Detection (VidCore GPIO)

The VidCore assembly (`vidcore/tubevc.s` line 257) includes detection of
host-side dummy reads at the hardware level:
```asm
# detect dummy read
# spin waiting for clk high
rd_wait_for_clk_high2:
   ld     r7, GPLEV0_offset(r6)
   btst   r7, CLK
   beq    rd_wait_for_clk_high2
   btst   r7, nTUBE           // If nTUBE deasserted -> read cycle over
   bne    Poll_loop
   btst   r7, RnW             // If write follows -> handle it
   bne    Poll_loop
```

After completing a host read cycle, the VidCore checks whether the Tube is
accessed again in the next bus cycle by monitoring GPIO pin state.  This
detects dummy reads from the real BBC Micro host 6502's page-cross behaviour.

The VidCore also skips posting mailbox messages for status register reads
(line 233):
```asm
btst   r7, 2               # no need to post mail if A0 = 0
beq    rd_wait_for_clk_low
```

When A0=0 (even addresses = status registers), no mailbox is posted because
status reads have no side effects requiring ARM notification.

Note: this dummy read detection is for the HOST side only (monitoring the
real BBC Micro's bus via GPIO).  It has no direct relevance to the
parasite-side emulation.

### Execution Speed and Clocking

Findings from a source review made for the Beebium Tube architecture
programme (September 2026), answering why PiTubeDirect's coprocessor runs
at hundreds of MHz-equivalent while Beebium's lockstep uses a fraction of
one core at 3 MHz.  Line references are to `src/`.

**Where the headline number comes from.**  The comment at
`copro-65tubeasm.S:12` records "280.72 MHz" (272.41 MHz with
`FIX_STACK_WRAP_BUG`) for the fast 65tube assembly core on a Pi Zero.  It
is an effective-6502 figure derived from the CLOCKS BBC BASIC benchmark
against a real 2 MHz 6502, not a measured clock.  Performance is logged
through the ARM PMU cycle counter (`performance.c:180-218`).

**Instruction level, no cycle counting, no pacing.**  The fast core
(copro 0/2) executes instructions and never counts cycles; between Tube
accesses it runs flat out.  A slow variant (copro 1/3, selected at
`copro-65tube.c:86`) paces per opcode: `Event_Handler_Single_Core_Slow`
(`copro-65tubeasm.S:3279`) reads a `timing_table` of cycles per opcode,
scales by `copro_speed` (`tube-ula.c:195`), and busy-waits on the PMU
counter in `waste_time` (`:3315`).  That is the pattern for a fast core
held to a nominal MHz.  The C core (`lib6502.c`) does count cycles via
`tick`/`tickIf` (`:24-25`, `:188-215`) but nothing paces on the count.

**The host is the only clock.**  There is no virtual time.  The VideoCore
does the GPIO handshake and posts each host register access as a mailbox
message; the ARM FIQ (`tube.S:20`) runs `tube_io_handler`
(`tube-ula.c:676`) on the *same* core as the emulator (`tube-client.c:136`,
cores 1-3 spin).  Ordering is preserved because every host access is
serialised through that one path and the parasite only ever observes ULA
register and FIFO state, never bus cycles.  Control-register writes must
become visible within three bus cycles, 1.5 us (`tube-ula.c:685`).
`tube_host_read` pre-loads the next value into `tube_regs[]`
(`tube-ula.c:246-254`) so the access side never computes on the critical
path.

**Interrupt delivery costs nothing in the hot path.**  The FIQ ORs
`EVENT_HANDLER_FLAG` into r7, the opcode dispatch-table base
(`tube.S:45-47`).  The next `FETCH_NEXT` therefore lands in a shadow
256-entry table (`copro-65tubeasm.S:3577`) whose entries all branch to
`Event_Handler` (`:3207`).  No per-instruction polling.  IRQ level is
re-sampled on CLI/PLP/RTI via `CHECK_IRQ` (`:307`); NMI is edge-handled
once (`handle_nmi`, `:3233`).  `tube_irq` is the bitfield at
`tube-defs.h:41-47`.  The C core instead polls `tube_irq & 7` every
instruction and externalises state only when a bit is set
(`copro-lib6502.c:119`).

**Fast-core techniques.**  Full 6502 state in ARM registers
(`copro-65tubeasm.S:46-94`); PC held as a native pointer into the flat
64K array (`USE_MEMORY_POINTER`), with the 64K image also mapped at page 16
so `$FFFF,X` wraps without masking (`copro-65tube.c:78`); N/Z/C carried on
the ARM CPSR, V/D/I in r4 and folded in on PHP (`:994`); each opcode a
64-byte aligned block dispatched by `add pc, instt, opcode, lsl #6`
(`:119-128`); a single shift-and-compare against `0xFEE0 >> 5` (`:3160`)
traps the 32-byte MMIO window in `LOAD_ABSOLUTE`/`STORE_ABSOLUTE`
(`:551`, `:574`), everything else being a raw array access.

**Not modelled.**  Exact per-instruction cycle counts (fast variant),
the page-cross dummy read (see above: indexed modes compute the final
effective address and load once, `EA_ABSOLUTE_INDEXED` `:430`), the
read-modify-write double write (a single `STORE_BYTE`), and undocumented
opcodes (NOPs, e.g. `opcode_02` `:946`).  Earlier Beebium notes suggested
the speed ratio hid the dummy-read problem; it does not.  The cores never
issue the read, so the problem cannot arise, and PiTubeDirect is not a
counter-example to the B2 PR 569 fix.

**Relevance to Beebium.**  Beebium's profile (release build, ROM/RAM
board, 65C02 coprocessor, idle at the BASIC prompt, macOS `sample`) shows
the emulation thread about 13% busy, of which the parasite is about a
quarter at roughly 11 ns per emulated cycle; the host CPU, VIAs, video and
per-cycle bookkeeping are the rest.  The difference is structural: a
cycle-accurate lockstep of two machines and all their peripherals against
one free-running CPU with no peripherals and nothing to keep in step with.
What transfers, through the `run_until` contract in
`docs/tube-coprocessor-contract.md`: instruction-level execution inside a
quantum with cycles counted only to find the quantum's end and bus-cycle
precision only around Tube register accesses; event-flag interrupt
delivery instead of per-cycle sampling; and the slow variant's pattern for
a nominal-speed mode on top of a fast core.  What does not transfer: the
omission of the dummy read.  Because Beebium models bus cycles, it must
keep routing fixup-cycle reads through `peek()`.

---

## jsbeeb

Source: `/Users/rjs/Code/jsbeeb`

### Architecture

jsbeeb runs in a single JavaScript execution context.  The host BBC Micro
CPU and the parasite Tube CPU share the same thread.  The parasite is
instantiated as `Tube6502` (extending `Base6502`) when a Tube model is
selected.

### 65C02 Page-Cross Handling: No Memory Read on Fixup Cycle

The Tube parasite model is `Tube65C02` (`models.js` line 170):
```javascript
new Model("Tube65C02", [], ["tube/6502Tube.rom"], CpuModel.CMOS65C02, false)
```

`CpuModel.CMOS65C02` maps to `Cpu65c02` (`6502.opcodes.js` line 1409):
```javascript
export function Cpu65c02(cpu) {
    return makeCpuFunctions(cpu, opcodes65c02, true);  // is65c12 = true
}
```

For `(),y` addressing with page crossing (`6502.opcodes.js` lines 1219-1227):
```javascript
ig = ig.split("addrWithCarry !== addrNonCarry");
if (!is65c12) {
    ig.ifTrue.readOp("addrNonCarry");   // NMOS: read from uncorrected addr
} else {
    ig.ifTrue.tick(1);                   // CMOS: tick a cycle, no memory read
}
ig.readOp("addrWithCarry", "REG");       // Both: read from corrected addr
```

On the 65C02 path (`is65c12=true`), the page-cross fixup cycle is handled
with `tick(1)` -- a cycle count increment with no memory read.  The
uncorrected address `addrNonCarry` is never passed to `readmem()`.

The same pattern applies to `abs,x` and `abs,y` addressing modes
(line 1132-1134):
```javascript
if (is65c12) {
    // the 65c12 reads the instruction byte again while it's carrying.
    ig.ifTrue.tick(1);
}
```

The comment "reads the instruction byte again while it's carrying" suggests
jsbeeb's model is that the 65C02 re-reads from the instruction stream during
the fixup cycle, rather than reading from the uncorrected target address.
However, the implementation simply ticks without performing any read at all.

**Whether this is accurate to real hardware is debatable.** A real 65C02
drives the bus on every cycle -- some address must be present.  No 65C02
variant has pins that allow external hardware to distinguish valid from
fixup bus cycles, so the address decode logic in external hardware (including
the Tube ULA) sees a normal read cycle regardless.  What address the real
65C02 actually puts on the bus during this cycle is not definitively
documented.

### Tube ULA: No Read-Type Filtering

`tube.js` `parasiteRead()` (lines 255-259) unconditionally clears the
data-available flag on R1 data reads:
```javascript
result = this.hostToParasiteData[TUBE_ULA_R1][0];
this.hostStatus[TUBE_ULA_R1] |= TUBE_ULA_FLAG_DATA_REGISTER_NOT_FULL;
this.parasiteStatus[TUBE_ULA_R1] &= ~TUBE_ULA_FLAG_DATA_AVAILABLE;
```

No read-type filtering exists.  This doesn't matter for CE2023 because the
fixup-cycle read never reaches `parasiteRead()` -- it's eliminated upstream
by the 65C02 page-cross handling.

### Execution Model: Host Advances via polltime()

The host CPU's `polltime()` function (`6502.js` line 1226) invokes the
parasite for each batch of host cycles:
```javascript
const tubeStuff = (cycles) => (this.model.tube ? this.tube.execute(cycles) : nop);
```

The parasite runs in batches during the host's `polltime()` call.  This
means the host and parasite are not interleaved at the individual cycle
level -- the host executes one instruction, then the parasite runs for the
corresponding number of cycles.

---

## B-Em

Source: `/Users/rjs/Code/b-em`

### Architecture

B-Em is a single-process, single-threaded emulator.  The host and parasite
share one execution context with instruction-level batching.

### 6502 Parasite: No Page-Cross Fixup-Cycle Reads

The parasite CPU (`6502tube.c`) dispatches all reads through `do_readmem()`
(line 178):
```c
static uint32_t do_readmem(uint32_t addr) {
    if ((addr & ~7) == 0xFEF8)
        return tube_parasite_read(addr);
    if ((addr & ~0xFFF) == 0xF000 && tube_6502_rom_in)
        return tuberom[addr & 0x7FF];
    return tuberam[addr];
}
```

For `LDA (zp),Y`, `read_zp_iy_normal()` (line 314) computes the final
address directly:
```c
static uint8_t read_zp_iy_normal(uint8_t zp) {
    uint32_t addr1 = read_zp_indirect(zp);
    uint32_t addr2 = addr1 + y;
    if ((addr1 & 0xFF00) ^ (addr2 & 0xFF00))
        polltime(1);                        // Extra cycle, no bus access
    return tube_6502_readmem(addr2 & 0xffff);
}
```

No intermediate read from the uncorrected address occurs.  Page crossing
only adds a cycle penalty via `polltime(1)`.

### Tube ULA: Unconditional Latch Clearing

`tube.c` `tube_parasite_read()` (line 252) has the same unconditional
flag-clearing behaviour as PiTubeDirect and jsbeeb:
```c
case 1: /*Register 1 data read*/
    temp = tubeula.hp1;
    if (tubeula.pstat[0] & TUBE_DATA_AVAIL) {
        tubeula.pstat[0] &= ~TUBE_DATA_AVAIL;
        tubeula.hstat[0] |= TUBE_SPACE_AVAIL;
    }
    break;
```

No read-type filtering.

### Execution Model: Cycle-Counted Batching

The host CPU (`6502.c` line 4044) accumulates fractional tube cycles:
```c
if (tube_exec && tubecycle > 3.0) {
    int whole_cycles = (int)tubecycle;
    tubecycles += whole_cycles;
    tubecycle -= whole_cycles;
    tube_exec();
}
```

The tube 6502 runs in a `while (tubecycles > 0)` loop, executing instructions
and decrementing via `polltime()`.  This is instruction-level interleaving,
not cycle-level.

---

## Beebium (Current)

Source: `/Users/rjs/Code/beebium`

### Architecture

Beebium runs the host and parasite either in separate processes communicating
via shared memory, or in a single-threaded interleaved mode (used by tests).
The Tube registers are in shared memory (`TubeShared.hpp`) with atomic
operations for synchronisation.

### 6502 Library: Cycle-Accurate WITH Fixup-Cycle Bus Activity

The 6502 library (`6502.c`, ported from B2) is cycle-accurate.  For
`LDA (zp),Y` with page crossing, `Cycle3_R_INY_BCD_CMOS_D0` (line 2185)
detects the page cross and sets up the fixup cycle:
```c
if (ffa.b.h != s->ad.b.h) {
    s->read = M6502ReadType_Uninteresting;
    s->tfn = &Cycle4_R_INY_BCD_CMOS_D0_AC1;
}
```

The CE2023 investigation's watchpoint evidence shows reads from `$FEF9` with
`read=2` (`M6502ReadType_Instruction`) during `LDA ($33),Y` page crosses.
The address on the bus during the fixup cycle is the uncorrected target
address, and the 6502 library generates a real bus read.

This is arguably more faithful to real hardware behaviour than the other
emulators -- the real 65C02 does put an address on the bus during every
cycle.  The question is whether that bus cycle should have side effects in
the Tube ULA.

### The Problem: Fixup-Cycle Read Has Full Tube Side Effects

`ParasiteCpu::tick()` (`ParasiteCpu.cpp` line 53) routes ALL reads through
the memory map:
```cpp
if (cpu_.read) {
    cpu_.dbus = memory_.read(addr);
}
```

`ParasiteMemoryMap::read()` (`ParasiteMemoryMap.hpp` line 63) dispatches
Tube addresses to the parasite port:
```cpp
if (is_tube_address(address)) {
    rom_enabled_ = false;
    return tube_port_.parasite_read(static_cast<uint8_t>(address & 7));
}
```

`TubeParasitePort::parasite_read()` (`TubeParasitePort.cpp`) performs the
full latch-clearing side effect.  There is no filtering based on
`M6502ReadType` -- the fixup-cycle read is treated identically to a
legitimate data read.

### M6502ReadType Classification Doesn't Help

The 6502 library classifies each bus cycle with an `M6502ReadType`:
- `M6502ReadType_Data` (1): Normal data read
- `M6502ReadType_Instruction` (2): Instruction stream
- `M6502ReadType_Address` (3): Indirect address fetch
- `M6502ReadType_Uninteresting` (4): Some fixup reads
- `M6502ReadType_Opcode` (5): Opcode fetch
- `M6502ReadType_Interrupt` (6): Interrupt dummy read

The CE2023 investigation found that the `LDA (zp),Y` fixup cycle uses
`M6502ReadType_Instruction` (2), not `M6502ReadType_Uninteresting` (4).
The classification is inconsistent across addressing modes -- it was not
designed to distinguish "the CPU will use this value" from "the CPU will
discard this value".

Filtering only on `Uninteresting` would miss the critical case.  Filtering
on everything except `Data` would be overly broad.

---

## Cross-Emulator Comparison

| | PiTubeDirect (ASM) | PiTubeDirect (C) | jsbeeb | B-Em | Beebium |
|---|---|---|---|---|---|
| **Fixup-cycle bus activity** | Skipped | Skipped | Tick only (no read) | Skipped | Memory read with side effects |
| **Tube dispatch scope** | Absolute modes only | All (via callback) | All (via readmem) | All (via do_readmem) | All (via memory_.read) |
| **Read-type filtering in Tube** | N/A | N/A | N/A | N/A | Has types, not used for filtering |
| **Execution model** | Async (274 MHz vs 2 MHz) | Async | Batched (polltime) | Batched (tubecycles) | Multi-process or interleaved |
| **Cycle accuracy** | Instruction-level | Instruction-level | Instruction-level | Instruction-level | Cycle-level |
| **CE2023** | Works | Works | Works | Works | **Hangs** |

The common factor among working emulators: **none of them perform a
side-effecting memory read during the page-cross fixup cycle.**  They each
achieve this differently (jsbeeb ticks without reading; B-Em and PiTubeDirect
compute the final address directly), but the result is the same.

Beebium is the only emulator that generates a bus read during the fixup
cycle AND routes it through the Tube register handler with full side effects.

---

## Analysis

### What Happens on Real Hardware

The open question from the CE2023 investigation remains: why does the game
work on real hardware?

A real 65C02 drives the address bus on every clock cycle.  During a
page-cross fixup cycle, some address is present.  No 65C02 variant
has pins that allow external hardware to distinguish valid bus cycles from
fixup cycles -- the address decode logic in the Tube ULA sees a read cycle
at whatever address happens to be on the bus.

What that address is remains unclear:
- The uncorrected target (low byte corrected, high byte not yet)?
- The last address (re-read of ZP+1)?
- The PC (re-reading the instruction stream, as jsbeeb's comment suggests)?
- Something else entirely?

Even if the address IS `$FEF9`, the Ferranti Tube ULA might not complete a
side-effecting register access.  The ULA crosses clock domains internally
(host and parasite have independent oscillators).  Its internal state machine
may require conditions that are not met during the fixup cycle due to the
asynchronous timing relationship.  This would need gate-level analysis of
the Ferranti ULA or targeted testing on real hardware to verify.

### Why Each Working Emulator Avoids the Problem

**jsbeeb**: Does not perform a memory read during the fixup cycle.  The
65C02 code path calls `tick(1)` instead of `readOp("addrNonCarry")`.  The
uncorrected address never reaches `parasiteRead()`.

**B-Em**: Computes the final effective address directly (base pointer + Y)
without any intermediate bus activity.  Page crossing only adds a cycle
count penalty.

**PiTubeDirect**: Same as B-Em (no intermediate bus activity), plus the
assembly emulator only routes absolute addressing mode reads to Tube
registers.  Indirect modes read directly from ARM memory.

None of these approaches are necessarily "correct" in the sense of
faithfully reproducing what happens on the real 65C02's bus.  They each make
different simplifying assumptions.  The result is that the fixup cycle
either doesn't generate a bus read at all, or generates one that bypasses
Tube register dispatch.

### Beebium's Situation

Beebium's 6502 library IS cycle-accurate and DOES generate bus activity
during the fixup cycle.  This is arguably more faithful to real hardware
than the other emulators.  The problem is not that the 6502 generates the
bus cycle -- it's that `ParasiteCpu::tick()` routes ALL bus reads through
the Tube register handler with full side effects.

The fix should address how the Tube ULA responds to fixup-cycle reads, not
whether the 6502 library generates them.  The 6502 library's cycle-accurate
bus behaviour is a feature worth preserving.

---

## Key Source Files

### PiTubeDirect

| File | Purpose |
|------|---------|
| `src/copro-65tubeasm.S` | Assembly 6502 emulator (274 MHz) |
| `src/lib6502.c` | C 6502 emulator |
| `src/copro-lib6502.c` | Tube callback setup for C emulator |
| `src/tube-ula.c` | Tube ULA register implementation |
| `src/tube.S` | ARM FIQ handler for host-side events |
| `vidcore/tubevc.s` | VidCore GPIO Tube bus handler |
| `src/tube-defs.h` | GPIO pin definitions, constants |

### jsbeeb

| File | Purpose |
|------|---------|
| `src/6502.opcodes.js` | Opcode generator (page-cross handling) |
| `src/6502.js` | Tube6502 class, polltime/execute |
| `src/tube.js` | Tube ULA (parasiteRead/parasiteWrite) |
| `src/models.js` | Tube65C02 model definition |

### B-Em

| File | Purpose |
|------|---------|
| `src/6502tube.c` | Parasite 6502 CPU + memory dispatch |
| `src/tube.c` | Tube ULA register implementation |
| `src/tube.h` | Tube ULA header |
| `src/6502.c` | Host CPU (tube execution interleaving) |

### Beebium

| File | Purpose |
|------|---------|
| `src/6502/src/6502.c` | Cycle-accurate 6502 (page-cross cycles) |
| `src/core/src/ParasiteCpu.cpp` | tick() -- bus read dispatch |
| `src/core/src/TubeParasitePort.cpp` | parasite_read() with side effects |
| `src/core/include/beebium/tube/ParasiteMemoryMap.hpp` | Address routing |
| `src/core/include/beebium/tube/TubeShared.hpp` | Shared memory layout |
