# Serial Port (MC6850 ACIA + Serial ULA)

This document describes Beebium's emulation of the BBC Micro's on-board serial
hardware: the **Motorola MC6850 ACIA** at `&FE08-&FE0F` and the **Ferranti
Serial ULA (SERPROC)** at `&FE10-&FE1F`. Together these implement the BBC's
RS423 / cassette serial interface.

The emulation is a faithful bit-level model (in the style of b2's `MC6850`/`SERPROC`).

## Hardware overview

On a real BBC the serial path is:

```
CPU  <->  MC6850 ACIA (&FE08/&FE09)  <->  Serial ULA (&FE10)  <->  RS423 / cassette
```

- The **ACIA** holds the transmit/receive shift registers, the control and
  status registers, and generates the serial IRQ. It does **not** generate its
  own bit-rate clock.
- The **Serial ULA** supplies the transmit and receive clocks (selecting the
  baud rate), chooses between the RS423 connector and the cassette interface,
  and drives the cassette motor relay. The ACIA divides its clocks by 1, 16 or
  64 itself.

### Register map

| Address       | Access | Function |
|---------------|--------|----------|
| `&FE08`       | read   | ACIA Status Register |
| `&FE08`       | write  | ACIA Control Register |
| `&FE09`       | read   | ACIA Receive Data Register (RDR) |
| `&FE09`       | write  | ACIA Transmit Data Register (TDR) |
| `&FE10`       | write  | Serial ULA control latch |

The ACIA occupies `&FE08-&FE0F` mirrored on the low address bit; the Serial ULA
occupies `&FE10-&FE1F` (a write-only latch). Reads of the Serial ULA return fast
2MHz open bus (the last value on the data bus), consistent with the rest of the
SHEILA I/O region, and **latch `&FE`** as a write would: the ULA has no
read/write line, and on a read the data bus holds the address high byte.
`&FE` is `%11111110`: motor on, RS423 selected, receive 75, transmit 2400. This
is an unverified decision of the cassette specification
([cassette-specification.md](discussion/cassette-specification.md)): Acorn does
not document it, and every reference emulator and the CPLD replica of the ULA
do it. A debugger peek of either region has no side effect.

### ACIA control register

| Bits | Field | Notes |
|------|-------|-------|
| 0-1  | Counter divide select | `00`=/1, `01`=/16, `10`=/64, `11`=master reset |
| 2-4  | Word select | data bits / parity / stop bits (e.g. `101` = 8N1) |
| 5-6  | Transmitter control | `/RTS` level, TX IRQ enable, transmit break |
| 7    | Receive interrupt enable | |

MOS 1.20 runs RS423 at **/64**: its idle value is `&56` (/64, 8N1, `/RTS`
high), and the Serial ULA's rate fields assume the ACIA divides by 64. `&15`
(/16) appears only while the MOS sets up a cassette SAVE, and the cassette
reads at `&D5` (/16, 1200 baud) or `&D6` (/64, 300 baud): the cassette's speed
is chosen by the ACIA's divider alone (cassette research findings, §2.1).

### ACIA status register

| Bit | Flag | Meaning |
|-----|------|---------|
| 0 | RDRF | Receive Data Register Full |
| 1 | TDRE | Transmit Data Register Empty (masked when `/CTS` high) |
| 2 | /DCD | Data Carrier Detect: 1 while the pin is high, and latched by a rising edge until the data register is read |
| 3 | /CTS | Clear To Send (1 = not clear to send) |
| 4 | FE   | Framing Error |
| 5 | OVRN | Receiver Overrun |
| 6 | PE   | Parity Error |
| 7 | IRQ  | Interrupt Request |

### Serial ULA control latch

| Bits | Field |
|------|-------|
| 0-2  | Transmit baud-rate select |
| 3-5  | Receive baud-rate select |
| 6    | RS423 (1) / cassette (0) select |
| 7    | Cassette motor relay |

Rate select values (per the SERPROC encoding, as set via `OSBYTE 7`/`8`). Each
is a divider of the ULA's 16/13 MHz clock; the baud rate assumes the ACIA
divides by 64, and every rate is 0.16% above nominal:

| Value | Baud  | ULA clocks per receive/transmit clock |
|-------|-------|-----|
| 0     | 19200 | 1   |
| 1     | 1200  | 16  |
| 2     | 4800  | 4   |
| 3     | 150   | 128 |
| 4     | 9600  | 2   |
| 5     | 300   | 64  |
| 6     | 2400  | 8   |
| 7     | 75    | 256 |

## Implementation

| Component | File | Role |
|-----------|------|------|
| `Mc6850`        | `devices/Mc6850.hpp`       | Clocked ACIA: control/status registers, /1, /16 and /64 division, start-bit hunting, TX/RX frames, parity/framing/overrun, the `/DCD` latch, IRQ, `/CTS`-gated TDRE. |
| `SerialUla`     | `serial/SerialUla.hpp`     | SERPROC latch decode, the 16/13 MHz clock and its rate dividers, the RS423 byte↔bit shifter between the ACIA and the attached device; drives `/CTS` from the device's flow-control state. Takes a part, Ferranti (default) or VLSI, for the cassette side. |
| `SerialSocket`  | `serial/SerialSocket.hpp`  | Owns the ACIA + ULA, exposes the two memory-mapped regions, clocking, IRQ, reset; `set_device()` wires a device in as source + sink. |
| `SerialPortDevice` (`SerialDataSource`/`SerialDataSink`) | `serial/SerialDevice.hpp` | The byte-oriented device seam the bit engine talks to. The core holds only the seam; concrete devices live in extensions. |
| `SerialPort` | `extension/SerialPort.hpp` | Non-owning attach/detach handle a PeripheralExtension uses to plug its device into the socket (the single-connector rule). |
| `HasSerialSocket` | `serial/SerialConcepts.hpp` | Detects hardware variants that carry a `serial_socket`. |

### Clocked model

The ACIA is clocked, as the device is, and the Serial ULA supplies the clocks:

- `Mc6850::transmit_clock()` is one transmit clock. Every 1, 16 or 64 clocks
  (the counter divide) the transmitter starts the next bit and reports its
  level and role (`Idle`/`Start`/`Data`/`Parity`/`Stop`/`Break`). TDRE sets as a
  character starts. The `SerialUla` assembles the data bits (LSB first) into a
  byte and hands the byte to the sink on the stop bit.
- `Mc6850::receive_clock(rxd)` is one receive clock with RxD at a level
  (`receive_clocks(n, rxd)` is n of them, sampling at the same points). At /16
  and /64 the receiver hunts for RxD low and confirms a start bit after 8 or 32
  clocks of low, half a bit; a shorter low is ignored. It then samples each
  following bit 16 or 64 clocks later, mid-bit. At /1 it samples on every clock.
  On the closing stop bit the ACIA latches RDR, sets RDRF, and raises the RX
  interrupt (subject to the receive-interrupt-enable bit).
- With RS423 selected, the `SerialUla` pulls a byte from the source and frames
  it as start + 7 or 8 data bits (LSB first) + an optional parity bit + 1 or 2
  stop bits, following the word format the guest selected (captured when the
  frame starts, so a mid-character control-register write cannot corrupt it),
  and holds each level on RxD for **64 receive clocks whatever the ACIA's
  divider**: the far end runs at the selected baud rate and knows nothing of
  the ACIA's setting. At /64 the byte arrives; at /16 or /1 the guest reads
  garbage or framing errors, as on the machine. On a 7-bit format only the low
  seven bits of the device's byte are carried, as on a real line.

`/DCD`: a low-to-high transition sets status bit 2 and raises the receive
interrupt when CR7 is set; the bit stays set until the **data register** is
read, however many times the status register is read in between, and then
follows the pin. While `/DCD` is high the receiver is held in reset and RDRF
reads clear. **Master reset** (counter divide `11`) clears the control
register, so `/RTS` is low and both interrupts stay disabled until the guest
writes it again; TDRE and IRQ read low until a non-reset divide is written.
Both rules are unverified decisions of the cassette specification, taken from
measurements on real machines reported by other emulators (beebjit, jsbeeb)
rather than from the datasheet, whose `/DCD` sequence is status then data.

Framing errors (bad stop bit), parity errors, and receiver overrun (a new
character arriving before RDR is read) are all modelled and surface in the
status register.

### Receiver overrun

The overrun rules repay careful reading, because the datasheet's sentences pull
in different directions and one of them cannot be implemented literally against
the BBC's MOS. What the MC6850 datasheet says, in the Receiver Overrun (OVRN,
Bit 5) description and the RECEIVE section:

