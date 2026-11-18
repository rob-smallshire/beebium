# The analogue port and its device seam: round 3 specification (#98)

Round 2 built the uPD7002 chip (`upd7002-adc-specification.md`, `docs/analogue-port.md`).
This round gives the Model B family an **analogue port**: a port handle, a device interface and
an attachment point, in the shape of the printer port and the user port, so that a joystick
adapter (round 4) and a light pen (#210) are extensions attached to it. No device ships in
this round; the tests use stub devices.

## Principles

Those of round 2 apply: datasheet and hardware first, no MOS dependence in the model (MOS 1.20
only as a test vector), test first, `Decision` marks a choice this document makes.

Two more for the seam:

- **The port carries what the connector carries, and nothing else.** The 15-way D connector has
  the four channel inputs, VREF, analogue ground, the two button lines (System VIA PB4 and
  PB5), and the light pen strobe LPSTB (System VIA CB2 and the CRTC's light pen input). A
  device sees those and only those. It never sees the VIA's other lines or the chip's
  registers.
- **The chip stays ignorant of extensions.** `Upd7002` does not include anything under
  `extension/`. The port handle adapts between them.

## The hardware

| Pin | Signal | Where it goes |
|---|---|---|
| 15, 7, 12, 4 | CH0, CH1, CH2, CH3 | uPD7002 inputs 0 to 3 |
| 11, 14 | VREF | The chip's reference, about 1.8 V, from the diode chain |
| 5, 8 | Analogue ground | |
| 13 | PB0 (connector naming) | System VIA **PB4**, active low, pull-up |
| 10 | PB1 (connector naming) | System VIA **PB5**, active low, pull-up |
| 9 | LPSTB | System VIA **CB2** and the 6845's light pen strobe input |

A joystick drives CH0 (X) and CH1 (Y) for stick 1, CH2 and CH3 for stick 2, and grounds PB4
or PB5 when a fire button is pressed. A light pen drives LPSTB and one button line as its
switch. Paddles and other devices use the same lines in other ways. The connector takes one
plug, so the port has one device; a two-joystick adapter is one device.

## The device interface

`src/core/include/beebium/extension/AnaloguePortDevice.hpp`, `BEEBIUM_EXT_API`, no
`ViaPeripheral` inheritance, in the style of `PrinterPortDevice`:

```cpp
struct AnaloguePortDevice {
    virtual ~AnaloguePortDevice() = default;

    // The level the device drives on channel 0 to 3, as a 16-bit fraction of
    // VREF: 0 is analogue ground, &FFFF is VREF. Called by the converter when
    // it samples at the start of a conversion, so it is read at most once per
    // conversion and never on a tick. A channel the device leaves unconnected
    // returns UNPLUGGED_LEVEL.
    virtual uint16_t channel_level(int channel) = 0;

    // The levels the device drives on the two button lines, in place as the
    // System VIA reads them: bit 4 for PB4 (connector PB0) and bit 5 for PB5
    // (connector PB1), 1 = not pressed (the pull-up), 0 = pressed. Other bits
    // are ignored. Called whenever the System VIA reads or updates port B.
    virtual uint8_t button_lines() { return 0x30; }

    // The level the device drives on LPSTB: 1 while the strobe is asserted,
    // 0 otherwise. Called on every VIA tick; the VIA's own edge detector and
    // the CRTC's own latch see the transitions.
    virtual uint8_t light_pen_strobe() { return 0; }

    static constexpr uint16_t UNPLUGGED_LEVEL = 0xCAFE;
};
```

`Decision`: the button lines are inputs only. MAME also passes port B outputs to the card for
devices such as the Bitstik that drive these lines the other way. No such device is in scope,
and adding `update_button_lines(output, ddr)` later is additive. `Decision`: LPSTB is in the
interface now, since it costs nothing and #210 needs exactly this seam; the wiring to CB2 and
the CRTC is in this round, the pen itself is not.

## The chip's input seam

`Upd7002` gains a small source interface declared in its own header:

```cpp
struct AnalogueInputSource {
    virtual ~AnalogueInputSource() = default;
    virtual uint16_t level(int channel) = 0;
};
void set_input_source(AnalogueInputSource* source);  // nullptr: the chip's own levels
```

When a source is set, `start_conversion` samples `source->level(channel)` instead of the
chip's `input_levels_`. `set_input_level` and the stored levels stay for the chip's unit tests
and for a machine with no device attached, where they keep reading `UNPLUGGED_LEVEL`. The
inspection RPC's `input_levels` report what the chip would sample now: the source's levels
when a source is set.

## The port handle

`src/core/include/beebium/extension/AnaloguePort.hpp`, in the shape of `PrinterPort`:

```cpp
class AnaloguePort {
public:
    AnaloguePort(SystemViaPeripheral& system_via_peripheral, Upd7002& converter);
    void attach(AnaloguePortDevice& device);  // throws std::runtime_error if occupied
    bool is_occupied() const;
};
template<typename T>
concept HasAnaloguePort = requires(T& hw) {
    { hw.analogue_port() } -> std::convertible_to<AnaloguePort&>;
};
```

`attach` gives the System VIA peripheral the device for its button lines and LPSTB, and gives
the chip an input source that forwards to `device.channel_level`. The adapter object lives in
the port handle. Every Model B family policy constructs an `AnaloguePort analogue_port_` over
its `system_via_peripheral` and `analogue_converter_` and exposes `analogue_port()`.

## The System VIA side

`SystemViaPeripheral`:

- `update_port_b` today returns bits 4 and 5 as 1. It now returns the device's
  `button_lines() & 0x30` when a device is attached, 0x30 otherwise. Nothing else in port B
  changes.
- `update_control_lines` today leaves `cb2` alone. With a device attached it sets
  `cb2 = device.light_pen_strobe()`. `Decision`: with no device attached, `cb2` stays
  untouched, exactly as today, so an empty port changes nothing observable.
- CB1 is unchanged from round 2.

## The CRTC side

`Crtc6845::tick(bool lightpen)` already latches R16/R17 on the strobe's rising edge, and no
caller passes `true`. The call is `hardware.crtc.tick()` in `VideoBinding.hpp`, which has the
hardware policy in hand, so it passes the port's LPSTB level (the device's `light_pen_strobe()`,
0 with no device) through an accessor on the policy; the policies without an analogue port
keep passing `false`.

## The attachment point

- `AttachmentPointCatalogue.hpp`: `{"analogue-port", "Analogue Port", "The 15-way analogue
  port, for joysticks, paddles and a light pen.", 0, 1}`.
- `PresetResolution.hpp` `machine_attachment_points`: push `"analogue-port"` when
  `HasAnaloguePort<Memory>`.
- `MachineAssembly.hpp`: pass the port into `ExtensionContext` as the others are passed;
  `ExtensionContext::get<AnaloguePort>()` and `has<AnaloguePort>()`.
- `docs/peripheral-extension-framework.md`: the built-in points list moves `analogue-port`
  from "future" to present.

No proto change in this round. The point is visible through the existing
`PeripheralExtensionService` discovery, and `AnalogueConverterState` already reports the
levels. `Decision`: no new inspection fields; occupancy is the extension framework's to report.

## Tests

### Catch2, `tests/test_analogue_port.cpp` (shape of `test_printer_port.cpp`)

Tag `[analogue_port]`, 7-bit ASCII names. A `StubAnalogueDevice` records calls and returns
settable levels, button bits and strobe.

1. Every Model B family policy has `analogue_port()`, and `machine_attachment_points` for each
   lists `"analogue-port"`.
2. `attach` succeeds once; a second `attach` throws `std::runtime_error`; `is_occupied`
   reports both states.
3. With a stub attached, a 12-bit conversion on each channel produces the stub's level for that
   channel (`&1234` gives high `&12`, low `&30`), and the stub's `channel_level` was called
   exactly once per conversion, with the channel written.
4. With no device, conversions read `&8000` as before (the round 2 test still passes).
5. Button lines: the stub returning `0x20` (PB4 pressed) makes a read of System VIA port B
   with DDR bits 4 and 5 as inputs return bit 4 clear and bit 5 set; `0x30` returns both set;
   the remaining port B bits are unchanged from the no-device case.
6. LPSTB to CB2: with PCR selecting CB2 as a negative-edge input, the stub raising then dropping
   the strobe sets IFR bit 3; with no device, IFR bit 3 never sets over a second of emulated
   time.
7. LPSTB to the CRTC: raising the strobe during a frame latches R16/R17 to the character
   address the CRTC is at on that tick (compare with the CRTC's own address counter in the
   same test); with no device, R16/R17 never change.
8. `ExtensionContext::get<AnaloguePort>()` returns the machine's port and `has<>()` is true
   on the Model B; a context built without one throws the same sentence shape as the printer
   port's.
9. Inspection: with a stub attached, `GetAnalogueConverterState.input_levels` are the stub's
   levels.

### Catch2, Model B with MOS 1.20 as the test vector

10. With a stub returning distinct levels on all four channels, the MOS's ADC workspace
    (`&2BA` to `&2BD`) holds each channel's high byte after a second (the round 2 test 16 with
    the device seam in place of `set_input_level`).
