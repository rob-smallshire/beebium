# IP232 serial (`ip232-serial`)

`ip232-serial` connects the BBC Micro's serial port (RS423) to a **tcpser-style
IP232 server** over TCP, so the emulated Beeb can talk to a virtual Hayes modem
and dial out to telnet BBSes and other TCP services — the classic retro
telecomms experience, with BeebEm parity.

It is one of Beebium's serial `SerialPortDevice` extensions, the network sibling
of `host-serial`: the same endpoint machinery (async reader/writer threads,
bounded queues, `/CTS` back-pressure) with a TCP socket and a small protocol
codec in place of a tty. For the shared serial architecture see
[serial-acia.md](serial-acia.md).

`ip232-serial` ships as a **dynamically-loaded plugin** (only `host-serial` is a
built-in), discovered at runtime from `<exe-dir>/extensions/ip232-serial/` — no
special setup; a packaged Beebium server includes it.

## Why it exists

The BBC's RS423 port was widely used in the 1980s to drive a modem and dial
bulletin boards. `tcpser` is a modern program that emulates a Hayes modem and
bridges the "phone line" to TCP: the emulator opens a TCP connection to `tcpser`,
the guest sends `AT` commands and dials, and `tcpser` connects onward to a telnet
host. `ip232-serial` is the Beebium end of that link.

It is also the first proof that Beebium's serial device seam is genuinely open: a
network-backed device is "just another extension" attaching to the same
`SerialPort` handle as `host-serial` / `rpc-serial` / `loopback-serial`, with no
changes to the core ACIA/ULA emulation.

```
BBC RS423  <->  ip232-serial (TCP client)  <--TCP-->  tcpser (server, :25232)  <->  telnet BBS / TCP
```

## How it works

