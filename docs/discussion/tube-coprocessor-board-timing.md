# 6502 Second Processor board timing: refresh and write stretch

Design note for issue #70 (the 6502 Second Processor runs about 4.2% faster
than hardware). It fits two board-level clock effects into the host-time
coprocessor contract, sizes the residual candidates named in the issue, and
recommends one measurement per candidate. It is a design; the implementation
is briefed separately.

Status: decided, not implemented.

## 0. Summary

A real 6502 Second Processor (the 3 MHz cheese wedge) is slower than 3 MHz
for two reasons, both on the board, neither in the CPU:

1. **DRAM refresh steals one cycle every ~44.** A 750 kHz refresh timer
   (12 MHz / 16, eleven counts) raises a request; the CPU is held by RDY for
   one cycle at the next opcode fetch (SYNC). Overhead about 2.2%.
2. **Every write cycle is stretched by one 12 MHz period.** The service
   manual: "During a write cycle (R/W=0) PH1 is stretched slightly in its
   high state by the action of R/W on IC12 via IC9 pin 2 and IC11. This is
   for RAM timing purposes." A write cycle therefore lasts 5/4 of a nominal
   cycle. Overhead about 0.25 x (fraction of cycles that are writes), which
   is about 2% for interpreter and compiled-language code.

Together these account for the measured 4.2% without any residual, and they
explain the two independent measurements that a refresh-only model cannot:
tom_seddon's cheese-wedge loops run at 2.922 MHz for LDA zp but 2.703 MHz for
STA zp, while the internal 65C102 co-processor runs both at 3.939 MHz; and
MAME, which models neither effect, is 3.9-4.5% fast on the wedge but only
1.25-1.5% fast on the Master Turbo. The 65C102 board has refresh (one in
64) and no write stretch; the 256K Turbo is unknown and stays unmodelled.

## 1. The hardware, from the schematic-derived sources

Clocks (service manual s5.1, s5.2): a 12 MHz crystal (IC8) feeds IC12, a
74LS393 dividing by 4, giving PHI IN at 3 MHz. In boot mode IC11 (another
74LS393, divide by 16) gates IC12 so PHI IN is 187.5 kHz; leaving boot mode
disables IC11.

Refresh (service manual s5.3, parts list; hoglet p445510, p445527):
IC13, a 74LS393, is the refresh timer. hoglet reads the chain as
12 MHz / 16 / 11 = 68.182 kHz: the timer counts at 750 kHz and fires after
eleven counts, i.e. every 176 crystal periods, which is 44 nominal 3 MHz
cycles. When it fires it "requests a refresh by setting pin 15 high (which
also pulls pin 7 low to stall the counter while the refresh is taking
place). The NAND gate (pins 8 9 and 10 of IC10) waits for SYNC to go high
signalling the end of an instruction cycle and pulls the READY line low to
stall the processor." The refresh row address from IC7 is selected instead
of the CPU's, RAS (inverted PHI IN) performs the refresh, "and on its rising
edge clocks pin 3 of the D-type flip-flop in IC6, which clears the refresh
request and reloads the timer (IC13)." Two consequences for the model:

- the stolen cycle lands on the next opcode fetch after the timer fires,
  never inside a Tube access (hoglet: "to avoid side-effects if a refresh
  cycle co-incided with a tube access");
- the timer is stalled from firing until the refresh completes and is then
  reloaded, so the interval between refreshes is 176 crystal periods PLUS
  the wait for SYNC. The overhead is 1/(44 + L) with L the mean wait in
  cycles, a little under hoglet's 1/44 = 2.27%. For the 65C102 board the
  same reading gives 1/(64 + L); tom_seddon's 3.939 MHz for both loops is
  4.000 x (1 - 1/65) = 3.938, and MAME's 1.25-1.5% fits 1/65.5 = 1.53%.
  The refresh hold is one cycle: the request clears on the first RAS edge
  after RDY is honoured. LK5 selects the refresh rate for a 4 MHz processor
  on this board and "must not be altered" at 3 MHz.

Write stretch (service manual, quoted in stardot thread 25167; the sentence
is not in our partial transcript and should be checked against the full
manual PDF at chrisacorns, Acorn_65022ndprocSM.pdf): on a write cycle
R/W acts on the divider IC12 through IC9 pin 2 and IC11 to hold PHI1 high
for one extra 12 MHz period, for DRAM write timing. A write cycle is 5
crystal periods instead of 4. Checked against measurement: an STA zp loop
is two read cycles and one write cycle, 2 + 1.25 = 3.25 nominal cycles for
3 nominal, so the store loop should run at 3/3.25 = 0.923 of the load loop;
tom_seddon measured 2.703 / 2.922 = 0.925. The internal 65C102 board has no
such circuit and measures the same for both loops.

Tube collision stall (service manual s5.3): "The whole second processor is
halted when the NOR gate (pins 8 9 and 10 of IC14) detects a collision.
This happens when both the host and parasite select the Tube
simultaneously." This is hoglet's second overhead; it is sized in section
3 and not modelled now.

Sources: hoglet https://stardot.org.uk/forums/viewtopic.php?p=445510 and
p=445527; tom_seddon's loop measurements and the manual quotation,
https://stardot.org.uk/forums/viewtopic.php?t=25167; acheton1984's Tak
results in #70; our transcript
`docs/manuals_text/6502_second_processor_service_manual.txt` s5.1-5.4 and
the parts list (IC7, IC11, IC12, IC13, IC6, IC10).

## 2. Fitting it into the host-time contract

The contract (docs/tube-coprocessor-contract.md) drives the coprocessor in
host time: `CoprocessorClock::cycles_due(host_cycle)` converts elapsed host
cycles to coprocessor cycles by an exact rational ratio with a carried
remainder, and `CoprocessorRunner::run_until` executes every due cycle. Two
things change: a coprocessor cycle is no longer of one fixed length, and
some due time executes nothing.

### Count crystal ticks, not CPU cycles

Make the unit of the coprocessor clock the board's crystal period, not the
CPU cycle. Everything on the board is an integer number of crystal periods:
a read cycle is 4, a write cycle is 5, the refresh timer fires every 176,
the refresh hold is one cycle (4 ticks). The clock ratio to the host stays
exact and rational: 12 MHz against the 2 MHz host is `ClockRatio{6, 1}`
ticks per host cycle. `CoprocessorClock` is unchanged in kind; it now
yields ticks. The skew bound is unchanged too: the runner never runs ahead
of due time by more than one cycle's cost (5 ticks, under one host cycle
of 6), so the coprocessor still lags the host by fewer than
MAX_COPROCESSOR_SKEW host cycles and register accesses stay exact.