1. *"A character or a number of characters were received but not read from the
   Receive Data Register prior to subsequent characters being received. The
   overrun condition begins at the midpoint of the last bit of the second
   character received in succession without a read of the RDR having
   occurred."*
2. *"The Overrun does not occur in the Status Register until the valid character
   prior to Overrun has been read."*
3. *"The RDRF bit remains set until the Overrun is reset."*
4. *"Character synchronization is maintained during the Overrun condition. The
   Overrun indication is reset after the reading of data from the Receive Data
   Register or by a Master Reset."*
5. *"The receiver is also double buffered so that a character can be read from
   the data register as another character is being received in the shift
   register."*

How `Mc6850` models them:

- **The second character in succession is lost, not the first** (1). When RDR is
  full the transfer from the shift register is inhibited and *"the RDR contents
  remain valid"*, so the older character survives and the newer one is dropped.
- **Double buffering is one character time of slack, not a queue slot** (5). It
  lets the guest read RDR while the next character is *in flight*; it does not
  let two completed characters queue. Modelling it as a second slot makes two
  characters in succession both arrive, which contradicts (1).
- **The flag surfaces late** (2), via a `rx_ovrn_pending_` latch: the overrun is
  reported on the read that takes the last good character before the gap, not
  when the loss happened.
- **A newly received character clears the flag** (4). `OVRN` describes the gap
  in the data stream just past; the receiver keeps delivering, so the next
  character does not inherit the error.

