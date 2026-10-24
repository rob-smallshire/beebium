# The printer port

The BBC Micro's parallel printer port as Beebium builds it: the connector
and its handshake in the core, and the extensions that take the bytes
somewhere, to a file, a directory of print jobs, a network socket or a
printer device on the host. Companion to
`docs/peripheral-extension-framework.md` (attachment points and the
extension mechanism) and `docs/via-peripherals.md` (the VIA seam). The
design discussion that led here is `docs/discussion/printing-architecture.md`;
the tracking issue is #99.

## Three concerns, three owners

| Concern | Owner | Where |
|---------|-------|-------|
| The port: the connector's electrical behaviour and timing | Beebium core | `PrinterPort`, `PrinterPortDevice`, `CentronicsReceiver`, the port A half of `UserViaPeripheral` |
| The sink: where the bytes go | Extensions on the `printer-port` attachment point | `file-printer` and `job-file-printer` (built in), `tcp-printer` and `host-parallel-printer` (plugins) |
| The printer: a command language, a character ROM, paper | Something outside Beebium | a real printer, or a printer emulation server such as Papyrium, reached over a raw TCP byte stream |

Beebium contains no printer-language code. A sink sees bytes and nothing
else, so the same port serves a capture file, a real printer on the host,
a bare socket, or a printer emulator at the other end of that socket.

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

Everything a Beebium sink can report back to the guest is therefore one
bit: acknowledge, or not yet.

### The MOS printer driver

How the guest reaches the port, for anyone writing a test or a preset:

- `*FX5,1` selects the parallel printer (`*FX5,2` is the serial printer,
  which is the RS423 port's business, not this document's).
- `VDU 2` turns the printer on and `VDU 3` turns it off; `VDU 1,n` sends
  byte `n` to the printer only, which is how a control code such as form
  feed (`VDU 1,12`) gets past the VDU driver.
- The MOS keeps a 64-byte printer buffer at &880 to &8BF. Characters go
  into it from the VDU driver; the printer driver takes one out, writes it
  to ORA, and waits for the CA1 interrupt that /ACK raises before sending
  the next. When the buffer is full the VDU driver waits, so a stalled
  printer stalls the program that is printing. `*FX21,3` empties the
  buffer.
- `*FX6,n` names the character the MOS does not send to the printer; the
  default is 10, so BASIC's LF CR newline reaches the printer as CR alone.
  `*FX6,0` lets the LF through, in the Beeb's order: LF then CR.
- `*FX3` selects output streams and can also route output to the printer.

Software that drives the port directly may use any CA2 mode (pulse,
handshake or manual) and may poll IFR instead of taking the interrupt.
The port model therefore treats the falling edge of CA2 as the strobe
regardless of mode, and reports /ACK as a level on CA1 for the VIA's own
edge detector. One quirk worth knowing: `STA (zp),Y` performs a dummy read
of ORA, which in pulse mode is itself a strobe.

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
satisfies `HasPrinterPort`, which is every B-series model. The port has
no indicator: a printer port has no lamp.

## The Centronics receiver

Every printer-like sink needs the same handshake behaviour, so it is
implemented once, in core, as `CentronicsReceiver`, a `PrinterPortDevice`:

- On the falling edge of /STROBE it latches the data lines. A second
  strobe while a byte is still held replaces it: the Beeb has no BUSY line,
  and well-behaved software waits for /ACK first.
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
a device node. The receiver's queue depth is the sink's `buffer`
parameter, 256 bytes by default. The counters every sink reports come from
here: `bytes_received` is what the receiver accepted from the port;
`bytes_written` or `bytes_sent` is what the sink has handed on.

### Busy and offline

Two states reach the guest, and they look the same to it, a withheld /ACK.
They differ in what a sink does with bytes it has already accepted:

| State | Receiver | Bytes already queued | Which sinks |
|-------|----------|----------------------|-------------|
| Busy | online, queue full | drain as the far side takes them | every sink, when its output cannot keep up |
| Offline | `set_online(false)` | kept, written after the sink is back online | `tcp-printer` while not connected; `host-parallel-printer` while its device is not open; `file-printer` through its test hook |

A sink goes offline only when its output has gone, a closed socket or a
vanished device, never because the output is slow. Bytes the guest has
already had acknowledged are never dropped on the way to the output; they
wait for the reconnect or the reopen.

## Sinks

All four sinks share the shape:

