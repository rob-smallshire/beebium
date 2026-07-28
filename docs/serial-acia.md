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
- The **Serial ULA** supplies the transmit and receive bit clocks (selecting the
  baud rate), chooses between the RS423 connector and the cassette interface
  (driving the ACIA's `/DCD` input), and drives the cassette motor relay.

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
SHEILA I/O region.

### ACIA control register

| Bits | Field | Notes |
|------|-------|-------|
| 0-1  | Counter divide select | `00`=/1, `01`=/16, `10`=/64, `11`=master reset |
| 2-4  | Word select | data bits / parity / stop bits (e.g. `101` = 8N1) |
| 5-6  | Transmitter control | `/RTS` level, TX IRQ enable, transmit break |
| 7    | Receive interrupt enable | |

The MOS programs `&15` for the standard configuration: /16 divide, 8N1,
`/RTS` low, RX interrupt enabled.

### ACIA status register

| Bit | Flag | Meaning |
|-----|------|---------|
| 0 | RDRF | Receive Data Register Full |
| 1 | TDRE | Transmit Data Register Empty (masked when `/CTS` high) |
| 2 | /DCD | Data Carrier Detect (1 = no carrier) |
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
| 6    | RS423 (1) / cassette (0) select — drives the ACIA `/DCD` input |
| 7    | Cassette motor relay |

Baud-rate select values (per the SERPROC encoding, as set via `OSBYTE 7`/`8`):

| Value | Baud  |
|-------|-------|
| 0     | 19200 |
| 1     | 1200  |
| 2     | 4800  |
| 3     | 150   |
| 4     | 9600  |
| 5     | 300   |
| 6     | 2400  |
| 7     | 75    |

## Implementation

| Component | File | Role |
|-----------|------|------|
| `Mc6850`        | `devices/Mc6850.hpp`       | Bit-level ACIA: control/status registers, TX/RX bit state machines, parity/framing/overrun, IRQ, `/CTS`-gated TDRE. |
| `SerialUla`     | `serial/SerialUla.hpp`     | SERPROC latch decode + byte↔bit shifter between the ACIA and the attached device; drives `/CTS` from the device's flow-control state. |
| `SerialSocket`  | `serial/SerialSocket.hpp`  | Owns the ACIA + ULA, exposes the two memory-mapped regions, clocking, IRQ, reset; `set_device()` wires a device in as source + sink. |
| `SerialPortDevice` (`SerialDataSource`/`SerialDataSink`) | `serial/SerialDevice.hpp` | The byte-oriented device seam the bit engine talks to. The core holds only the seam; concrete devices live in extensions. |
| `SerialPort` | `extension/SerialPort.hpp` | Non-owning attach/detach handle a PeripheralExtension uses to plug its device into the socket (the single-connector rule). |
| `HasSerialSocket` | `serial/SerialConcepts.hpp` | Detects hardware variants that carry a `serial_socket`. |

### Bit-level model

The ACIA walks a start / data / (parity) / stop-bit state machine one bit at a
time, exactly as the real device does:

- `Mc6850::update_transmit()` returns the next serial bit and its role
  (`Start`/`Data`/`Stop`/...). The `SerialUla` assembles the data bits (LSB
  first) into a byte and hands the byte to the sink on the stop bit.
- `Mc6850::update_receive(bit)` is fed one bit at a time. The `SerialUla` pulls
  a byte from the source and shifts it in as start + 7 or 8 data bits (LSB
  first) + an optional parity bit + 1 or 2 stop bits, following the word format
  the guest selected (captured when the frame starts, so a mid-character
  control-register write cannot corrupt it). On the closing stop bit the ACIA
  latches RDR, sets RDRF, and raises the RX interrupt (subject to the
  receive-interrupt-enable bit). On a 7-bit format only the low seven bits of
  the device's byte are carried, as on a real line.

Framing errors (bad stop bit), parity errors, and receiver overrun (a new
character arriving before RDR is read) are all modelled and surface in the
status register.

### Timing

`SerialUla::tick()` is called once per 2MHz CPU cycle from `Machine::step()`
(both the normal and bus-stretch paths), guarded by `HasSerialSocket`. The
number of ticks per serial bit is `CPU_HZ / baud` (e.g. 104 ticks/bit at 19200
baud). Transmit and receive have independent bit-clock timers derived from the
two baud-rate fields in the Serial ULA latch.

This is a faithful **bit-level** model at an emulator-friendly cadence; it is
not cycle-accurate to the ACIA's 16× sampling clock. The BBC's software (and
fujinet-nio) only depend on correct byte framing at the selected rate. This
mirrors the pragmatic timing approach already taken by the MC6854 byte-trickle
model in the Econet subsystem.

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
| `tests/test_mc6850.cpp`        | ACIA reset/control decode, TX/RX bit framing, framing/parity errors, overrun, IRQ gating, `/CTS` → TDRE. |
| `tests/test_serial_ula.cpp`    | ULA baud/motor/RS423 decode, bit-period derivation, byte↔bit shifting through the ACIA. |
| `tests/test_serial_socket.cpp` | `SerialSocket` + Model B memory-map integration (mapping, mirroring, IRQ to the aggregator), device round trips, and the ULA driving `/CTS` from device back-pressure. |
| `tests/test_serial_pty.cpp`    | POSIX pty transport: `PtyMaster` + `HostSerialEndpoint` bridged through the socket to a `PosixSerialPort` (hardware-parity round trips). |
| `tests/test_host_serial_endpoint.cpp` | Cross-platform: a stuck peer back-pressures via `/CTS` without blocking the emulation thread. |
| `tests/test_{host,rpc,loopback}_serial_extension.cpp` | Each extension attaches its device; rpc-serial round-trips over its service. |
| `tests/test_{host,rpc,loopback}_serial_ui.cpp` | Each extension's `build_view` panel shape. |
| `tests/test_serial_break_e2e.cpp` | Application-to-application BREAK through the *real 6502*: a beebasm program on an auto-booting disc drives/detects a break against a recording device and a live pySerial peer. See [the pattern](testing-from-disc.md). |
| `clients/beebium-python-client/tests/test_serial*.py` | Status, the `watch_status` stream, the extension round trips over gRPC, and a real BBC-BASIC `/CTS` end-to-end test. |

## Status / future work

- **Done**: bit-level ACIA + Serial ULA, `SerialSocket`, wiring into all Model B
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
- **Cassette** (the ULA's other axis) is a separate future seam, gated by the
  cassette-presence axis and *not* routed through `SerialPortDevice`.
- **Network-serial** peers (IP232, RFC 2217) are candidate future extensions
  attaching to the same seam.