That last point is the one that matters in practice, and it is where rule (3)
had to be set aside. Holding `RDRF` set until the overrun is reset assumes a
guest that polls the status register: it will notice `RDRF` and read again, and
that second read clears the condition. **The BBC's MOS reads the ACIA only on
interrupt.** With `RDRF` held set and no new character to raise one, no further
read ever comes -- the receiver wedges, every subsequent character is rejected
as an overrun, and the serial port goes permanently deaf. Implemented literally,
it is far worse than the bug it would fix.

Leaving `OVRN` set instead is the milder failure that issue #59 reported: a
burst arriving while nobody is listening (a modem's `OK` reaching a machine
still at the BASIC prompt) leaves the flag standing across an idle line, and the
next character to arrive -- minutes later, entirely unrelated -- is discarded by
the guest as an error. Clearing on arrival ends the condition at the moment the
data stream resumes, which is both what (4) describes and what BeebEm does
(`Status &= ~MC6850_STATUS_OVRN` at the top of its receive path).

The rules are pinned by tests in `tests/test_mc6850.cpp`, and the end-to-end
consequence by `test_commstar_serial_receive.py`, which drives real comms
software over `rpc-serial` and checks what actually reached the screen.

### Timing

`SerialUla::tick()` is called once per 2MHz CPU cycle from `Machine::step()`
(both the normal and bus-stretch paths), guarded by `HasSerialSocket`. The ULA
runs from 16 MHz / 13 = 1.2308 MHz, as on the machine: a fractional accumulator
yields 8 ULA clocks for every 13 ticks, so every interval is a whole number of
ULA clocks (812.5 ns) and every rate is 0.16% above nominal. The transmit and
receive rate fields each select a divider of the ULA clock (the table above);
the divider outputs are the transmit and receive clocks the ACIA counts. With
RS423 selected at 19200 baud and the ACIA at /64, a bit is 64 receive clocks,
64 ULA clocks, 104 ticks.