- Configured on the command line as `--<name> key=value:key=value`, or in
  a preset as an entry of `extensions` with `name` and `config`:

  ```json
  { "name": "tcp-printer", "config": { "host": "printer.local", "port": 9100 } }
  ```

- Status over the ExtensionRpc channel as a typed service named after the
  sink (`FilePrinter`, `JobFilePrinter`, `TcpPrinter`,
  `HostParallelPrinter`), with a `GetStatus` method. The Python client
  exposes them under `beebium.ext.peripheral.<name>` and the TypeScript
  client as `Beebium.filePrinter`, `jobFilePrinter`, `tcpPrinter` and
  `hostParallelPrinter`.
- A declarative sidebar panel showing the same status. None has controls:
  there is no online toggle, because a printer connected and switched off
  is not a situation anyone asks for.
- No front-end code of its own. The New Machine dialog, the Peripherals
  sidebar and the status panel are all built from the manifest and the
  panel declaration.

### file-printer (built in)

Spools received bytes to a file. Built into the server, like
`host-serial`, so that `--file-printer` works without a plugin directory,
so that presets can name it unconditionally, and because it is the test
oracle for everything else on the port.

| Parameter | Type | Default | Meaning |
|-----------|------|---------|---------|
| `path` | filepath (write) | required | File to write; an existing file is replaced |
| `line_ending` | `raw`, `lf`, `crlf` | `raw` | Keep the bytes as sent, or translate the Beeb's CR line ends to LF or CR LF |
| `buffer` | integer | 256 | Receiver queue depth in bytes |

`path` declares write access, so a configurator offers a Save panel for
it rather than an Open panel. `GetStatus` returns path, bytes received,
bytes written and online.

**`SetOnline` is a test hook**, not a user setting. It exists to exercise
the port's switched-off-printer behaviour: the receiver holds the byte the
BBC strobed and withholds /ACK, so the guest waits; `SetOnline(true)`
accepts the byte and the print resumes. Only file-printer has it, as the
test oracle for the port.

Note on line ends: with the MOS default (`*FX6,10`) `PRINT "HELLO"`
reaches the sink as `HELLO` followed by a single CR; with `*FX6,0` it is
`HELLO` LF CR. `line_ending=lf` turns a lone CR into LF and a CR LF pair
into LF; `crlf` turns a lone CR into CR LF and leaves CR LF alone. The
translator is stateful across the pair, one per output stream.

### job-file-printer (built in)

Writes one file per print job into a directory. A job is a run of bytes
separated from the next by quiet: it begins with the first byte after
quiet and ends when no byte has been accepted for `idle` seconds of
emulated time. The output is the replay record a printer emulator wants,
and a plain record for anyone else.

| Parameter | Type | Default | Meaning |
|-----------|------|---------|---------|
| `directory` | dirpath | required | Directory to write job files into; created if missing |
| `idle` | integer (seconds) | 5 | Quiet, in emulated time, that ends a job; at least 1 |
| `line_ending` | `raw`, `lf`, `crlf` | `raw` | As file-printer, translated afresh in each job |
| `buffer` | integer | 256 | Receiver queue depth in bytes |

Quiet is counted in VIA ticks, which advance only while the machine runs:
the device attached to the port passes every call through to the
`CentronicsReceiver` and counts the ticks since it last accepted a byte.
So a paused machine, a debugger stop or host sleep in the middle of a job
does not split it, and at real speed the two clocks agree. When a job has
been quiet for `idle`, the emulation thread hands the sink thread the
count of bytes received by the job's end, and the sink closes the job
file once it has written that many. Form feed is a page boundary, not a
job boundary. Shutting the machine down, or detaching the extension,
writes what is queued and closes the open job.

Job files are named `job-<YYYYMMDDThhmmssZ>.prn` in UTC, stamped when the
job's first byte is written, so they sort by when printing began. A job
that begins in the same second as an existing file gets `-2`, `-3`... before
the extension; an existing file is never overwritten. Bytes are written
and flushed as they arrive, not held until the job ends, so a long job is
visible while it prints and a crash loses nothing.

`GetStatus` returns the directory, the current job file (empty between
jobs), bytes in the current job, jobs completed and total bytes written.

### tcp-printer (plugin): a network printer over a raw socket

