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

**3. Dial from Commstar's Terminal mode — not Prestel mode.** At the main menu
check the top line reads `Emulate : Terminal` (`<#>` toggles). Then:

- `<I>` to initialise the RS423: word format option **5** (8 bits, no parity, 1
  stop) with `<R>` and `<S>` both cycled to **2400**. `<RETURN>` to go back.
- `<C>` for chat mode, then type `ATDT1` and press `<RETURN>`.

tcpser logs the dial and answers `CONNECT 2400`.

**Why Terminal mode matters:** in Prestel mode Commstar maps `<RETURN>` onto the
viewdata "proceed to next frame" character — which is *not* ASCII CR, but the
code Prestel uses for `#` (ASCII underscore, `0x5F`). An AT command typed from
Prestel mode therefore never receives its carriage return, and the modem sits
waiting forever. Commstar's own manual documents the mapping in section 5.3.

**`CTRL-M` avoids the mode switch entirely**: it transmits a genuine carriage
return from Prestel mode, so an AT command can be typed without leaving viewdata
emulation. In Prestel mode the three keys that look alike are:

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

**4. Switch to Prestel emulation once connected.** Viewdata pages are teletext,
so they only render correctly in Prestel mode: `<ESCAPE>` back to the menu (the
line stays up and incoming data is buffered), `<#>` to select
`Emulate : Prestel`, then `<C>` to return to chat. If a page arrived while you
were still in Terminal mode it will look like mosaic characters printed as
ASCII; `<f8>` asks the board to retransmit the current frame.

Entering Prestel mode reconfigures the line to viewdata's **7E1 at 1200/75**.
That is correct and expected — Commstar's manual is explicit that the word format
should *not* be altered while using Prestel — and Beebium receives it properly.
The BBC's rate need not match the board's: IP232 is a byte pipe with no bit
timing of its own, so the guest's receive rate only governs how fast the socket
queue drains.

**5. Log off properly.** `*90#` is the conventional viewdata logoff (the board
shows a goodbye frame and drops the line). Walking away instead leaves the line
held until the board times out, which on a single-line system locks everyone else
out. If the board is unresponsive, hang up at the modem instead: type `+++` (no
RETURN), wait for `OK`, then `ATH0` to go on-hook. `ATH0` needs a real carriage
return, so return to Terminal mode first — the same restriction that applies to
dialling.

#### Optional: a dialling delay

Over TCP, `CONNECT` is instantaneous, so there is no dial-and-train-up pause in
which to do the Terminal-to-Prestel hop that a real modem would have given you.
A proxy that accepts at once but bridges onward after a pause restores the
window — tcpser reports `CONNECT` as soon as *its* connection succeeds, so the
delay lands after the result code and before the first byte of the login page:

```
socat TCP-LISTEN:6401,reuseaddr,fork \
  SYSTEM:'sleep 15; exec socat STDIO TCP\:nightowlbbs.ddns.net\:6400'
```

Point the phonebook at the proxy (`-n 1=localhost:6401`). `fork` gives each
redial a fresh delay, and the board does not see the call until the pause
expires.

On a board whose viewdata module does not implement the Prestel star commands,
this stops being a convenience and becomes the only way to get a clean first
page: `<f8>` and `*00#` ask the host to retransmit, and a host that ignores them
leaves you with the mosaic-garbled copy that arrived while Commstar was still in
Terminal mode. The delay window sidesteps the problem by having Commstar already
in Prestel mode when the first byte lands.

### Worked example: EOTL viewdata, with the delay window

[End Of The Line](https://www.endofthelinebbs.com/) runs Synchronet with a
viewdata front door on port **6502**, at 7E1. Unlike a single-line board it is
always up and multi-user, which makes it the better target for testing. Guest
access is username `Guest` with an empty password.

```
socat TCP-LISTEN:6401,reuseaddr,fork \
  SYSTEM:'sleep 20; exec socat STDIO TCP\:endofthelinebbs.com\:6502'

tcpser -v 25232 -s 2400 -l 4 -n 1=127.0.0.1:6401
```

Dial `ATDT1` from Commstar's Terminal mode; `CONNECT 2400` comes back at once
because the proxy accepts immediately and the board has not been contacted yet.
Then `<ESCAPE>` → `<#>` → `<C>` inside the twenty seconds, and the login frame
paints into a terminal that is already in viewdata mode. Raise the `sleep` if
that feels tight.

Leave the line settings alone throughout: Prestel mode selects 7E1 at 1200/75
and that is correct. The BBC's rate need not match the board's, and a 40x24
frame is under 1 KB against a 4 KB receive queue, so a page arriving in one
burst has ample headroom even though the guest drains it at 120 bytes a second.
A frame takes about eight seconds to paint -- which is exactly what Prestel felt
like.

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

**3. Add a delay window if the board greets on connect** and you will need to
change terminal mode after dialling -- see the proxy above. Over TCP there is no
dial-and-train-up pause, so without one the first page arrives before the
terminal is ready. Required rather than optional if the board does not honour
the Prestel retransmit commands.

**4. Dial from a mode that can send a carriage return.** AT commands need CR, and
viewdata emulation does not transmit one (see above). So dial from Terminal mode
and switch afterwards, during the delay window.

**5. Leave the line settings to the terminal software.** IP232 carries bytes, not
bits, so the guest's word format and baud only have to be self-consistent -- they
need not match the board. Let Commstar's Prestel mode select 7E1 at 1200/75.

**6. Log off through the board** rather than closing the emulator, so the line is
freed at once. `+++` then `ATH0` hangs up at the modem if the board stops
responding, and needs Terminal mode for the same CR reason.

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