**Cassette selected**: `/DCD` and `/CTS` are held low (AUG §20.7.4); the
high-tone detector that drives `/DCD` in cassette mode, and the data separator
that supplies the cassette receive clock, come with the tape path. Until then
the ACIA gets no receive clock and RxD idles high, and the receive rate field
is ignored, as on the machine. The transmit clock is the transmit divider
output, so TDRE paces as it would; the transmitted bits go nowhere and the
RS423 sink receives nothing. The RS423 line still runs at its own rate, so a
far end's bytes sent meanwhile are framed and lost. `SerialStatus` reports each
divider as `tx_ula_divider` and `rx_ula_divider`.

### IRQ wiring

Unlike Econet (which is an NMI source gated through the INTON/INTOFF
flip-flop), the ACIA interrupt is wired to the **shared CPU IRQ line**.
`SerialSocket::irq_pending()` is therefore added to each Model B variant's
`IrqAggregator` (bit 4) and is sampled by `Machine::step()` via `poll_irq()`
alongside the VIAs, Tube, and 1MHz bus.

## Device seam and extensions

The byte-oriented seam between the bit engine and whatever is on the far end of
the wire is `SerialPortDevice` (`serial/SerialDevice.hpp`) — the union of two
half-interfaces, `SerialDataSource` (device → Beeb) and `SerialDataSink`
(Beeb → device). It is the serial analogue of `UserPortDevice` /
`OneMHzBusDevice`. `SerialSocket::set_device(SerialPortDevice*)` wires a device
in as both source and sink; the non-owning `beebium::SerialPort` handle
(`extension/SerialPort.hpp`) is how a PeripheralExtension attaches/detaches its
device (`ctx.get<SerialPort>().attach(...)`), enforcing the single connector.

Alongside the byte streams the seam carries the RS423 **line conditions** out of
band, edge-forwarded by the ULA from the 6850's control register: the sink's
`set_rts(bool)` (the BBC's outbound `/RTS`) and `set_break(bool)` (the guest
holding the TX line in a break), and the source's `take_break()` (a device-side
break the ULA clocks into the receiver as a Framing Error + `0x00`). All default
to no-ops, so a device only implements the ones it cares about. `host-serial`
maps `set_break` onto a real line break (`TIOCSBRK`/`TIOCCBRK` on POSIX,
`SetCommBreak`/`ClearCommBreak` on Win32), applied on its writer thread; the RFC
2217 endpoints carry break both ways over the wire (see
[serial-rfc2217.md](serial-rfc2217.md)).

The **core holds only the seam and the bit engine**; the concrete devices are
PeripheralExtensions, each owning its configuration and (where useful) its own
typed gRPC service and a Peripherals-sidebar `ExtensionUi`:

| Extension | Device | Deployment | What it is |
|-----------|--------|------------|------------|
| `host-serial`     | `HostSerialEndpoint`   | **built-in** | Bridges the port to a host pty or serial device. |
| `rpc-serial`      | `RpcSerialEndpoint`    | plugin | The RPC client *is* the device: inject/collect bytes over gRPC. |
| `loopback-serial` | (the extension itself) | plugin | TX → RX echo plug; zero config, a "does my serial path work" smoke. |
| `ip232-serial`    | `Ip232SerialEndpoint`  | plugin | Bridges the port to a tcpser-style IP232 server over TCP (retro modem / BBS dial-out). See [serial-ip232.md](serial-ip232.md). |
| `rfc2217-client-serial` | `Rfc2217ClientEndpoint` | plugin | Drives a remote RFC 2217 access server (ser2net / FujiNet); sets the remote UART's real baud. See [serial-rfc2217.md](serial-rfc2217.md). |
| `rfc2217-server-serial` | `Rfc2217ServerEndpoint` | plugin | Exposes the BBC port to any RFC 2217 client (pyserial / socat / tio); loopback-bound, unauthenticated. See [serial-rfc2217.md](serial-rfc2217.md). |