A TCP client to `host:port`. The default port is 9100, the convention HP
JetDirect established and that nearly every network printer and print
server since has followed: open a socket, send the bytes you would have
sent down a parallel cable, close it. No protocol: the byte stream is all
that crosses. Beebium therefore talks to a real network printer, to
`nc -l 9100` on a laptop, or to a printer emulation server such as
Papyrium, which renders what an Epson or a daisywheel would have put on
paper, in exactly the same way.

| Parameter | Type | Default | Meaning |
|-----------|------|---------|---------|
| `url` | string | | `[scheme://]host:port`, e.g. `socket://printer.local:9100`; alternative to `host`/`port` |
| `host` | string | `localhost` | Printer hostname or address |
| `port` | integer | 9100 | Printer TCP port |
| `buffer` | integer | 256 | Receiver queue depth in bytes |
| `reconnect_ms` | integer | 1000 | Interval between connection attempts while not connected; 0 makes one attempt and never retries |

The sink owns two threads. A connection thread owns the socket's
lifecycle: it connects (each attempt bounded by a 2 s timeout), then sits
in a short read on the socket, discarding anything that arrives, purely to
notice the peer closing. A writer thread drains the receiver and writes.
The receiver is online exactly while the socket is connected, so:

- Before the first connection, and after a drop, the guest waits for /ACK
  as it would for a switched-off printer. Bytes already acknowledged stay
  queued and go out on the next connection; the unsent remainder of a
  write that failed is kept, not lost.
- With `reconnect_ms` at its default the sink keeps trying every second
  for as long as the machine runs. A printer switched on late, or a
  printer emulator started after the machine, is picked up without any
  action in the guest.
- A write the kernel cannot take (EAGAIN, the socket buffer full) is
  retried after 2 ms; that is busy, not offline, and nothing goes
  offline because the network is slow.

`GetStatus` returns the endpoint (an IPv6 host in brackets), connected,
bytes received and bytes sent. The panel shows the endpoint, "Connected"
or "Not connected", and the counts.

#### Printing to a real network printer

A modern office printer on the LAN typically listens on 9100 as well as
on LPD and IPP, and its raw port accepts whatever page-description
languages the printer implements. An Epson inkjet on the project's LAN
advertises ESC/P2 among them, which is a superset of the FX-80's ESC/P, so
what a Beeb prints for an Epson dot-matrix printer is, for plain text and
most of the common escapes, what the inkjet expects. Two things to know
from sending it bytes:

- A page is not ejected until the printer sees a form feed, so a listing
  that ends without one sits in the printer. End a print with `VDU 1,12`.
- The printer wants a line feed to move down a line. The MOS default
  (`*FX6,10`) strips it, so every line prints over the last; `*FX6,0`
  before printing lets the LF through.

Raw text to that printer's port 9100 was confirmed accepted end to end
(the printer fed a page); the page itself was blank for a reason that had
nothing to do with the bytes, blocked heads. A print from a Beebium
Model B to it through `tcp-printer` is the one check still outstanding.

### host-parallel-printer (plugin): the host's own printer port

Opens the host's printer device and writes to it through the operating
system's plain device-file contract. Nothing is configured on the device
(it is not a serial port) and nothing is read from it.

| Parameter | Type | Default | Meaning |
|-----------|------|---------|---------|
| `device` | string | required | Linux: `/dev/usb/lp0` (usblp) or `/dev/lp0` (parport). Windows: a USB printer's `\\?\usb#...` interface path, or `LPTn` |
| `buffer` | integer | 256 | Receiver queue depth in bytes |
| `reconnect_ms` | integer | 1000 | Interval between attempts to open the device while it is not open; 0 makes one attempt and never retries |

Platforms:

- **Linux**: the `usblp` driver gives a USB printer-class device
  `/dev/usb/lpN`; a legacy parallel port with `parport` gives `/dev/lpN`.
  The nodes are `root:lp` mode 0660, so the user running the server
  needs to be in the `lp` group. A USB-to-parallel adapter and a real port
  look the same to the driver, so one extension covers both.
- **Windows**: a USB printer is not reachable as `LPTn`, and the `USB001`
  name the Printers dialog shows is a spooler port, not a device. The
  extension enumerates the USB printer class interface
  (`GUID_DEVINTERFACE_USBPRINT`) through SetupDi and opens the `\\?\usb#...`
  path with `CreateFileW`. A bare `LPTn` is opened as `\\.\LPTn`.