A per-board timing description, supplied by the plugin and consumed by the
runner:

```
struct BoardTiming {
    ClockRatio ticks_per_host_cycle;   // wedge {6,1}; 65C102 {2,1}
    uint32_t   read_cycle_ticks;       // wedge 4; 65C102 1
    uint32_t   write_cycle_ticks;      // wedge 5; 65C102 1
    uint32_t   refresh_period_ticks;   // wedge 176; 65C102 64; 0 = none
    uint32_t   refresh_hold_cycles;    // 1 on both boards
};
```

The 65C102 board is expressed in its own 4 MHz cycles (one tick per cycle,
refresh every 64 ticks, no stretch). The 256K Turbo gets `refresh_period_
ticks = 0` and a comment saying its refresh is unmeasured. The counter, the
hold and the stretch live in `CoprocessorRunner`, which is already the
6502-family runner; another CPU family brings its own board timing with
its own runner. The plugin owns the numbers, the runner owns the mechanism,
and `ClockRatio` as the extension API's notion of "how fast" is replaced by
`BoardTiming` for 6502-family boards (the coprocessor extension guide and
the contract's Coprocessor interface section change accordingly).

### The runner's loop

`run_until(host_cycle)` obtains `due_ticks` and adds them to a tick budget.
While the budget can pay for the next cycle it executes one:

1. If a refresh is pending and the CPU is at an opcode fetch
   (`M6502_IsAboutToExecute`), consume `refresh_hold_cycles x
   read_cycle_ticks` ticks, execute nothing, clear the pending flag and
   restart the refresh timer from zero (the reload on the RAS edge). This
   is the RDY hold: our M6502 wrapper has no RDY input, and none is needed;
   a held cycle is simply a tick cost with no `cpu_.tick()`. The CPU's
   state, its about-to-execute status and its interrupt sampling are
   untouched, exactly as on the part.
2. Otherwise tick the CPU once and charge `read_cycle_ticks` or
   `write_cycle_ticks` according to whether the cycle it just performed was
   a write (`cpu_.cpu().read == 0` after the tick, the same flag the memory
   map uses). Charging after the fact is exact over any span and avoids
   predicting the cycle kind; the budget may briefly go one write's extra
   tick negative, which is the carried deficit.
3. Advance the refresh timer by the ticks consumed in 1 or 2; when it
   reaches `refresh_period_ticks`, set pending and stop counting until the
   hold has happened (the stalled timer).

The breakpoint check stays at the top of `step()` but a held cycle returns
before it, so a breakpoint at the fetch fires once, on the executing cycle.
`step_instruction()` (the debugger's single step) must go through the same
loop so that a step across a refresh costs the extra cycle and reports it;
today it calls `cpu_.step_instruction()` directly, which bypasses the
runner, and that has to change to a loop of `step()` until the CPU is
about to execute again. `cycle_count()` reported to the debugger stays
"CPU cycles executed"; add `ticks()` only if the debugger wants it.

### Tube accesses

A refresh hold never coincides with a Tube register access, because it
lands on an opcode fetch from RAM; this is the hardware's own reason for
choosing SYNC and the model inherits it for free. A parasite write to a
Tube register is a write cycle and is stretched like any other; the
register update happens inside the cycle as now. Host-side accesses are
unaffected: the host clock is not involved in any of this.

### What stays exact

- Ratio: 6 ticks per host cycle, carried remainder, no drift.
- Skew: unchanged bound, unchanged register-access exactness.
- Pause/resume, reset and rebase: a reset restarts the refresh timer and
  clears any pending hold (the request flip-flop is cleared by NRST via
  IC6); the clock rebase semantics are unchanged.

## 3. The residual, candidate by candidate

The issue attributes 2.27 points to refresh and leaves 1.9 unexplained.
With the stalled-and-reloaded timer refresh is nearer 2.2 points, so the
residual is about 2.0.

(a) **Tube collision stall.** Sized for a compute-bound benchmark: a
collision needs the host and the parasite to select the Tube in the same
cycle; the parasite touches the Tube only in OS calls (a Tak run prints a
handful of lines), and each collision halts the wedge for at most one host
access, half a microsecond. Even a thousand collisions in a 180 s run is
0.0003%. Not the residual. Measurement: none needed for #70; if ever
modelled, a test that counts collisions during a Tube-heavy transfer.

(b) **65C02 instruction cycle counts.** Our coprocessor core is
`M6502_rockwell65c02_config` on the b2-derived generated core, which has
the 65C02-specific timings (decimal-mode extra cycle, 6-cycle RMW abs,X
without page cross, BRA, the CMOS dead-cycle addresses; commit e0d3337e
added the dead-cycle bus addresses, nothing changed the counts). Two
independent facts argue against a core defect: MAME's own 65C02 core shows
the same 3.9-4.5% on the wedge, and the residual vanishes on the internal
65C102 board in both MAME and tom_seddon's loops, where the CPU is the same
but the board has no write stretch. Recommended measurement anyway, because
it is cheap and permanent: a table-driven ctest that, for every 65C02
opcode and addressing mode (with and without page crossing, decimal mode
on and off, branch taken and not taken), ticks the core from one
`IsAboutToExecute` to the next and compares the count with the datasheet
table. Expected outcome: all equal; any inequality is its own fix.

(c) **Host time base.** `Machine::step()` advances `cycle_count` by exactly
one on both its paths (the 1 MHz stretch path and the normal path), so one
count is one 2 MHz cycle; the System and User VIAs are ticked on alternate
host cycles (rising then falling), i.e. at 1 MHz, and the MOS centisecond
comes from System VIA T1 at 10 000 us. No defect found by reading.
Recommended measurement: a Python scenario that runs exactly 2 000 000 host
cycles from a known TIME and asserts TIME advanced by 100 (allowing one
tick of phase), on the plain Model B and again with the coprocessor
attached and busy; and the host-only Tak variant from the reporter's disc
compared with the BBC B column of his table. Expected: exact.

(d) **The write-cycle stretch**, not in the issue's list, is the residual.
Its size depends only on the fraction of cycles that are writes: 0.25 x
(writes / cycles). For an interpreter loop that fraction is typically
6-10% (stack pushes, zero-page workspace, string and variable stores),
giving 1.5-2.5 points; with refresh's 2.2 that spans the measured
3.96-4.46% and centres on the 4.23% mean. Measurement, which doubles as
the acceptance test for the fix: reproduce tom_seddon's two loops on the
coprocessor and read them with TIME: an unrolled LDA zp loop must read
about 2.93 MHz and an unrolled STA zp loop about 2.70 MHz (ratio 0.92),
red today at 3.00/3.00, green with the model; and the same pair on the
65C102 preset must read about 3.94 MHz for both.

## 4. Tests and acceptance for the fix

- Unit, runner level: with BoardTiming for the wedge and a program of N
  three-cycle read-only instructions, N x 12 ticks executes N instructions
  minus one held cycle per 176 ticks, and the hold is always at an opcode
  fetch (assert `IsAboutToExecute` before and after the held cycle, CPU
  state unchanged). With a program of STA zp, each instruction costs 13
  ticks. With the 65C102 BoardTiming, one hold per 64 cycles and no
  stretch.
- Unit, clock level: `ClockRatio{6,1}` over a long host span yields exactly
  six ticks per host cycle with no drift; the skew bound test in
  test_coprocessor_skew is re-run with ticks.
- Scenario (Python): the LDA/STA loop pair on both presets, as in 3(d),
  red then green; and the existing boot-banner and tube-save/wfsinit/l3fs
  suites unchanged.
- Regression fixture: acheton1984's TAKAsm at 2.64 s on hardware, if the
  disc may be redistributed; otherwise the loop pair stands in.
- Docs: tube-subsystem.md gains a "Board timing" section with the table
  of both boards; the contract's Coprocessor interface and the extension
  guide describe BoardTiming; the 256K Turbo is listed as unmodelled.

## 5. What not to do

Do not fold the effects into the clock ratio (a 2.93 MHz ratio would be
right on average and wrong for every store-heavy or store-light program),
and do not model the write stretch as a fractional cycle on a 3 MHz clock
(it is exactly one crystal period; the tick model is both simpler and
exact).
