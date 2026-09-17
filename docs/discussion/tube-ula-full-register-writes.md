# Tube ULA: writes to a full register never stall the host

Design note for issue #71 (host write to a full R3 stalls the host; hoglet's
`tube_r3_tests.ssd` hangs in "HP3 in One Byte Mode"). It settles four
questions: whether the ULA can stall the host at all, where Beebium's stall
model came from and what its removal touches, what to do about empty-read
values, and which ULA variant to model.

Status: decided, not yet implemented. beebium-architect briefs the fix.

## 1. The ULA has no way to stall the host

Verdict: none. The deferred-write "bus stretch" for R1, R3 and R4 is a
fiction and is to be removed. Every host write completes in its own cycle.

Evidence, in order of authority:

- **Host connector pinout.** The 6502 Second Processor Service Manual,
  section 5.5, lists the whole 40-way Tube connector: 0V/+5V, R/NW, 2MHzE,
  NIRQ, NTUBE, NRST, D0-D7, A0-A6. There is no ready, wait, DMA-hold or
  clock output from the ULA to the host. 2MHzE is an input to the ULA: the
  host's clock is made on the host board, and the only host-side stretching
  that exists is the host's own 1 MHz bus stretch for FRED/JIM and the slow
  SHEILA devices, which the ULA (a 2 MHz device at &FEE0) does not use.
  (`docs/manuals_text/6502_second_processor_service_manual.txt`, s5.5.)
- **A cycle-exact re-implementation has no such port.** hoglet's
  ReTuLaReMake CPLD (`cpld/tube.v`) exposes on the host side only
  `h_addr[2:0]`, `h_cs_b`, `h_phi2`, `h_rdnw`, `h_rst_b`, `h_data[7:0]`,
  `h_irq_b` (and `drq`/`dack_b` for the Master DMA build). Writes are
  clocked unconditionally by the write enable; the FIFO modules have no
  "full" branch that withholds the acknowledge, because there is nothing
  to withhold. https://github.com/hoglet67/ReTuLaReMake/blob/master/cpld/tube.v
- **Measurements of a real Ferranti ULA.** hoglet's register tests on a
  genuine Acorn ULA (stardot thread 28080, posts p409877 and p412565): a
  write to a full register completes and is either ignored or overwrites;
  the host is never held. Per register and direction:

  | Register | Depth | Write when full |
  |---|---|---|
  | HP1 (host to parasite, R1) | 1 | overwrites |
  | PH1 (parasite to host, R1) | 24 | ignored |
  | HP2 / PH2 | 1 / 1 | overwrites / overwrites |
  | HP3 | 2 | ignored |
  | PH3 | 2 | ignored |
  | HP4 / PH4 | 1 / 1 | overwrites / overwrites |

  hoglet also found that R3's depth is always two bytes; the V flag changes
  only the status flags, not the depth. Beebium's R3 model already treats
  the depth as two independent of V.
  https://stardot.org.uk/forums/viewtopic.php?p=409877
  https://stardot.org.uk/forums/viewtopic.php?p=412565
- **Independent confirmation.** Tom Seddon reproduced the HP3 one-byte-mode
  section on a Master 128 with a cheese wedge (p409902): the third write
  completes and is dropped, exactly as in the Ferranti log quoted in #71.
- **Every other emulator agrees.** b2 (`src/beeb/src/tube.cpp`): host
  latch writes assign unconditionally (`t->h2p1 = value`, `t->h2p4 =
  value`), `WriteFIFO3` stores only `if (*fifo_n < 2)`, the parasite's R1
  FIFO stores only while `p2h1_n < sizeof p2h1`; no stall anywhere. B-Em
  (`src/tube.c`): `if (tubeula.hp3pos < 2)` for HP3, latches overwrite.
  MAME (`src/devices/machine/tube.cpp`): `if (m_hp3pos < 2)`, `if
  (m_ph1pos < 24)`, `if (m_ph3pos < 2)`; latches assign; no HALT, spin or
  ready callback.
- **Our own period vectors say so.** `tests/test_tube_ula_vectors.cpp`
  (the TUBE-TEST replay) already fails any host write that reports
  `stretched()`: the diagnostic's expectations never include a held host.

Application Note 004 describes the register protocol and does not describe
any host stall either; the commit that introduced the stall cited AN004
only for R2 being exempt, which is consistent with the measurements (R2
overwrites) but says nothing about the other registers stalling.

Required behaviour after the fix: the table above, applied on both sides.
Beebium's `host_write` must store or drop and return; nothing is deferred.

## 2. Where the stall model came from, and what its removal touches

Origin: commit ac25a956 (2026-03-14), "Add bus stretching to Tube ULA and
TubeHostPort". Its message asserts "On real hardware the Tube ULA halts the
host CPU when it writes to a full H-to-P register (R1, R3, R4)" with no
source. The motivation was the multi-process `TubeHostPort` of the time,
whose host writes spin-waited on the parasite; the in-process ULA was given
a deferred write to match. The multi-process target was dropped in the
2026-09 Tube architecture programme; the stall survived it.

Commits that built on the model: 01535331 (bus_stretch_cancel dropping
Tube writes), f4601b4a (single-threaded parasite ticking, with a Tube
stretch path in `Machine::step()`), d806300d (R3 paired-transfer
synchronisation, the WFSINIT hang), b02a4655 (the Econet investigation
identified an R3 stretch as the root cause of a server slowdown). Note the
last one: the stall has already cost real time once.