Only `host-serial` is essential enough to be a **built-in** (statically linked
into the server). The others ship as **dynamically-loaded plugins**, discovered
at runtime from `<exe-dir>/extensions/<name>/` (a `.dylib`/`.so`/`.dll` +
`manifest.json`), exactly like `acorn-rtc` / `acorn-scsi` / `piconet`. A
user-specified `--extension-dir` layers *extra* plugins over those defaults.

The shared OS-port primitives stay in core under `beebium::serial`:
`HostSerialPort` (interface), `PosixSerialPort` / `Win32SerialPort` (open an
existing device path), and `PtyMaster` (create a pseudo-terminal, own the master
end, advertise the slave path). The Piconet transport reuses the same primitives
via thin shim headers.

Only one device can own the single serial port at a time, so the serial
extensions are mutually exclusive on the command line.

`ip232-serial` and the two `rfc2217-*-serial` extensions are **network-backed**
siblings: the same endpoint machinery with a TCP socket + a small protocol codec
in place of a tty. They reuse the shared cross-platform transport in
`beebium::net` (`SocketPlatform.hpp`, `TcpClientSerialPort` /
`TcpServerSerialPort`, `EndpointUrl`). See [serial-ip232.md](serial-ip232.md)
and [serial-rfc2217.md](serial-rfc2217.md).

## Flow control and bounded buffers

A core invariant: an external peer must never stall the *emulator host* — only
the emulated *guest* may stall, and only via faithful hardware back-pressure.
Both device endpoints bound their queues and surface fullness through the
MC6850's `/CTS` line:

- **TX (Beeb → device).** The device's transmit queue has a back-pressure mark
  (`tx_buffer`, default 4096 bytes). At/above it the device reports
  `!accepts_more()`, the Serial ULA asserts the ACIA's `/CTS`, and `TDRE` reads
  0 — so the guest's transmit loop busy-waits (it stalls the guest, losslessly),
  exactly as a real ACIA gates on `/CTS`. A small hard cap above the mark drops
  bytes only if the guest *also* ignores `/CTS` (a real ACIA overruns there too).
  `host-serial` additionally drains its queue on a dedicated **writer thread**,
  so a stuck real peer blocks only that thread, never the emulation thread.
- **RX (device → Beeb).** `RpcSerial.Send` is bounded and returns the number of
  bytes *accepted* (the POSIX-write idiom); it never blocks. The client resends
  the unaccepted tail.

The `tx_buffer` size is configurable per extension (see below).

## Configuring serial devices (CLI)

Each serial extension is configured with the generic extension argument form —
`--<name> key=value:key=value...` (the spec is the single argv token after the
flag; colon-separated `key=value` pairs). `list-extensions` and
`describe-extension <name>` print the live parameter schema.

**Quoting values that contain a colon.** Because the pairs are colon-separated, a
value that itself contains a `:` (a URL, a Windows path) must be wrapped in
**double quotes** — the one rule. The shell usually needs an outer single-quote
too: `--ip232-serial 'url="ip232://host:25232"'`,
`--scsi-hdd '0:image="file:///discs/drive.dat"'`. An unquoted `scheme://…` value
is detected and rejected with a message pointing you to quoting.

```
# host-serial: bridge to a host pty or device
--host-serial                                       # pty, defaults
--host-serial mode=pty:path=/tmp/beeb-serial        # pty + stable slave symlink
--host-serial mode=device:path=/dev/ttyUSB0:baud=9600
#   mode = pty | device (default pty)
#   path = pty: optional stable symlink to the slave; device: the path to open
#   baud = device line speed (default 19200; ignored for pty)
#   tx_buffer = transmit buffer bytes (default 4096)

# rpc-serial: the gRPC client is the device on the far end of the wire
--rpc-serial                                        # default 4096-byte TX buffer
--rpc-serial tx_buffer=256
#   tx_buffer = transmit buffer bytes (default 4096)

# loopback-serial: TX -> RX echo
--loopback-serial                                   # no parameters

# ip232-serial: connect out to a tcpser-style IP232 server (see serial-ip232.md)
--ip232-serial host=bbs.example.com:port=25232      # ip232 mode, persistent
--ip232-serial host=127.0.0.1:port=25232:mode=raw   # raw byte pipe, connect on RTS
#   host = IP232 server hostname/address (default localhost)
#   port = IP232 server TCP port (default 25232)
#   mode = ip232 | raw (default ip232)
#   handshake = convey RTS via the 0xFF escape, ip232 mode (default true)
#   tx_buffer = transmit buffer bytes (default 4096)
```