- **macOS**: no device node exists for a USB printer; raw access needs
  IOKit bulk writes to the printer-class interface. Not built (#213).

`ListDevices`, the second method on the `HostParallelPrinter` service,
enumerates what is present now (the lp nodes on Linux, the USBPRINT
interfaces on Windows), each with a path to give as `device` and a
friendly name. It only enumerates; it opens nothing.

The sink's writer thread opens the device, drains the receiver and
writes with a short timeout. What the device reports decides the
receiver's state:

- A device that fails to open (ENXIO, ENOENT, ENODEV on POSIX; any
  `CreateFile` failure on Windows) is not there: the receiver is offline
  and the sink retries the open every `reconnect_ms`.
- A write that fails with EPIPE, EIO, ENODEV or the like means the device
  has gone: close it, offline, retry the open. SIGPIPE is blocked on the
  writer thread so a FIFO stand-in whose reader has gone reports EPIPE
  rather than killing the process.
- EAGAIN, or a poll that never reports writable, is a busy printer: the
  receiver stays online, the write is retried, and the guest waits for
  /ACK. This is the common case with real hardware, see below.

`GetStatus` returns the device, open, bytes received and bytes sent.
`bytes_sent` counts bytes handed to the device file; for a busy printer
the kernel may still be holding them.

#### USB to IEEE 1284 adapters

The hardware this extension is for on a modern machine is a USB-to-
parallel cable: a USB printer-class device at one end, a 36-way
Centronics plug or a 25-way D-type at the other. Two were tried on a
Raspberry Pi 5 running Debian, neither with a printer behind it:

| Adapter | Chip | USB id | Linux sees |
|---------|------|--------|------------|
| StarTech ICUSB1284, 36-way Centronics | Prolific PL2305 "IEEE-1284 Controller" | 067b:2305 | `/dev/usb/lp0`, bidirectional printer, protocol 2 |
| An unbranded cable, 25-way D-type | QinHeng CH340S "USB2.0-Print" | 1a86:7584 | `/dev/usb/lp1`, likewise |

What they do with no printer attached, which is also what a switched-off
printer behind them looks like:

- Both accept one write of 4096 bytes instantly and then return EAGAIN
  from every further write, with `poll` never reporting the device
  writable, for as long as nothing drains them. The adapter has a buffer
  of its own and the `usblp` driver has one bulk transfer in flight; once
  both are full the device is simply busy, indefinitely. This is the
  right answer: a parallel printer that is not ready holds BUSY high, and
  the Beeb, which cannot see BUSY, waits for the /ACK that never comes.
- The Prolific adapter reports its port status (paper out, selected, no
  error) through the printer-class status request; the CH340S fails that
  request with EIO and returns a string of repeated characters as its
  IEEE 1284 device ID. The extension consults neither, so neither matters.
- A soft reset of the USB device (de-authorising and re-authorising it in
  sysfs) re-enumerates the node but does not empty the adapter's buffer.
  Only unplugging the adapter, or a printer draining it, clears a jam.

Driven from a Beebium Model B on that machine, with each adapter in turn:
`ListDevices` named both nodes; the device opened and stayed open
throughout; a 300-line BASIC print stalled the guest part way through its
eighth line with 258 bytes received (the 256-byte receiver queue plus two
in flight) and one byte handed to the kernel; and shutting the machine
down with the writer thread parked on a busy device took well under a
second. The data path onto paper is still verified only against stand-ins
that keep the device-file contract, a FIFO and a regular file; the next
step is the same adapter with a printer on the end of it.

## Testing the port

- C++: the port A half forwards data and levels to the attached device and
  leaves port B alone; `CentronicsReceiver` acknowledges after the
  configured delay, withholds acknowledge when full or offline, and
  releases the held byte when room appears. Each sink has a unit test
  against its own stand-in: a file, a directory, a listening socket, a
  FIFO.
- Integration, over the Python client against a Model B: with
  `file-printer`, `*FX5,1` then `VDU 2 : PRINT "HELLO" : VDU 3` produces
  `HELLO` CR in the file; a BASIC program that sets DDRA, selects a CA2
  pulse mode, writes ORA and polls IFR sees the CA1 flag; after the
  `SetOnline(false)` test hook a print longer than the MOS printer buffer
  stalls, and `SetOnline(true)` lets it finish with every byte in the file.
  `job-file-printer` is checked for job splitting on emulated quiet;
  `tcp-printer` against a listening socket in the test, including a drop
  and reconnect; `host-parallel-printer` against a FIFO.
  Every wait polls the guest, the output or the server's own accounting,
  never the wall clock alone.