11. With the stub pressing PB4 then PB5, OSBYTE &80 with X=0 (as `ADVAL(0)`) returns the
    button bits the AUG documents, read through the MOS workspace or a CALL from a disc-loaded
    BASIC program if simpler.

### Python

12. Through the Python client's `PeripheralExtensionService` wrapper, the attachment points of
    a Model B include `analogue-port` with display name "Analogue Port" and occupancy 0..1.
    There is no existing Python test of the point listing for the other ports, so this is the
    first; keep it small and name the file after the port.

## Documentation

- `docs/analogue-port.md` gains "The port model" (the handle, the device interface, the
  button lines and LPSTB wiring, the no-device behaviour) and "Writing a device", and its
  "What is modelled" table moves the fire buttons and LPSTB wiring to the modelled column.
- `docs/peripheral-extension-framework.md` as above.
- `docs/howto_write_a_peripheral_extension.md`: a sentence on `get<AnaloguePort>()` where the
  other ports are listed.

## Delivery

Commits, each with targeted tests green:

1. `AnaloguePortDevice`, `AnalogueInputSource` in the chip, `AnaloguePort`, the System VIA and
   CRTC wiring, policies' `analogue_port()`, tests 1 to 7 and 10 to 11.
2. Catalogue, preset resolution, `ExtensionContext`, `MachineAssembly`, tests 8, 9 and 12,
   documentation.

Full gate from a separate worktree build before reporting the round done. Same rules as
round 2: `git commit --only`, never amend, never push, no people or host names, 7-bit ASCII in
test names and tags, stop and ask if a `Decision` proves wrong.
