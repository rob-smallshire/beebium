# The uPD7002 analogue-to-digital converter: round 2 specification (#98)

This is the specification for modelling the uPD7002 ADC chip itself. It is the second round of
the analogue port programme. Round 1 was the research survey and the hardware trial sheet
(`analogue-port-hardware-trials.md`). Round 3 will add the `AnaloguePort` handle, the
`AnaloguePortDevice` seam and the `analogue-port` attachment point in the shape of the printer
and user ports. Round 4 will add the first device, a joystick adapter presenting host joysticks
as BBC joysticks. This round builds only the chip, its memory region, its end-of-conversion
line into the System VIA, its inspection state and its tests.

## Principles

- **The datasheet and real hardware are the authorities.** Where the datasheet is silent (the
  conversion-time sawtooth, the unplugged-port value) the measurements in the survey and the
  hardware trial sheet decide. Where neither says anything yet, this document makes a decision
  and marks it `Decision`; the trial results may revise it.
- **No MOS dependence.** The model must not know what MOS 1.20 does with the chip. MOS
  behaviour (the reset write of `&0E`, the CB1 handler, the 10 ms poll, `ADVAL`, event 3) is
  used only as a test vector, because we control which MOS a test machine loads.
- **Test first.** Every behaviour below has a test named with it. Write the failing Catch2 test,
  then the minimum code, then refactor.
- **Cycle accurate where it matters.** Conversion time is in 1 MHz ADC clocks, driven from the
  machine clock, not from host time or from a fixed millisecond count.

## The chip

The uPD7002 is a dual-slope integrating converter with a four-channel analogue multiplexer. It
has no reset pin and no power-on reset. The BBC Micro clocks it at 1 MHz (the 1 MHz bus clock;
C25 = 33 nF matches the datasheet's C_INT = 0.033/f_clk(MHz) uF). Its chip select on the Model
B decodes `&FEC0` to `&FEDF`, with A1 and A0 selecting one of four registers, so the four
registers repeat every four bytes across the thirty-two.

### Registers

Offset is `address & 3`.

| Offset | Read | Write |
|---|---|---|
| 0 | Status (below) | Latch channel, mode and flag, then start a conversion |
| 1 | Result high byte. **Resets EOC** | Ignored |
| 2 | Result low byte; bits 3 to 0 read as 0 | Ignored |
| 3 | Result low byte, the same as offset 2 | Test mode. Modelled as ignored |

Write to offset 0, bits:

| Bit | Meaning |
|---|---|
| 7 to 4 | Ignored |
| 3 | 1 = 12-bit (10-bit accurate) mode; 0 = 8-bit mode |
| 2 | Flag. Stored and echoed in status; has no effect on the chip |
| 1, 0 | Channel 0 to 3 |

Status (read offset 0), bits:

| Bit | Meaning |
|---|---|
| 7 | EOC, as the EOC pin: 1 = conversion not complete (also 1 after the high byte is read) |
| 6 | 1 = not busy. 0 from the start write until the conversion completes |
| 5, 4 | The two most significant bits of the result. During a conversion these are the **previous** result's bits |
| 3 | Mode bit as written |
| 2 | Flag as written |
| 1, 0 | Channel as written |

The result is sixteen bits wide as the Acorn software sees it (`&0000` to `&FFF0`). In 12-bit
mode the high twelve bits are the conversion code and the low four read 0. `Decision`: in 8-bit
mode the high byte is the 8-bit code and the whole low byte reads 0. Trial 7's `00` run
records the raw low byte and will confirm or revise this.

### Power-on state

The chip has no reset. `Decision`: at machine construction the chip is idle with EOC inactive
(status bit 7 = 1), not busy (bit 6 = 1), control bits 3 to 0 all 0, a result of `&0000`, and
CB1 high. Status therefore reads `&C0`.

**Break does not touch the chip.** The CPU reset line does not reach the uPD7002. A conversion
in flight across Break completes normally and drives CB1 low; what the freshly reset System VIA
makes of that edge is the VIA's business. Power-on (constructing a new machine) is the only way
to a clean chip.

### Starting a conversion

A write to offset 0:

1. Stores channel, mode and flag.
2. Samples the selected channel's input level **at the write** (the datasheet's
   sample-and-hold at the start of integration; the integration itself is not modelled).