### In a preset

A serial device can be pinned in a preset like any other extension. `create-preset`
accepts the same `--<name> spec` flags as `start` and emits them into the preset's
`extensions` array:

```
beebium-model-b create-preset --name "FujiNet" \
    --host-serial mode=device:path=/dev/ttyUSB0:baud=9600
```

produces (and `--preset <file>` loads back):

```json
{
  "model": "model-b",
  "name": "FujiNet",
  "extensions": [
    { "name": "host-serial",
      "config": { "mode": "device", "path": "/dev/ttyUSB0", "baud": 9600, "tx_buffer": 4096 } }
  ]
}
```

A CLI `--<name>` flag at `start` adds to the preset's extensions; since only one
device can own the serial port, list just one serial extension (a preset serial
device plus a conflicting CLI serial device is rejected at attach time).

## gRPC services and clients

`SerialService` (`src/service/proto/serial.proto`) is now a **status-only**
surface for the on-board chips — it observes the ACIA/ULA registers and never
attaches a device:

| RPC | Purpose |
|-----|---------|
| `GetSerialStatus`   | One-shot ACIA + Serial ULA register snapshot. |
| `WatchSerialStatus` | Server-pushed stream: an initial snapshot, then a fresh one whenever the chip state changes (sampled at `min_interval_ms`, default 50, so per-byte TDRE/RDRF churn is coalesced). |

The `rpc-serial` extension carries its own typed service, `RpcSerial`
(`src/extensions/rpc-serial/rpc_serial.proto`):

| RPC | Purpose |
|-----|---------|
| `Send`      | Inject bytes for the BBC to receive; returns the accepted count. |
| `Receive`   | Collect bytes the BBC has transmitted. |
| `GetStatus` | Pending byte counts in each direction. |

Clients:

- **Python**: `bbc.serial` (`SerialStatus` + `watch_status()` stream) and
  `bbc.rpc_serial` (`send` / `receive` / `status`).
- **TypeScript**: `bbc.serial` (`Serial`: `getStatus` / `watchStatus`) and
  `bbc.rpcSerial` (`RpcSerial`: `send` / `receive` / `getStatus`).
- **macOS**: the serial extensions appear in the Peripherals sidebar through the
  generic server-driven Extension UI — no serial-specific client code.

`clients/beebium-python-client/examples/serial_demo.py` is a runnable end-to-end demo over
`--rpc-serial`.

### Peripherals sidebar (Extension UI)

Only host-serial has an `ExtensionUi` panel (`HostSerialUi`): it shows the mode
heading (PTY / Device), the device path, and -- in device mode -- the baud, with
the path editable to re-point at runtime. rpc-serial and loopback-serial have no
panel: a client-driven peer and a fixed self-echo plug have nothing to show or
configure (rpc-serial's queue depths are debugger detail, available via
`RpcSerial.GetStatus`). All three still appear in the sidebar by name through the
generic discovery (`PeripheralExtensionService.ListExtensions`); the per-node
panel is rendered only when `has_ui` is set (`ExtensionUiService.SubscribeView`
by the server-assigned UUID id), so the panel-less ones simply list without a
body. No frontend changes are needed either way.

## Generic end-to-end (any serial ROM)

The serial path is protocol-agnostic. To use a ROM that talks over the serial
port, bridge it to a host pty and attach your serial client to the advertised
slave:

