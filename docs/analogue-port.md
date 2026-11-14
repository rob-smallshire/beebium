# The analogue port and its converter

The BBC Micro's analogue-to-digital converter as Beebium builds it: the NEC
uPD7002 at &FEC0-&FEDF, its end-of-conversion line into the System VIA, and
the inspection RPC that reads it. This is round 2 of the analogue port work.
Round 3 adds the `AnaloguePort` handle, the `AnaloguePortDevice` seam and the
`analogue-port` attachment point, in the shape of the printer and user ports.
Round 4 adds the first device, a joystick adapter. The tracking issue is #98.

The specification is `docs/discussion/upd7002-adc-specification.md`. The
measurements behind the timing model come from the round 1 survey and from
`docs/discussion/analogue-port-hardware-trials.md`. The disc that runs those
trials on real hardware is built by `tools/hardware-trials/adc/`.

## What is modelled and what is not

| Modelled | Not modelled (yet) |
|---|---|
| The four registers, their mirror every four bytes across &FEC0-&FEDF, and the low byte repeated at offset 3 | The test mode at offset 3 (writes are ignored) |
| Status: EOC, busy, the result's two top bits (the previous result's during a conversion), mode, flag and channel | Anything the datasheet leaves unspecified beyond the trial results |
| The high-byte read resetting EOC; reads of the other offsets having no side effects | The read error when a read coincides with the end of a conversion |
| A write during a conversion restarting it | The integration itself: the input is sampled once, at the start write |
| Conversion time in 1 MHz clocks, as the measured sawtooth (12-bit) or line (8-bit) | Per-chip boundary shifts, the overlap band past each boundary, jitter and noise |
| EOC as a level on System VIA CB1, with the VIA doing the edge detection | The fire buttons on PB4 and PB5, which still read "not pressed" |
| No reset on Break; power-on state on a power cycle | The light pen strobe on CB2 (#210) |
| An undriven input reads the centre of the range, &8000 | A wandering, machine-dependent unplugged value |

Nothing in the model knows what any MOS does with the chip. MOS 1.20 appears
only in tests, as a test vector.

## The hardware

The uPD7002 is a dual-slope integrating converter with a four-channel
multiplexer. The BBC clocks it at 1 MHz: the 33 nF integrating capacitor C25
matches the datasheet's C_INT = 0.033/f_clk(MHz) uF, and the measured
conversion times fit 1 MHz arithmetic. Its reference, VREF, comes from three
diodes. The 15-way analogue port carries the four channel inputs, VREF,
analogue ground, the two fire buttons (System VIA PB4 and PB5) and the light
pen strobe. The chip has no reset pin and no power-on reset.

The Model B family decodes the chip at &FEC0-&FEDF on the 1 MHz bus. (The
Master 128 has it at &FE18-&FE1F; Beebium has no Master yet.)

## Registers

Offset is `address & 3`.

| Offset | Read | Write |
|---|---|---|
| 0 | Status | Latch channel, mode and flag, then start a conversion |
| 1 | Result high byte; resets EOC | Ignored |
| 2 | Result low byte; bits 3 to 0 read 0 | Ignored |
| 3 | Result low byte, as offset 2 | Test mode: ignored |

Status bits: 7 EOC as the pin (1 = not complete), 6 not busy, 5 and 4 the
result's two most significant bits, 3 mode (1 = 12-bit), 2 flag, 1 and 0
channel. A write stores bits 3 to 0; bits 7 to 4 are ignored. The flag has no
effect on the chip but is echoed.

The result is sixteen bits as the guest reads it. In 12-bit mode the top
twelve bits are the conversion code and the low four read 0. In 8-bit mode
the high byte is the 8-bit code and the low byte reads 0 (a decision trial 7
may revise).

At power-on the status reads &C0: idle, EOC inactive, not busy, control bits
clear, a zero result, CB1 high.

## Conversion

A write to offset 0 stores the control bits, samples the selected channel's
input level, works out the code and the conversion time, and starts counting.
The chip goes busy with EOC inactive. The result registers and status bits 5
and 4 keep showing the previous result until the conversion completes. On the
last clock the new result appears, busy clears and EOC goes active.

A write during a conversion abandons it and starts again from the new write
with a fresh sample.

### The timing model

Time is counted in 1 MHz clocks from the start write. The chip is a
`ClockSubscriber` at `ClockRate::Rate_1MHz` on the falling edge. Each hardware
policy ticks it through a `ClockBinding` from `poll_nmi()`, beside the disc
controller and the 1 MHz bus, which the machine calls on every 1 MHz cycle,
stretch cycles included. It never counts host time.

All the constants are in `upd7002_timing` in
`src/core/include/beebium/devices/Upd7002.hpp`, beside the measurements they
come from, so they can be re-fitted from new trial data without touching the
logic. For a conversion producing the 12-bit code `c`:

- **8-bit mode**: `t = 2950 + c / 2` clocks, one straight line from 2.95 ms to
  5.0 ms.
- **12-bit mode**: seven teeth. Each tooth rises 2 clocks per code from its
  start code `b` and start time `T`: `t = T + 2 (c - b)` for the tooth with
  the largest `b <= c`.

| Tooth | `b` (12-bit code) | `T` (clocks) |
|---|---|---|
| 1 | 0 | 9100 |
| 2 | 1020 | 10118 |
| 3 | 1452 | 9957 |
| 4 | 2052 | 10139 |
| 5 | 2628 | 10265 |
| 6 | 3204 | 10387 |
| 7 | 3796 | 10553 |

The table was digitised from conversion-time plots posted on Stardot (the
Beebium thread, post 492873) to about plus or minus 30 us. It predicts the
earlier Stardot measurements (thread 27458) on two other Model Bs to within
about 30 us. The model is deterministic: real chips move their boundaries by
tens of codes, and near a boundary a conversion sometimes takes the earlier
tooth's time. Trial 7 measures both on further machines.

## EOC and System VIA CB1

EOC is a level on System VIA CB1: 1 while idle or converting, 0 from
completion until the high byte is read or a new conversion starts.
`SystemViaPeripheral::update_control_lines` sets `cb1` from the chip's
`eoc_level()` on every VIA tick. The VIA does the edge detection under its
own PCR:

- Under negative-edge CB1 (MOS 1.20's PCR of &04), each completion sets IFR
  bit 4.
- Under positive-edge CB1, the high-byte read, or the next start write, sets
  it instead.

Both come out of the VIA's own logic; nothing in the chip looks for edges.

## Reset

Break (`soft_reset()`) does not touch the chip: a conversion in flight across
Break completes and drives CB1 low, and the freshly reset System VIA makes of
that what it will. A power cycle (`reset()`, which is also the debugger's
Reset) returns the chip to its power-on state. The input levels belong to
whatever drives the inputs and are kept.

## Inputs and the unplugged port

In this round the chip has a direct input seam,
`set_input_level(channel, level)`, used by the C++ tests. `level` is a 16-bit
fraction of VREF: 0 is analogue ground, &FFFF is VREF. The 12-bit code is
`level >> 4`. Round 3 moves the source of these levels behind the analogue
port's device seam; the chip's interface does not change.

With nothing driving a channel its level is &8000. Real machines read a
wandering, machine-dependent value with nothing plugged in: about &7300 on a
Master, and about &9500-&99E0 and &C800-&CBA0 on two Model Bs. There is no
single right value, and the centre upsets the fewest programs.

## Inspection

`DeviceInspection.GetAnalogueConverterState` reads the chip under quiesce
without side effects. It returns the status byte, the 16-bit result, the
channel, mode and flag, busy, the EOC level as CB1 sees it, the 1 MHz clocks
left in a conversion (0 when idle) and the four input levels. A machine
without a converter reports `present = false`.

The debugger's memory reads of &FEC0-&FEDF go to the chip's own `peek()`
through the memory map, so peeking the high byte does not reset EOC.

## Testing

| Suite | What it covers |
|---|---|
| `tests/test_upd7002.cpp` | The chip alone: power-on state, status echo, busy and EOC, the previous result's top bits, the 12-bit and 8-bit result layouts, the high-byte read, peek, restart, both timing models against the measured times, the undriven value, ignored writes |
| `tests/test_upd7002_model_b.cpp` | The mirror on all seven Model B family policies, peek through the machine, CB1 under both edges, the reset rules. With MOS 1.20 as a test vector: the first conversion (256 ms after power-on, once the MOS has cleared RAM), the AUG's ADC workspace (&2BA-&2BD) filling with each channel's level, and a CB1 edge every 11.3 ms while the MOS cycles the channels |
| `tests/test_device_inspection_adc.cpp` | The inspection RPC, including that it leaves EOC alone |
| `clients/beebium-python-client/tests/test_analogue_converter.py` | From BBC BASIC, loaded from a tokenised disc: `ADVAL(1)` to `ADVAL(4)` read &8000 with no fire button pressed, and `?&FEC0` reads the chip cycling all four channels in 12-bit mode, never the old open-bus &20 |

There is no Python-side input hook yet, so the Python tests see only
undriven inputs.
