# The analogue port and its converter

The BBC Micro's analogue port and the converter behind it as Beebium builds
them: the NEC uPD7002 at &FEC0-&FEDF, its end-of-conversion line into the
System VIA, the inspection RPC that reads it, and the port's handle, device
seam and `analogue-port` attachment point, in the shape of the printer and
user ports. The first device, a joystick adapter, comes next. The tracking
issue is #98.

The specifications are `docs/discussion/upd7002-adc-specification.md` (the
chip) and `docs/discussion/analogue-port-seam-specification.md` (the port). The
measurements behind the timing model come from the round 1 survey and from
`docs/discussion/analogue-port-hardware-trials.md`. The disc that runs those
trials on real hardware is built by `tools/hardware-trials/adc/`.

## What is modelled and what is not

| Modelled | Not modelled (yet) |
|---|---|
| The four registers, their mirror every four bytes across &FEC0-&FEDF, and the low byte repeated at offset 3 | The test mode at offset 3 (writes are ignored) |
| Status: EOC and busy, and bits 5 to 0 (the result's two top bits, mode, flag and channel) latched at completion, so the previous conversion's while one runs | Anything the datasheet leaves unspecified beyond the trial results |
| The high-byte read resetting EOC; reads of the other offsets having no side effects | The read error when a read coincides with the end of a conversion |
| A write during a conversion restarting it | The integration itself: the input is sampled once, at the start write |
| Conversion time in 1 MHz clocks, as the measured sawtooth in both modes | Per-chip boundary shifts, the overlap band past each boundary, jitter and noise; the lower accuracy of 8-bit mode |
| EOC as a level on System VIA CB1, with the VIA doing the edge detection | A device driving the button lines as outputs (the Bitstik does) |
| No reset on Break; power-on state on a power cycle | A light pen device (#210); the strobe is wired, the pen is not |
| An undriven input reads the centre of the range, &8000 | A wandering, machine-dependent unplugged value |
| The port: one device's channel levels, button lines (PB4, PB5) and light pen strobe (CB2 and the CRTC) | Any shipped device; the tests use stubs |

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

Bits 5 to 0 describe the conversion whose result is showing. A start write
changes only bits 7 and 6; the mode, flag and channel bits, like the two top
result bits, keep the previous conversion's values until the new one
completes. A Master AIV's chip showed this in trial 2 (just after writing &0E
the status still carried the previous conversion's 8; E appeared at
completion), and the datasheet marks D5 to D0 "previous conversion data".

The result is sixteen bits as the guest reads it, laid out the same in both
modes: the 12-bit conversion code in the top twelve bits, the low four 0.
Real chips return low bytes such as &30, &B0 and &F0 in 8-bit mode, the same
twelve bits only less accurately; the inaccuracy is not modelled.

At power-on the status reads &C0: idle, EOC inactive, not busy, control bits
clear, a zero result, CB1 high.

## Conversion

A write to offset 0 stores the control bits, samples the selected channel's
input level, works out the code and the conversion time, and starts counting.
The chip goes busy with EOC inactive. The result registers and status bits 5
to 0 keep showing the previous conversion until this one completes. On the
last clock the new result and control bits appear, busy clears and EOC goes
active.

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

- **8-bit mode**: two teeth, each rising half a clock per code:
  `t = T + (c - b) / 2` for the tooth with the largest `b <= c`, with
  `{b, T}` = `{0, 2945}` and `{3440, 4190}`. That is 2.95 ms at code 0
  rising to about 4.66 ms, then a drop of about 0.43 ms. The table is the
  Master AIV chip's (trial 7 sweep B), the only measurement with a drop; its
  top bin, codes 3968 and above near 4221 clocks, is open and not modelled.
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

The 12-bit table was digitised from conversion-time plots posted on Stardot
(the Beebium thread, post 492873) to about plus or minus 30 us. It predicts
the earlier Stardot measurements (thread 27458) on two other Model Bs to
within about 30 us.

A second chip, in a Master AIV (`tools/hardware-trials/adc/results/2026-11-14-master-aiv/`),
has the same 12-bit shape: every tooth at 1.99 to 2.01 clocks per code, drops
of 960 to 984 clocks, code 0 at about 9092. Its boundaries are elsewhere: 832,
1520, 1888, 2464 and 3056 (and one more beyond the sweep, between 3614 and
4095), with tooth start times 9091, 9728, 10079, 9792, 9919 and 10079. Its
regular run sits about 160 codes below the Model B chip's. Both tables are in
the `upd7002_timing` comments. The default 12-bit table stays the Model B
chip's, which is complete.

The model is deterministic: real chips move their boundaries from chip to
chip, and near a boundary a conversion sometimes takes the earlier tooth's
time (0 to 21 of about 400 points per tooth on the Master's chip). A
per-chip parameter waits for a second Model B sweep. One figure is open: on
the Master, trial 1 timed codes 0 to 4 at 10.0 ms in 12-bit mode, where its
sweep gives 9.09 ms.

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

The chip samples an input at the start of each conversion. Where the level
comes from is an `AnalogueInputSource`, declared beside the chip: the
analogue port gives the chip one that forwards to the attached device, so
the chip includes nothing from `extension/`. With no source the chip uses
its own stored levels, set with `set_input_level(channel, level)` (the C++
unit tests do). `level` is a 16-bit fraction of VREF: 0 is analogue ground,
&FFFF is VREF. The 12-bit code is `level >> 4`.

With nothing driving a channel its level is &8000. Real machines read a
wandering, machine-dependent value with nothing plugged in: about &7300 on a
Master, and about &9500-&99E0 and &C800-&CBA0 on two Model Bs. There is no
single right value, and the centre upsets the fewest programs.

## The port model

`AnaloguePort` (`src/core/include/beebium/extension/AnaloguePort.hpp`) is the
handle, owned by every Model B family hardware policy over its
`SystemViaPeripheral` and its `Upd7002`, and exposed as `analogue_port()`
(`HasAnaloguePort` detects it). The port is one connector, so it takes one
device: `attach(device)` a second time throws, and `is_occupied()` reports
whether one is there. Attaching:

- gives the converter an input source that asks the device's
  `channel_level(channel)`, once per conversion, at the start write;
- gives the System VIA peripheral the device for port B: its
  `button_lines()` in bits 4 and 5 (PB4 and PB5, which the connector calls PB0
  and PB1), active low, the other port B bits unchanged;
- drives System VIA CB2 from the device's `light_pen_strobe()` on every VIA
  tick, and passes the same level to the CRTC's light pen input on every
  character clock through `VideoBinding`, so the VIA's edge detector and the
  CRTC's R16/R17 latch both see the strobe's edges.

With no device attached nothing observable changes: PB4 and PB5 read 1 (the
pull-ups), CB2 is left as the VIA has it, the CRTC sees no strobe, and every
channel reads the undriven &8000.

The point is `analogue-port` in the attachment point catalogue, "Analogue
Port", occupancy 0..1. Machines with the port list it among their own
points, and an extension reaches the handle with
`ExtensionContext::get<AnaloguePort>()` (and `has<AnaloguePort>()`).

## Writing a device

A device implements `AnaloguePortDevice`
(`src/core/include/beebium/extension/AnaloguePortDevice.hpp`) and attaches in
its `init()`:

```cpp
void init(beebium::ExtensionContext& ctx) override {
    ctx.get<beebium::AnaloguePort>().attach(device_);
}
```

- `channel_level(channel)`: the level on channel 0 to 3 as a fraction of VREF.
  A joystick puts stick 1 on channels 0 (X) and 1 (Y) and stick 2 on 2 and 3,
  low for right and down. Return `UNDRIVEN_LEVEL` for a channel the device
  does not drive.
- `button_lines()`: bits 4 and 5 as the VIA reads them, 0 while a button is
  pressed. The default is 0x30, nothing pressed.
- `light_pen_strobe()`: 1 while the strobe is asserted. The default is 0.

All three run on the emulation thread (`channel_level()` also from
inspection, with the emulation paused). `button_lines()` and
`light_pen_strobe()` run on every System VIA tick and CRTC character clock,
so a device answers them from plain members, an atomic or a value cached
from elsewhere, and never touches a host API or takes a lock there. A device
that takes input from a client or a host device updates those members from
its own thread.

## Inspection

`DeviceInspection.GetAnalogueConverterState` reads the chip under quiesce
without side effects. It returns the status byte, the 16-bit result, the
channel, mode and flag as last written, busy, the EOC level as CB1 sees it,
the 1 MHz clocks left in a conversion (0 when idle) and the four input
levels, which are what the chip would sample now: the device's levels when
one is attached. A machine without a converter reports `present = false`.

The debugger's memory reads of &FEC0-&FEDF go to the chip's own `peek()`
through the memory map, so peeking the high byte does not reset EOC.

## Testing

| Suite | What it covers |
|---|---|
| `tests/test_upd7002.cpp` | The chip alone: power-on state, the control bits latched at completion, busy and EOC, the previous result's top bits, the result layout in both modes, the high-byte read, peek, restart, both timing tables against the measured times, the undriven value, ignored writes |
| `tests/test_upd7002_model_b.cpp` | The mirror on all seven Model B family policies, peek through the machine, CB1 under both edges, the reset rules. With MOS 1.20 as a test vector: the first conversion (256 ms after power-on, once the MOS has cleared RAM), the AUG's ADC workspace (&2BA-&2BD) filling with each channel's level, and a CB1 edge every 11.3 ms while the MOS cycles the channels |
| `tests/test_analogue_port.cpp` | The port: every policy's handle, attach-once, a conversion sampling the device once per conversion, the no-device level, the button lines on PB4 and PB5, the strobe into CB2 and into the CRTC's R16/R17, `ExtensionContext`. With MOS 1.20 as a test vector: the ADC workspace filling from the device, and OSBYTE &80 reporting its fire buttons |
| `tests/test_device_inspection_adc.cpp` | The inspection RPC, including that it leaves EOC alone and reports a device's levels |
| `tests/test_preset_resolution.cpp`, `tests/test_attachment_points.cpp` | Every Model B family machine offers `analogue-port`; the catalogue entry |
| `clients/beebium-python-client/tests/test_analogue_converter.py` | From BBC BASIC, loaded from a tokenised disc: `ADVAL(1)` to `ADVAL(4)` read &8000 with no fire button pressed, and `?&FEC0` reads the chip cycling all four channels in 12-bit mode, never the old open-bus &20 |

`clients/beebium-python-client/tests/test_analogue_port.py` checks the
point through the client's attachment-point listing. There is no Python-side
input hook yet, so the Python tests see only undriven inputs.