```bash
beebium-model-b --mos roms/acorn-mos_1_20.rom \
                --sideways 15:rom:your-rom.rom \
                --host-serial mode=pty   # prints: host-serial: pty ready at /dev/pts/N
# attach your serial client/device to /dev/pts/N; the ROM's * commands drive bytes:
#   ROM -> ACIA (&FE09) -> Serial ULA -> HostSerialEndpoint -> pty -> your device
```

beebium has no knowledge of the ROM's command set or the device's protocol; the
ROM and the device agree on the bytes, and beebium transports them.

## Tests

| Test | Coverage |
|------|----------|
| `tests/test_mc6850.cpp`        | ACIA reset/control decode, division at /1, /16 and /64 for every word format, start-bit hunting and glitch rejection, batched clocks, transmit framing and parity, framing/parity errors, overrun, IRQ gating, the `/DCD` latch, master reset clearing the control register, `/CTS` → TDRE. |
| `tests/test_serial_ula.cpp`    | ULA rate/motor/RS423 decode, the 16/13 MHz clock and its dividers at every rate, 64 clocks per RS423 bit (and garbage at /16), the `&FE10` read rule and side-effect-free peek, cassette mode's `/DCD`, `/CTS` and silence, the part, byte↔bit shifting through the ACIA. |
| `tests/test_serial_socket.cpp` | `SerialSocket` + Model B memory-map integration (mapping, mirroring, IRQ to the aggregator), device round trips, and the ULA driving `/CTS` from device back-pressure. |
| `tests/test_serial_pty.cpp`    | POSIX pty transport: `PtyMaster` + `HostSerialEndpoint` bridged through the socket to a `PosixSerialPort` (hardware-parity round trips). |
| `tests/test_host_serial_endpoint.cpp` | Cross-platform: a stuck peer back-pressures via `/CTS` without blocking the emulation thread. |
| `tests/test_{host,rpc,loopback}_serial_extension.cpp` | Each extension attaches its device; rpc-serial round-trips over its service. |
| `tests/test_{host,rpc,loopback}_serial_ui.cpp` | Each extension's `build_view` panel shape. |
| `tests/test_serial_break_e2e.cpp` | Application-to-application BREAK through the *real 6502*: a beebasm program on an auto-booting disc drives/detects a break against a recording device and a live pySerial peer. See [the pattern](testing-from-disc.md). |
| `clients/beebium-python-client/tests/test_serial*.py` | Status, the `watch_status` stream, the extension round trips over gRPC, and a real BBC-BASIC `/CTS` end-to-end test. |

## Status / future work

- **Done**: clocked ACIA + Serial ULA, `SerialSocket`, wiring into all Model B
  variants (memory map, IRQ, reset, clocking); the `SerialPortDevice` seam +
  `SerialPort` handle; the host-serial / rpc-serial / loopback-serial
  extensions, each with configuration, `/CTS`-bounded buffers (configurable
  `tx_buffer`), and an `ExtensionUi` panel; host-serial async reader + writer
  threads; the shared OS-port primitives in `beebium::serial`; the status-only
  `SerialService` with `WatchSerialStatus` streaming and the `rpc-serial`
  extension's own `RpcSerial` service; Python and TypeScript clients; C++,
  Python, and TypeScript tests (incl. pty round trips and a real BBC-BASIC
  `/CTS` test); the `serial_demo.py` example; and this document.
- **host-serial frame coalescing**: some peers need transmitted frames coalesced
  (a short debounce) to avoid packet splitting — b2 carried such a workaround.
  The hook lives in `HostSerialEndpoint`; deferred pending real-device testing.
- **Cassette** (the ULA's other axis) is a separate seam, gated by the
  cassette-presence axis and *not* routed through `SerialPortDevice`. The clock
  model it needs is in place (cassette round 3); the tape path, data separator
  and high-tone detector follow in
  [cassette-specification.md](discussion/cassette-specification.md).
- **Network-serial** peers (IP232, RFC 2217) are candidate future extensions
  attaching to the same seam.