**The emulator is the TCP client.** `ip232-serial` connects *out* to an IP232
server at `host:port` (tcpser's default port is `25232`). There are two modes.

### `ip232` mode (default)

A persistent TCP connection with a minimal in-band signalling protocol that uses
`0xFF` as an escape/flag byte (defined by tcpser and BeebEm; there is no formal
spec):

- **Outbound (Beeb → server).** Data bytes are sent verbatim, except a data byte
  of `0xFF` is doubled to `0xFF 0xFF` so it is not mistaken for a flag. When
  `handshake` is enabled, a change in the BBC's RTS line is sent as
  `0xFF 0x01` (RTS asserted) or `0xFF 0x00` (RTS deasserted).
- **Inbound (server → Beeb).** `0xFF 0xFF` is a literal `0xFF` data byte;
  `0xFF 0x01` / `0xFF 0x00` convey the modem's DTR. The BBC RS423 connector has
  **no DTR or DCD pin** (see below), so inbound DTR is decoded but informational.

### `raw` mode

A pure byte pipe with no escaping. The TCP connection follows the BBC's RTS line:
it **connects when RTS is asserted and disconnects when RTS is dropped**. Use raw
mode for a plain socket peer that does not understand the `0xFF` convention.

### Control lines: what the BBC actually has

The Acorn RS423 connector exposes only five lines: signal ground, transmitted
data, received data, **RTS** (an output from the BBC) and **CTS** (an input to
the BBC). There is **no DCD, DTR or DSR** pin. So `ip232-serial` models exactly
what the hardware has:

- **CTS (in).** While the TCP connection is down, the device reports "not clear
  to send", which makes the Serial ULA assert the ACIA's `/CTS`. The guest's
  transmit loop then busy-waits rather than transmitting into a dead socket — a
  clean stall of the *guest*, never the emulator host. When the connection comes
  up, transmission resumes.
- **RTS (out).** Driven by the MC6850 control register. In `ip232` mode with
  `handshake` it is conveyed to the server via the `0xFF` escape; in `raw` mode
  it drives the connect/disconnect lifecycle.

A serial **BREAK is not carried** by IP232 (the protocol has no break
signalling): a BBC-transmitted break is not propagated, and the BBC's receiver
is never handed one. If you need break across the link — for frame-based
protocols such as DMX512 or LIN — use the RFC 2217 endpoints, which carry it in
both directions (see [serial-rfc2217.md](serial-rfc2217.md)).

### Never stalls the emulator

All socket I/O runs on dedicated threads (a connection/reader thread and a writer
thread); the emulation thread only touches bounded, mutex-protected queues. An
unresponsive or slow peer can stall the *guest* (via real `/CTS` back-pressure)
but never the emulator host. The transmit queue is bounded by `tx_buffer` (the
`/CTS` mark) with a small hard cap above it; a dropped connection mid-transmit
loses the in-flight bytes, exactly as real hardware would. In `ip232` mode a
dropped connection is retried automatically.

## How to use it

`ip232-serial` is configured with the generic extension argument form,
`--ip232-serial key=value:key=value`. The endpoint is given either as a single
`url=` or as separate `host=`/`port=` (not both).

| key | type | default | meaning |
|-----|------|---------|---------|
| `url` | string | — | endpoint as `[scheme://]host:port`; alternative to `host`/`port` |
| `host` | string | `localhost` | IP232 server hostname / address |
| `port` | integer | `25232` | IP232 server TCP port |
| `mode` | string | `ip232` | `ip232` (escaped, persistent) or `raw` (pipe, connect on RTS) |
| `handshake` | boolean | `true` | convey RTS via the `0xFF` escape (`ip232` mode only) |
| `tx_buffer` | integer | `4096` | transmit buffer bytes; `/CTS` asserts at/above it |

`beebium-model-b describe-extension ip232-serial` prints this schema live.

Because the argument form splits on `:`, a `url` value (which contains a port
colon) must be **wrapped in double quotes**; the shell usually needs an outer
single-quote too:

```
--ip232-serial 'url="ip232://bbs.example.com:25232"'
--ip232-serial 'url="127.0.0.1:25232":mode=raw'     # scheme optional
```

An unquoted `url=ip232://host:port` is caught with a message telling you to
quote it. `url=` and `host=`/`port=` cannot be combined.

### Worked example: a bare Hayes session from BBC BASIC

No comms ROM required — enough to prove the link and talk to the virtual modem.

1. Start a tcpser virtual modem (in another terminal):

   ```
   tcpser -v 25232 -s 19200 -l 7
   ```

2. Launch a Beebium server with the IP232 bridge:

   ```
   beebium-model-b start --ip232-serial host=localhost:port=25232
   ```

3. In the emulated BBC, route the serial port and talk to the "modem". From BBC
   BASIC, send keyboard input to the RS423 output and show RS423 input on screen:

   ```basic
   *FX2,2        : REM take input from RS423
   *FX3,1        : REM send output to RS423
   ```

   Then type Hayes commands, e.g. `ATDT bbs.example.com:23` to have tcpser dial a
   telnet BBS. (`*FX2,0` / `*FX3,0` restore the keyboard and screen.)

### Worked example: Commstar to a viewdata BBS (Night Owl)

A full session with real period software, dialling a real board. [Night Owl
BBS](https://www.telnetbbsguide.com/bbs/night-owl-bbs/) runs Premiere BBS on a
BBC Master 128 in Glasgow, reachable at `nightowlbbs.ddns.net:6400`: a viewdata
(Prestel-style) system, 2400 baud, one line, open roughly 19:30-23:00 GMT, guest
login `GUEST` / `GUEST` / `NIGHTOWL`. The BBC end is Pace **Commstar** in a
sideways ROM slot.

**1. Start tcpser with a phonebook entry.** `-n <number>=<host:port>` maps a
dialled "phone number" onto an address, so the hostname is typed once here
rather than through the emulated keyboard on every call:

```
tcpser -v 25232 -s 2400 -l 4 -n 1=nightowlbbs.ddns.net:6400
```

`-s 2400` matches the board's rate, so tcpser paces bytes at something the
emulated ACIA absorbs comfortably. Add `-t sS` for a byte-level trace of the
serial side when diagnosing.

The phonebook is optional: it is a lookup, not a restriction, so a dial string
that matches no entry is used as the address directly. `ATDTnightowlbbs.ddns.net:6400`
works just as well and needs no `-n`. The phonebook simply spares you typing a
long hostname through the emulated keyboard on every call — and a single
mistyped character costs a full redial, since Hayes command-line editing is not
to be relied on. Retype the whole line rather than backspacing, and use the
`SR<-` bytes in a `-t sS` trace to see exactly what the BBC sent.

**2. Start the server** with Commstar fitted and the IP232 bridge pointed at
tcpser:

```
beebium-model-b start \
  --sideways 13:rom:commstar_1_40.rom \
  --ip232-serial host=localhost:port=25232
```

**3. Dial.** An AT command has to be terminated with a carriage return, and in
Prestel mode `<RETURN>` does not send one — it sends the viewdata "proceed to
next frame" character. There are two ways round that.

*Either* stay in viewdata mode and terminate with **`CTRL-M`**, which does send a
real CR (see the table below). Press `<#>` for `Emulate : Prestel`, `<C>` for
chat, type `ATDT1` and press `CTRL-M`. Nothing further is needed: the terminal is
already in the mode the board's pages want.

*Or* dial from **Terminal** mode and switch afterwards. Check the top line reads
`Emulate : Terminal` (`<#>` toggles), then:

- `<I>` to initialise the RS423: word format option **5** (8 bits, no parity, 1
  stop) with `<R>` and `<S>` both cycled to **2400**. `<RETURN>` to go back.
- `<C>` for chat mode, then type `ATDT1` and press `<RETURN>`.

Either way tcpser logs the dial and answers `CONNECT 2400`.

**Why `CTRL-M` works.** In Prestel mode the three keys that look alike are:

| keypress | transmits | as viewdata | as ASCII |
|----------|-----------|-------------|----------|
| `SHIFT-3` | `0x23` | `£` | `#` |
| `RETURN` | `0x5F` | `#` | `_` |
| `CTRL-M` | `0x0D` | CR | CR |

Both `CTRL-M` and `RETURN` produce MOS character code 13, so Commstar cannot be
choosing between them on the character. It calls `OSBYTE 122` to ask which key
is physically held (the scan starts at key 16, skipping SHIFT and CTRL) and
substitutes the viewdata `#` only for RETURN, whose internal key number is
`0x49`. Confirmed on real hardware, and pinned by
`clients/beebium-python-client/tests/test_commstar_prestel_keys.py`.

tcpser's `-D` direct connection looks like the way to avoid dialling (and hence
the mode switch) altogether, but it **does not work** with the ip232 virtual
serial device: it connects the line and reports `DTR has gone high`, then never
forwards inbound data to the serial side. Verified on tcpser 1.1.4 with a client
that raises DTR and a board that sends only after the client has attached, with
and without a proxy in the chain: zero bytes delivered. Dial with `ATDT`.

**4. Be in Prestel emulation when the first page arrives.** Viewdata pages are
teletext and render correctly only in Prestel mode. Dialling with `CTRL-M`
leaves you there already, so this step is nothing but a check.

If you dialled from Terminal mode you must now switch, and quickly: `<ESCAPE>`
back to the menu (the line stays up and incoming data is buffered), `<#>` to
select `Emulate : Prestel`, then `<C>` to return to chat. A page that arrived
while you were still in Terminal mode appears as mosaic characters printed as
ASCII; `<f8>` asks the board to retransmit the current frame, though not every
board honours it.

Entering Prestel mode reconfigures the line to viewdata's **7E1 at 1200/75**,
which Beebium receives correctly. Keep the **word format** at 7E1 -- Commstar's
manual is explicit that it should not be altered while using Prestel -- but the
**rates are yours to raise**, and doing so transforms the experience.

IP232 is a byte pipe with no bit timing of its own, so the guest's rate need not
match the board's; it only governs how fast the BBC drains the socket queue. At
1200 baud a 40x24 frame of about 960 characters takes some eight seconds to
paint, which is authentically what Prestel felt like. Cycling `<R>` and `<S>` in
`<I>` up to **9600** (Commstar's maximum) brings that under a second, and
running tcpser with `-s 9600` to match keeps it from becoming the new
bottleneck. The manual leaves `<I>` available in Prestel mode for exactly this
kind of adjustment.

**5. Log off properly.** `*90#` is the conventional viewdata logoff (the board
shows a goodbye frame and drops the line). Walking away instead leaves the line
held until the board times out, which on a single-line system locks everyone else
out. If the board is unresponsive, hang up at the modem instead: type `+++` (no
RETURN), wait for `OK`, then `ATH0` terminated with `CTRL-M` — the same carriage
return an AT command always needs, available without leaving viewdata mode.

#### Optional: a dialling delay

Only needed if you dial from Terminal mode. Over TCP, `CONNECT` is
instantaneous, so there is no dial-and-train-up pause in which to do the
Terminal-to-Prestel hop that a real modem would have given you. A proxy that
accepts at once but bridges onward after a pause restores the window — tcpser
reports `CONNECT` as soon as *its* connection succeeds, so the delay lands after
the result code and before the first byte of the login page:

```
socat TCP-LISTEN:6401,reuseaddr,fork \
  SYSTEM:'sleep 15; exec socat STDIO TCP\:nightowlbbs.ddns.net\:6400'
```

Point the phonebook at the proxy (`-n 1=localhost:6401`). `fork` gives each
redial a fresh delay, and the board does not see the call until the pause
expires.

This mattered more before `CTRL-M` was known to work. Dialling from Terminal
mode on a board whose viewdata module ignores the Prestel star commands left no
way to recover the garbled first page -- `<f8>` and `*00#` ask the host to
retransmit, and a host that ignores them leaves you with the mosaic copy that
arrived during the hop. Dialling from Prestel mode avoids the situation
entirely, since Commstar is never in the wrong mode.

### Worked example: EOTL viewdata

[End Of The Line](https://www.endofthelinebbs.com/) runs Synchronet with a
viewdata front door on port **6502**, at 7E1. Unlike a single-line board it is
always up and multi-user, which makes it the better target for testing. Guest
access is username `Guest` with an empty password.

```
tcpser -v 25232 -s 2400 -l 4 -n 1=endofthelinebbs.com:6502
```

Put Commstar in Prestel mode (`<#>`), enter chat (`<C>`), type `ATDT1` and press
`CTRL-M`. The login frame arrives into a terminal already in viewdata mode, so
there is no hop to race and no delay proxy to arrange.

Dialling from Terminal mode instead works too, but then the board's greeting
arrives while Commstar is still rendering ASCII, and EOTL does not honour the
Prestel retransmit commands that would redraw it -- so pair that route with the
delay proxy above, pointing the phonebook at `127.0.0.1:6401`.

Keep the word format at the 7E1 Prestel mode selects, but raise the rates: at
the default 1200/75 a frame takes some eight seconds to paint, and at 9600/9600
(with `tcpser -s 9600`) it is under a second. A 40x24 frame is under 1 KB
against a 4 KB receive queue, so a page arriving in one burst has headroom at
either speed.

### Connecting to any other board

The two examples above differ only in their details. The general procedure:

**1. Find the board and read its terminal options.**
[telnetbbsguide.com](https://www.telnetbbsguide.com/) lists active systems with
hostnames, ports and opening hours. What matters is which *front door* to use: a
board may offer several on different ports, and they are not equivalent to a BBC.

| the board offers | use | why |
|---|---|---|
| viewdata / videotex (often 7E1) | Commstar Prestel mode | 40x24 teletext, exactly what Mode 7 renders |
| ASCII / ANSI (usually 8N1) | Commstar Terminal mode | connects fine, but ANSI colour and 80 columns will not render well |
| PETSCII, RIP, SSH | nothing useful | wrong machine |

Prefer a viewdata port if one exists. Note the opening hours and whether the
board is single-line -- a single line answers `BUSY` when someone else is on.

**2. Point a virtual modem at it.** The BBC dials tcpser; tcpser dials the board.
Match `-s` to the board's rate and put the address in the phonebook so it is
typed once rather than through the emulated keyboard:

```
tcpser -v 25232 -s 2400 -l 4 -n 1=<host>:<port>
```

**3. Add a delay window only if you must change terminal mode after dialling**
-- see the proxy above. Over TCP there is no dial-and-train-up pause, so the
board's first page arrives while the terminal is still in the wrong mode, and a
board that does not honour the Prestel retransmit commands gives you no way to
redraw it. Terminal software that can send a CR without leaving viewdata mode
(on Commstar, `CTRL-M`) sidesteps this entirely.

**4. Terminate the AT command with a carriage return the terminal can actually
send.** Viewdata emulation maps `<RETURN>` to the "next frame" character, so on
Commstar use `CTRL-M`, which sends a real CR and lets you dial without leaving
viewdata mode. Other terminal software will differ; if it has no such key, dial
from its ASCII/terminal mode and switch afterwards, during the delay window.

**5. Keep the word format, raise the rates.** IP232 carries bytes, not bits, so
the guest's word format and baud only have to be self-consistent -- they need not
match the board. Let Commstar's Prestel mode select 7E1 and leave that alone,
but cycle `<R>` and `<S>` in `<I>` up to 9600 (matching `tcpser -s`): the
default 1200/75 is faithful to Prestel and painfully slow, and nothing about the
link requires it.

**6. Log off through the board** rather than closing the emulator, so the line is
freed at once -- `*90#` on a viewdata system, which on Commstar means `*`, `9`,
`0`, `<RETURN>`. If the board stops responding, hang up at the modem instead:
`+++`, wait for `OK`, then `ATH0` terminated with `CTRL-M`.

When it does not work, the `-t sS` trace answers most questions: it shows exactly
what the BBC sent (so a mis-typed hostname is visible), and it distinguishes the
modem's own result codes from text the board sent -- see
[Troubleshooting](#troubleshooting).

### Raw mode

For a plain socket peer that just wants bytes, with the connection gated on RTS:

```
beebium-model-b start --ip232-serial host=127.0.0.1:port=10001:mode=raw
```

### In a preset

Like any extension, it can be pinned in a preset; `create-preset` captures the
`--ip232-serial …` flags into the preset's `extensions` array.

## Troubleshooting

- **Nothing is transmitted / the program hangs sending.** The connection is
  probably down, so `/CTS` is held and the guest's transmit loop is waiting. Check
  the server is listening on `host:port`; `describe-extension` confirms the
  defaults. A refused connection is reported on the server's stderr.
- **A `0xFF`-heavy binary transfer looks corrupted in `raw` mode.** Raw mode does
  not escape `0xFF`; use `ip232` mode (the default) against a tcpser-style server.
- **The link drops and does not come back in `raw` mode.** Raw mode follows RTS;
  it reconnects when the guest re-asserts RTS. `ip232` mode reconnects
  automatically.
- **The call is dropped when the guest changes serial settings.** Software that
  re-initialises the ACIA mid-session (Commstar does this when switching between
  Terminal and Prestel emulation) can glitch RTS, which tcpser reads as the
  terminal going on-hook: the log shows `DTR has gone low` followed by
  `Disconnecting modem`, and the guest sees `NO CARRIER`. Set `handshake=false`
  so the BBC's RTS is not conveyed; nothing is lost, since a telnet host has no
  use for it.
- **`BUSY` after a successful `CONNECT`.** Check which side said it. tcpser's own
  result codes are wrapped in `0d 0a` on both sides and come from its modem
  thread; text forwarded from the remote host arrives on the IP reader thread,
  typically with different line endings. In a `-t sS` trace:

  ```
  6159380480: SR->|0d 0a| |CONNECT 2400| |0d 0a|   <- tcpser's result code
  6161100800: SR->|42 55 53 59 0a|  BUSY.          <- the remote host's own text
  6161100800: No socket data read, assume closed peer
  ```

  A preceding `Connection to <host> established` confirms the TCP connect
  succeeded, so this is the *board* refusing the call — a single-line BBS with
  its line in use — not a network or emulation fault. Redialling will not help
  until the far end frees up; note that a session abandoned without a proper
  logoff can hold the line until the board's own timeout expires.

## Relationship to RFC 2217

IP232 is a thin retro-comms hack (tunnel bytes + a couple of modem lines). RFC
2217 is the IETF "Telnet Com Port Control Option" — a standards-based way to
drive a *real* remote serial port (set its baud/parity, read its line states).
Beebium ships `rfc2217-client-serial` and `rfc2217-server-serial` as further
serial extensions reusing the same `beebium::net` TCP transport; see
[serial-rfc2217.md](serial-rfc2217.md) and the research notes in
[serial-network-ip232-rfc2217.md](discussion/serial-network-ip232-rfc2217.md).

## Implementation

- `src/extensions/ip232-serial/Ip232Codec.hpp` — the `0xFF` escape codec (pure,
  golden-vector tested against the BeebEm wire format).
- `src/extensions/ip232-serial/Ip232SerialEndpoint.{hpp,cpp}` — the
  `SerialPortDevice`: TCP transport + codec + reader/writer threads + RTS
  reaction + connection-as-`/CTS`.
- `src/extensions/ip232-serial/Ip232SerialExtension.{hpp,cpp}` — the built-in
  PeripheralExtension and its CLI/manifest config.
- `src/core/include/beebium/net/` — the shared `SocketPlatform.hpp` and
  `TcpClientSerialPort` (reused by the RFC 2217 extensions).

Tests: `tests/test_ip232_codec.cpp` (golden vectors),
`tests/test_ip232_serial_endpoint.cpp` (round-trip / RTS / reconnect against an
in-process loopback server), `tests/test_ip232_serial_extension.cpp` (config /
attach), and an opt-in `tests/test_ip232_tcpser_integration.cpp` that runs
against a real `tcpser` when one is available.

Authoritative protocol references: BeebEm `Src/IP232.cpp` + `Src/Serial.cpp`, and
[tcpser](https://github.com/go4retro/tcpser).