3. Computes the result code from the sampled level and the conversion time from the mode and
   that code (below), and arms a countdown in 1 MHz clocks.
4. Sets busy (bit 6 = 0), sets EOC inactive (bit 7 = 1), raises CB1 to 1.
5. Leaves bits 5 and 4 and the result registers showing the previous result until completion.

A write during a conversion re-initialises the sequence: the conversion in progress is
abandoned and a new one starts from the new write, with a fresh sample. Nothing of the
abandoned conversion is visible afterwards.

### Completing a conversion

When the countdown reaches zero, on a 1 MHz clock edge:

1. The result registers and status bits 5 and 4 take the new result.
2. Busy clears (bit 6 = 1).
3. EOC goes active: status bit 7 = 0 and CB1 = 0.

Reading offset 1 (the high byte) resets EOC: status bit 7 = 1 and CB1 = 1. Reading offsets 0,
2 or 3 has no side effect. The debugger peek path must read all four offsets without side
effects (`MemoryRegion` `HasSideEffects` is set, and `ModelBHardware::peek` routes the region
to a `peek` method as it does for the VIAs and the Tube).

### End of conversion into the System VIA

EOC is a **level** on System VIA CB1: 1 while idle or converting, 0 from completion until the
high byte is read or a new conversion is started. `SystemViaPeripheral::update_control_lines`
sets `cb1` from the chip's EOC level on every call. Edge detection and the PCR polarity belong
to the VIA and are not duplicated. Under MOS 1.20's PCR (`&04`, negative edge on CB1) the
completion gives one interrupt per conversion; under positive-edge mode the high-byte read
and the start write give edges instead. Both are reachable by programs and both must come
out of the VIA's own logic.

### Conversion time