What becomes dead code (remove, do not stub):

- `TubeUla`: `host_stretched_`, `pending_offset_`, `pending_value_`,
  `try_complete_stretch()`, `stretched()`, and the deferred branches in
  `host_write` cases 1, 5 and 7 (`src/core/src/TubeUla.cpp`).
- `TubeHostBackend::stretched()` / `try_complete_stretch()` and the
  `TubeInspection::stretched()` accessor, plus their `TubeSocket`
  forwarders `stretched()`, `tube_stretched()`, `try_complete_tube_stretch()`.
  Check whether `DeviceInspection`'s Tube state reports the flag; if it
  does, drop the field (no wire compatibility is owed between server and
  clients, they release together).
- `Machine`: `tube_stretch_active_`, the Tube-stretch path at the top of
  `step()` (run the coprocessor, try to complete, else `tick_stretch_cycle`
  and return) and the set site after the CPU cycle. The 1 MHz bus stretch
  path beside it is unrelated and stays.
- `CoprocessorRunner::prepare_for_step()` comment "No bus stretching on
  coprocessor side"; `docs/tube-coprocessor-contract.md` sections that
  describe the three `step()` paths and the stretch-path sync rule;
  `docs/tube-subsystem.md` "Bus stretching" (rewrite as "Writes to a full
  register" with the table above); comments in `tests/test_tube_r2_6502.cpp`,
  `tests/test_econet_tx_with_tube.cpp` and the "Removed:" block in
  `tests/test_tube_ula.cpp`. The test "R2 has no bus stretching" becomes
  "R2 write overwrites the latch" and gains siblings for every row of the
  table, both sides.

Migration risk and how it is bounded:

- The scenario suites (tube-save, wfsinit, l3fs) and the Python Tube tests
  ran through the stall path on every R3 paired transfer; d806300d fixed a
  WFSINIT hang inside it. After the change the host's third write is
  dropped instead of held, which is what the Tube host code in the FS ROMs
  is written against, so these must stay green and are the acceptance.
- Any test that passed only because the host was held (a write that
  "waited" for the parasite) will now see a dropped byte; that is the test
  asserting the fiction, and it changes.
- The coprocessor contract's skew bound is unaffected: the host no longer
  has a path that syncs the coprocessor every cycle, which removes work
  from `step()`, not correctness.

## 3. Empty-register reads: keep the bus latch, mask in the golden

What the hardware does (hoglet, same thread): an empty latch (HP1, HP2,
HP4, PH2, PH4) reads back the last value written. The two-byte FIFOs read
back fixed-looking values when empty: HP3 read by the parasite gives &E4;
PH3 read by the host gave &96 in the first report and &94 in the later
"full story", and the later posts say the value depends on more than the
register being empty. Tom Seddon's ReTuLa-based ReCo6502Mini returns &00
and &FF instead, and his AMI Master Turbo shows its own oddities.

What Beebium does: an empty read returns the opposite side's data bus
latch (`host_bus_latch_` / `coprocessor_bus_latch_`), the value last driven
on that side of the ULA. For the latches this coincides with "last value
written" whenever the last bus activity on that side was the write, which
is the common case.

Recommendation: do not model the fixed R3 values now. They are internal to
one implementation, still not fully characterised ("more complex" in the
later posts), and differ between Ferranti, AMI and ReTuLa. Keep the bus
latch, and in the golden comparison against hoglet's Ferranti log mask the
data byte of every read whose accompanying status shows the register empty
(the parasite-side data-available flag clear for an HP3 read, the host-side
hdav clear for a PH3 read); compare flags and non-empty data exactly. File
a follow-up to model empty-read values once the "full story" posts are
digested into a table, and note in `docs/tube-subsystem.md` that the
empty-read value is a simplification.

## 4. Model the Ferranti ULA

Model the Ferranti part, the one in the cheese wedge and the source of
every golden log we compare against. Record the AMI (Master Turbo) ULA's
observed deviations in `docs/tube-subsystem.md` as known differences, not
as a switch: hoglet's PH3 line in the R3 test differs on the AMI part, and
Tom Seddon saw HP1 return an unexpected value and PH1's hdav stay set after
the last byte was read. Nothing in Beebium selects a ULA variant and
nothing should until a preset needs it.

## Sources

- 6502 Second Processor Service Manual, s5.5 (connector) and s6 (ULA):
  `docs/manuals_text/6502_second_processor_service_manual.txt`.
- hoglet, "Tube ULA Re-Implementation", stardot thread 28080:
  https://stardot.org.uk/forums/viewtopic.php?t=28080, posts p409877
  (first Ferranti measurements), p409902 (Tom Seddon, Master 128 + cheese
  wedge), p412565 (the full story).
- ReTuLaReMake: https://github.com/hoglet67/ReTuLaReMake/blob/master/cpld/tube.v
- b2 `src/beeb/src/tube.cpp`; B-Em `src/tube.c`; MAME
  `src/devices/machine/tube.cpp`.
- Beebium history: ac25a956, 01535331, f4601b4a, d806300d, b02a4655.
- Issue #71 and the stardot report p492658.
