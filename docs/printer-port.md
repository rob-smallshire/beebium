# The printer port

The BBC Micro's parallel printer port, how Beebium models it, and how a
printer-like device plugs into it. Companion to `docs/peripheral-extension-framework.md`
(the attachment-point mechanism) and `docs/via-peripherals.md` (the VIA
seam). The design discussion that led here is
`docs/discussion/printing-architecture.md`; the tracking issue is #99.

## Three concerns, three owners

| Concern | Owner | Where |
|---------|-------|-------|
| The port: the connector's electrical behaviour and timing | Beebium core | `PrinterPort`, `PrinterPortDevice`, the port A half of `UserViaPeripheral` |
| The sink: where the bytes go | Extensions on the `printer-port` attachment point | `file-printer` (built in), `tcp-printer`, `host-parallel-printer` |
| The printer: a command language, a character ROM, paper | Scriptorium, a separate project | reached over a raw TCP byte stream |

Beebium contains no printer-language code. A sink sees bytes and nothing
else, so the same port serves an ESC/P emulator, a capture file, a real
printer on the host, or a bare socket.

## The hardware

From the Advanced User Guide, chapter 24, and the Model B circuit diagram:

- PA0 to PA7 of the User VIA (6522 at &FE60) are buffered by a 74LS244 to
  the printer connector PL9. The buffer makes them output only: a device
  cannot drive the VIA's port A.
- CA2 is buffered to an open-collector output and is the printer /STROBE.
- CA1 is an input pulled to +5 V through 4K7 and is the printer /ACK.
- Nothing else is wired. There is no BUSY, no paper-out, no SELECT and no
  inbound data. A printer that is busy or absent simply never pulls /ACK
  low, and the MOS waits for it.

The MOS printer driver (`*FX5,1`) writes a byte to ORA, which pulses CA2 in
whatever mode PCR selects, then waits for the CA1 interrupt that /ACK's
active edge raises before sending the next byte. Software that drives the
port directly may use any CA2 mode (pulse, handshake or manual) and may
poll IFR instead of taking the interrupt. The port model therefore treats
the falling edge of CA2 as the strobe regardless of mode, and reports /ACK
as a level on CA1 for the VIA's own edge detector.

## The port model

`UserViaPeripheral` is the one `ViaPeripheral` installed on the User VIA.
Its port B half carries the User Port (`UserPort`, `UserPortDevice`). Its
port A half carries the printer port:

```cpp
// Interface for a device on the printer connector.
struct PrinterPortDevice {
    virtual ~PrinterPortDevice() = default;

    // The eight data lines after the buffer, whenever port A's output or
    // DDR changes. Bits whose DDR is input float high through the buffer.
    // Most devices ignore this and read the data at the strobe instead.
    virtual void update_data_lines(uint8_t data) { (void)data; }

    // Called every VIA tick (1 MHz), and again on a port A register write.
    // strobe is the level of /STROBE as the VIA drives it (CA2). The device
    // returns the level it drives on /ACK (CA1): 1 when idle (the pull-up),
    // 0 while asserted, on every call, because the VIA resets CA1 to 1
    // after each tick.
    virtual uint8_t update_control_lines(uint8_t strobe, uint8_t data) = 0;
};
```

The device does its own edge detection and timing in ticks, exactly as a
`UserPortDevice` does on CB1/CB2. The composite's port A half detects
nothing itself beyond passing the levels through; it is the connector, not
the printer.

`PrinterPort` is the handle an extension receives from
`ExtensionContext::get<PrinterPort>()`: `attach(PrinterPortDevice&)`,
throwing if occupied, and `is_occupied()`. The `printer-port` attachment
point has occupancy 0..1 and exists on every machine whose hardware
satisfies `HasPrinterPort`, which is every B-series model.

## The Centronics receiver

Every printer-like sink needs the same handshake behaviour, so it is
implemented once, in core, as `CentronicsReceiver`, a `PrinterPortDevice`:

- On the falling edge of /STROBE it latches the data lines.
- If its bounded queue has room it enqueues the byte, waits `ack_delay`
  ticks, then drives /ACK low for `ack_pulse` ticks. Defaults: 4 and 4
  microseconds, in the range of an Epson FX-80.
- If the queue is full it holds the byte and acknowledges only once the
  consumer has made room. The MOS is waiting for that acknowledge, so a
  slow consumer stalls the guest, never the host, and no byte is lost.
- `set_online(bool)`: while offline nothing is accepted and /ACK stays
  high, which is what an absent or switched-off printer looks like. The byte
  strobed while offline is held like a byte strobed into a full queue, and
  accepted and acknowledged once the receiver is online again, so a print
  that stalled on a switched-off printer resumes where it stopped.
- The queue is a single-producer, single-consumer lock-free queue. The
  emulation thread produces; the sink's own thread consumes with
  `try_dequeue` and `wait_dequeue_timed`. The emulation thread never
  touches host I/O.

A sink is then a thread that drains the receiver into a file, a socket or
a device node. The receiver's queue depth is the sink's "buffer" parameter.

## Sinks

### file-printer (built in)

Spools received bytes to a file. Built into the server, like
`host-serial`, so that `--file-printer` works without a plugin directory,
so that presets can name it unconditionally, and because it is the test
oracle for everything else on the port.

| Parameter | Type | Default | Meaning |
|-----------|------|---------|---------|
| `path` | string | required | File to write; created or truncated at attach |
| `line_ending` | `raw`, `lf`, `crlf` | `raw` | Translate the Beeb's CR line ends, or keep the bytes as sent |
| `buffer` | integer | 256 | Receiver queue depth in bytes |
| `online` | boolean | true | Start online; false models a printer that is switched off |

Typed API over the ExtensionRpc channel, service `FilePrinter`:
`GetStatus` returns path, bytes received, bytes written and online;
`SetOnline` sets the online state. A declarative sidebar panel shows the
same. Python and TypeScript bindings follow the `rpc_serial` layout.

Note on line ends: BASIC ends a line with LF then CR, and the MOS by
default (`*FX6,10`) suppresses LF to the printer. So `PRINT "HELLO"` reaches
the sink as `HELLO` followed by a single CR. `*FX6,0` lets the LF through,
in the Beeb's order: `HELLO` LF CR.
`line_ending=lf` turns a lone CR into LF and a CR LF pair into LF;
`crlf` turns a lone CR into CR LF and leaves CR LF alone.

### tcp-printer (plugin)

A raw TCP client to `host:port`, default port 9100, the JetDirect
convention. No protocol: the byte stream is all that crosses. While
disconnected the receiver is offline, so the guest waits as with a
switched-off printer. This is the link to Scriptorium and works against
`nc -l 9100`.

### host-parallel-printer (plugin)

Opens the host's printer device: `/dev/lp0` or `/dev/usb/lp0` on Linux,
`LPT1` or the USB printer class on Windows. A USB-to-parallel adapter and a
real port look the same to the operating system, so one extension covers
both. macOS has no such device node.
It has been verified only against stand-ins that keep the device-file
contract, a FIFO and a regular file, not against a physical printer.

## Testing the port

- C++: the port A half forwards data and levels to the attached device and
  leaves port B alone; `CentronicsReceiver` acknowledges after the
  configured delay, withholds acknowledge when full or offline, and
  releases the held byte when room appears.
- Integration, over the Python client against a Model B with
  `file-printer`: `*FX5,1` then `VDU 2 : PRINT "HELLO" : VDU 3` produces
  `HELLO` CR in the file; a BASIC program that sets DDRA, selects a CA2
  pulse mode, writes ORA and polls IFR sees the CA1 flag; with
  `online=false` a print longer than the MOS printer buffer stalls, and
  `SetOnline(true)` lets it finish with every byte in the file.
  Every wait polls the guest, never the wall clock.