Time is counted in 1 MHz clocks from the start write to completion. The datasheet gives only
ranges (2.4 to 5 ms and 8.5 to 15 ms). The measured shape (survey section 2 and the trial
sheet's trial 7 notes) is reproduced:

- **8-bit mode**: `t = 2950 + c / 2` clocks, where `c` is the 12-bit code (0 to 4095) the
  conversion will produce. One straight line from about 2.95 ms to 5.0 ms.
- **12-bit mode**: a sawtooth of seven teeth. Each tooth is a line of slope 2 clocks per
  12-bit code starting at a boundary code `b_k` with a start time `T_k`:
  `t = T_k + 2 * (c - b_k)` for the tooth with the largest `b_k <= c`. The table, from one
  Model B (survey section 2.2, hoglet's plot, about plus or minus 30 us):

  | Tooth | `b_k` (12-bit code) | `T_k` (clocks) |
  |---|---|---|
  | 1 | 0 | 9100 |
  | 2 | 1020 | 10118 |
  | 3 | 1452 | 9957 |
  | 4 | 2052 | 10139 |
  | 5 | 2628 | 10265 |
  | 6 | 3204 | 10387 |
  | 7 | 3796 | 10553 |

  The time runs from 9.10 ms at code 0 to about 11.55 ms at the top of tooth 6, and each
  boundary drops about 1 ms. A single `T0 + 2c - 1000 n(c)` formula drifts 140 us from the
  measurements by the top of the range, which is why the table carries a start time per tooth.

`Decision`: the model is deterministic. The per-chip boundary shifts, the overlap band where a
conversion just past a boundary still takes the earlier tooth's time, the plus or minus 10 us
jitter and the plus or minus 4 codes of noise are **not** modelled in this round. Trial 7 on a
second chip tells us how far the boundaries move and how wide the overlap is; if the answer
makes a parameter worth having, it becomes one then. The constants are to be in one place,
named, with the measurement they come from in a comment, so they can be re-fitted from trial
7's data without touching logic.

The countdown is driven by the machine's 1 MHz clock. The chip is a `ClockSubscriber` at
`ClockRate::Rate_1MHz` (falling edge is enough) through `ClockBinding`, like the other
peripherals; it must not count host time and must not count CPU cycles directly, so that bus
stretching and the 2 MHz/1 MHz relationship stay the clock architecture's concern.

### Inputs and the unplugged port

In this round the chip has a direct input seam for tests: `set_input_level(channel, level)`
with `level` a 16-bit unsigned fraction of VREF (`0` = analogue ground, `&FFFF` = VREF). The
12-bit code is `level >> 4`. Round 3 moves the source of these levels behind the
`AnaloguePortDevice` seam; the chip's own interface does not change.

`Decision`: with nothing driving a channel, its level is `&8000`, so an unplugged channel reads
`&8000` in 12-bit mode. Real machines read a wandering, machine-dependent value somewhere in
the upper half of the range (survey section 4: about `&7300`, `&9500` to `&99E0`, `&C800` to
`&CBA0` on three machines), so there is no single right value; the centre is the choice that
upsets the fewest programs. Trial 4 will add a fourth machine's reading to the record, not
change this decision unless it reveals something systematic.

Fire buttons on System VIA PB4 and PB5 stay as they are today (not pressed, both read 1)
until round 3 gives the port a device.

### Memory region

Add `make_region<0xFEC0, 0xFEDF, Mirror<0x03>>` over the chip to `ModelBHardware` and the
other Model B family hardware policies that list the region in their memory map comments
(`ModelBPlusHardware`, `ModelBPlus128KHardware` and the ROM/RAM board variants built on the
Model B map). The region is a 1 MHz access (`kSheilaSlowdownMask` already marks it; the
test `test_memory_map.cpp` lines 564 to 566 asserting the open-bus value `&00` there must be
replaced by tests against the chip, because the region is no longer unmapped).

## Inspection

Add `GetAnalogueConverterState` to `DeviceInspection` in `debugger.proto`, returning:
status byte, result (16-bit), channel, twelve_bit, flag, busy, eoc (the CB1 level), clocks
remaining in the current conversion (0 when idle), and the four input levels. It reads under
quiesce like the other `Get*State` RPCs. The proto change means regenerating the Python and
TypeScript stubs, running `tsc`, rebuilding every server, and a protocol fingerprint change;
land it as its own commit after the chip, and run the full gate from a separate worktree build
before telling me it is done.

## Tests

### Catch2, on the chip alone (`tests/test_upd7002.cpp`)

All names 7-bit ASCII. One `TEST_CASE` per line below, tagged `[upd7002]`.

1. Power-on state: status `&C0`, high and low bytes 0, CB1 level 1.
2. Mirror: offsets 0 to 3 repeat at every 4-byte step from `&FEC0` to `&FEDF` (test through
   the Model B memory map, `[memory_map]` tag as well).
3. Status echoes the written channel, mode and flag bits.
4. Busy and EOC during conversion: after a start write, bit 6 = 0 and bit 7 = 1 until the
   last clock; on completion bit 6 = 1 and bit 7 = 0.
5. Status bits 5 and 4 show the previous result's MSBs during a conversion and the new
   result's on completion (two conversions on inputs `&0000` then `&FFFF`).
6. 12-bit result layout: input `&ABCD` gives high `&AB`, low `&C0`; offset 3 equals offset 2.
7. 8-bit result layout: input `&ABCD` gives high `&AB`, low `&00`.
8. High-byte read resets EOC: bit 7 returns to 1 and CB1 to 1; reading offsets 0, 2 and 3
   does not.
9. Peek has no side effects: peeking offset 1 after completion leaves bit 7 = 0 and CB1 = 0.
10. Write during a conversion restarts it: the new channel's level and the new mode are what
    complete, and the completion time is measured from the second write.
11. 8-bit timing: codes 0, 2024 and 4080 complete at 2950, 3962 and 4990 clocks.
12. 12-bit timing: the last code of each tooth (1016, 1448, 2048, 2624, 3192, 3784, 4080)
    completes within 15 clocks of the measured end times (11138, 10975, 11153, 11281, 11386,
    11560, 11120), and the first code of each tooth from the table completes at `T_k`
    exactly; the step from 1016 to 1024 drops by more than 950 clocks.
13. Unplugged channel reads `&8000` after a 12-bit conversion.
14. Writes to offsets 1, 2 and 3 change nothing observable.

### Catch2, on a Model B with MOS 1.20 as the test vector (`tests/test_upd7002_model_b.cpp`)

These load MOS 1.20 from the test assets as the existing BASIC-driven tests do and assert
machine behaviour, not MOS behaviour as such.

15. After reset the first conversion starts within 50 ms of emulated time, on channel 2 in
    12-bit mode with the flag set (the MOS's `&0E`). This is a vector for "the region exists
    and the write path works", not a statement about what a Beeb must do.
16. With all four inputs set to distinct levels, and after a second of emulated time, the MOS
    workspace holds each channel's high byte at `&2B0` plus channel offsets as documented in
    the Advanced User Guide, read through the debugger peek path. This proves the CB1
    interrupt, the handler's register reads and the channel cycling all meet.
17. The System VIA sees a negative edge on CB1 roughly every 9 to 12 ms while the MOS cycles
    the channels, measured from `ViaState.ifr` bit 4 or a CB1 edge counter, never faster.

### Python integration (`clients/beebium-python-client/tests`)

BASIC programs go onto an SSD with `uvx oaknut-basic tokenise` and `uvx oaknut-disc`, inserted
and run with `CHAIN`, never typed in. Use a marker `PRINT` and `basic.wait_for_text` rather
than `wait_for_prompt` (#270).

18. `ADVAL(1)` to `ADVAL(4)` all read `&8000`, the unplugged value, and `ADVAL(0) AND 3`
    reads 0 (no fire buttons). There is no Python-side input hook in this round; the C++
    tests cover set inputs, and round 4 gives the Python client a real source. Say so in the
    test's docstring.
19. `PRINT ~?&FEC0` reads a status byte with bit 6 set and bits 1 to 0 cycling, and
    `?&FEC0` is never `&20` again (the open-bus value that today's unmapped read gives).

## Documentation

- `docs/analogue-port.md`, new, in the shape of `docs/printer-port.md`: the hardware, the
  registers, the timing model with the measured constants, EOC and CB1, inputs and the
  unplugged decision, inspection, testing. Say plainly what is modelled and what is not.
- `docs/clock-architecture.md` line 98 already lists the region as 1 MHz; check it reads true.
- The three hardware policies' memory map comments gain the region in their region lists.
- `CLAUDE.md` "Fully Implemented" gets a line when round 4 lands, not now.

## Delivery

Commits in this order, each self-contained, each with its targeted tests green:

1. The chip and its unit tests (items 1 to 14).
2. The memory region, the CB1 plumbing, the peek path, and the Model B tests (15 to 17),
   replacing the open-bus assertions.
3. Inspection RPC with stubs regenerated (full gate from a separate worktree).
4. Python test (18, 19) and documentation.

Rules that apply: `git commit --only` on master, never amend, never push; no names of people
or host machines in files or commit text; 7-bit ASCII in test names and tags; no MOS
behaviour in the model, only in tests 15 to 19. Report each commit hash to the architect as
it lands, with the test output for anything that failed on the way and how it was fixed.
