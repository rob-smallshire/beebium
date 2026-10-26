# Music 500 / Music 5000

Reference material for emulating the Acorn Music 500 and the Hybrid
Technology Music 5000, the external digital synthesisers that hang off
the BBC Micro's 1 MHz bus (FRED/JIM, &FC00-&FDFF). The emulation is
planned as a peripheral extension, probably a plugin, attaching to the
`1mhz-bus` attachment point alongside the SCSI host adapter, which will
also exercise more than one device sharing the 1 MHz bus.

This directory holds the primary sources: manuals, circuit descriptions,
register maps and links, placed here by hand. Nothing in it is code.
Design notes go in `docs/discussion/` once the material has been read;
the built subsystem's documentation will live beside the other
component documents in `docs/`.

See also:

- `docs/peripheral-extension-framework.md` for the `1mhz-bus` attachment
  point, `OneMHzBusDevice` and the FRED/JIM offset convention.
- `docs/howto_write_a_peripheral_extension.md` for the plugin shape.
- `docs/clock-architecture.md` and `BusStretching.hpp` for 1 MHz bus
  stretching, which any device on this bus is subject to.
- `docs/sound-subsystem.md` for the audio pipeline the synthesiser's
  output has to be mixed into.

## Links

(to be added)
