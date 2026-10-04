# Serial subsystem: architecture review and design direction

Status: working notes, on the `feature/serial` integration branch.
Context: PR #44 (Mark Fisher, `feature/mc6850`) adds an MC6850 ACIA + Serial ULA
(SERPROC) plus a host-side serial transport. We have accepted it onto
`feature/serial` (merged with `--no-ff`, preserving the contributor's history)
and intend to evolve it toward what we want before it reaches `master`.

These notes capture (a) what the PR does well, (b) two corrections/decisions
that change the design, and (c) the remaining gaps and a plan. They exist so we
can deal with the issues systematically rather than from memory.

## What the PR gets right

- The emulated chips (MC6850 ACIA, Serial ULA / SERPROC) live in core and
  mirror the `EconetSocket` pattern closely: memory-mapped regions
  (`&FE08-&FE0F` ACIA, `&FE10-&FE17` ULA), `tick_rising`/`tick_falling`,
  IRQ-aggregator binding, hard/soft reset, open-bus pointer. The ACIA/ULA are
  integral host-side hardware, so modelling them in core (not the extension
  framework) is correct, consistent with the MC6854 ADLC.
- A clean byte-level transport seam already exists:
  `SerialDataSource` / `SerialDataSink` (see `serial/SerialDevice.hpp`). This is
  the seam we want; the disagreement below is only about *what plugs into it*
  and *where that lives*.
- Faithful bit-level timing at an emulator-friendly cadence (`CPU_HZ/baud`
  ticks per bit), honestly documented as not cycle-accurate to the 16x sampling
  clock - consistent with our MC6854 rationale.
- Good test coverage (Catch2 for ACIA/ULA/socket/PTY; Python unit +
  integration). Builds and passes on macOS including the PTY round trip, so the
  POSIX transport is already Mac-clean.

## Correction 1: the serial port is NOT always fitted (two axes we model)

The PR (and my first-pass review) assumed the serial socket is always present.
That is wrong. There are two capabilities we choose to model, which do not track
each other across variants:

- Axis A - the serial chips + RS423 (MC6850 ACIA + Serial ULA, and the RS423
  port they drive). A both-or-neither pair, so a single `SerialSocket` container
  is correct; we do NOT need separate sockets for the two chips.
- Axis B - the cassette interface (driven by the same ULA: latch bit 6 select,
  bit 7 motor relay).

Note on the real hardware vs what we model: the Model A actually fitted the chips
(for cassette) but omitted the 5-pin domino DIN RS423 connector, so a real
Model A had cassette without a usable serial port. We deliberately do NOT model
that connector-absent case: Beebium treats the Model A as having serial support
(RS423) alongside cassette, the same as a Model B. So "chips present" and "RS423
usable" are one and the same in our model (Axis A). The only place chips and
cassette come apart is the Master Compact.

Per variant (none of A / Compact are implemented in Beebium yet; B / B+ /
Master 128 are):

| Variant        | A: chips + RS423 | B: cassette |
|----------------|------------------|-------------|
| Model A        | yes              | yes         |
| Model B        | yes              | yes         |
| Model B+       | yes              | yes         |
| Master 128     | yes              | yes         |
| Master Compact | optional         | NO (never)  |

The instructive corner case is the Master Compact: no cassette at all (disc or
Econet only), and the serial hardware is an option (Rob's own Compact has none).
So on the Compact the chips serve RS423 only, and "chips fitted" is the same
decision as "RS423 fitted" - but cassette is absent even when they are fitted.
That is why Axis B cannot simply be derived from Axis A.

Design implications:

- One `SerialSocket` (chips + RS423) gated by Axis A. The `HasSerialSocket<Memory>`
  concept the PR uses is the right mechanism; make every variant opt in/out
  correctly, and let the Compact express "fitted or not" as configuration, not a
  compile-time fact. The `SerialPortDevice` attachment point (Correction 2) is
  present exactly when the `SerialSocket` is.
- Cassette availability (Axis B) is its OWN per-variant property, not "wherever
  the chips are". It is present on A/B/B+/Master128 and absent on the Compact.
  When we add cassette, gate it separately (see the cassette note under
  Correction 2) - do not derive it from chip presence.
- The whole stack above core must tolerate absence gracefully:
  - `SerialService.GetSerialStatus` already returns `has_serial_socket=false` -
    good; keep that contract and make the clients honour it. We likely want a
    second flag for cassette presence rather than a single has-serial bit.
  - Any future UI (panel/indicator) must hide or disable itself when the machine
    has no serial socket, the same way Econet UI keys off station config.
  - `--serial` on a machine with no serial socket should be a clear error, not a
    silent no-op.
- This also weakens the "it's always there, so it's not pluggable" argument for
  keeping serial out of the extension framework. The chips are optional on the
  Compact - closer to a fitted option than to RAM.

## Correction 2: the host-serial bridge belongs in an extension

The PR bakes the *host transport* into core: `PtyMaster`, `HostSerialEndpoint`,
`PosixSerialPort`, `Win32SerialPort`, plus a bespoke `SerialService` gRPC
registered directly in `Server.hpp`. We do not want "bridge the BBC serial port
to a real host serial device" to be a core responsibility.

Reasoning:

- The BBC serial port is an *external interface*. What sits on the other end of
  it is open-ended. Connecting it to a real host serial device (via PTY or an
  opened device path) is only ONE option.
- The motivating case, FujiNet (https://fujinet.online), is a *real* hardware
  device: the BBC FujiNet connects over the real RS423 serial port, and Mark is
  driving a real FujiNet on his Linux host's serial port. So the host-serial
  bridge is a genuine, first-class need - not a test crutch. It just should not
  be the *only* way to populate the far end of the wire, nor a core concern.
- An emulated serial-connected device is equally valid: emulating a FujiNet
  (or a modem, serial printer, mouse, or another emulated machine) directly
  inside Beebium, never touching a real serial interface. Such a device speaks
  to the ACIA/ULA over the same seam but is not "a host serial port".
- This is exactly the core-vs-extension boundary we already drew for Econet (the
  ADLC is core; the AUN/Piconet *backend* is an extension behind a
  `NetworkBackend` seam) AND the one we use for the User Port and 1MHz bus: core
  exposes a device seam, extensions plug devices into it. Serial should follow
  the same convention - see below.

### Align with the existing external-port convention

We already have a settled vocabulary and mechanism for plugging external
peripherals into the BBC's ports, and serial should reuse it rather than invent
a parallel one. The convention (in `src/core/include/beebium/extension/`):

- A `...Device` seam is the callback interface an extension implements:
  - `UserPortDevice` (`update_port_b`, `update_control_lines`) attaches to the
    `UserPort` handle.
  - `OneMHzBusDevice` (`read`, `write`, `tick`, `irq_pending`) attaches to the
    `OneMHzBusPort` handle (daisy-chained across address ranges).
- A `...Socket` is an *optional-hardware container* that may be populated or
  empty: `TubeSocket`, `EconetSocket`, `DiscControllerSocket`, and the PR's
  `SerialSocket`. (This is the right vocabulary for Correction 1: a Socket is
  allowed to be absent, which is precisely Model A / Master Compact.)
- A `...Port` is an always-present multiplexer that dispatches to attached
  Devices (`UserPort`, `OneMHzBusPort`).
- Extensions derive from `PeripheralExtension`, declare `attaches_to()` /
  `provides()`, and in `init(ExtensionContext& ctx)` attach themselves via
  type-safe `ctx.get<UserPort>()` / `ctx.get<OneMHzBusPort>()`.

Target shape for serial, by analogy:

- Core owns: the ACIA, the ULA, and the `SerialSocket`. Nothing about PTYs,
  device paths, or host OS serial APIs.
- Introduce a `SerialPortDevice` seam (the far end of the RS423/cassette
  connector), the serial analogue of `UserPortDevice` / `OneMHzBusDevice`. The
  PR's `SerialDataSource` + `SerialDataSink` already ARE this seam, just split in
  two and not named to the convention; fold them into a single bidirectional
  `SerialPortDevice` (or keep the split but rename to the house pattern).
- Expose the attachment point through `ExtensionContext` so a
  `PeripheralExtension` can attach a `SerialPortDevice` exactly the way the RTC
  attaches to the User Port and SCSI attaches to the 1MHz bus. The
  `SerialSocket`'s existing `set_source`/`set_sink` is the wiring underneath.
- Then "bridge to a real host serial port" and "emulated FujiNet" become two
  ordinary `PeripheralExtension`s implementing the same `SerialPortDevice`
  seam - which is the user's point that the real-serial bridge "should work the
  same way". Likely a single built-in extension provides today's
  `device` / `pty` / `loopback` host-serial modes; emulated devices are separate
  extensions added later.
- Configuration and UI then come "for free" via the extension framework and
  `ExtensionUiService` (see `docs/discussion/extension-ui-architecture.md`),
  which is how AUN/Piconet get macOS + TypeScript client panels and indicators.
  See also `docs/discussion/serial-port-selector-control.md` for a
  domain-specific control we could reuse for device-path selection.

Naming collision to resolve: the PR already uses `SerialPort` /
`PosixSerialPort` / `Win32SerialPort` for the *host OS* serial port abstraction.
That name will clash with a BBC-side serial seam. Rename the host-side primitive
(e.g. `HostSerialPort`) and reserve serial/`SerialPort...` names for the BBC
seam, or keep `SerialPortDevice` for the seam and leave the host primitives
clearly host-scoped. Decide before cutting the extension.

Where the OS primitives live: `SerialPort` / `PosixSerialPort` /
`Win32SerialPort` / `PtyMaster` do not need to be in core just to be shared. The
PR promoted them from the Piconet extension to core for a single source of
truth; a cleaner home may be a small shared library that both the Piconet
extension and the host-serial extension depend on, so neither core nor either
extension owns the other's transport.

Open question: the ACIA/ULA are genuinely core hardware and must stay core even
with no device attached (the BBC can still read/write the registers, get
framing/parity/overrun, drive /DCD from the cassette/RS423 select, etc.). So the
seam needs a well-defined "nothing attached" behaviour (RX idle, TX discarded)
in core, independent of any extension - the NONE case. Keep that in core and
push everything else out to extensions.

Cassette (in scope going forward, see Correction 1 Axis B): the Serial ULA also
drives the cassette interface (latch bit 6 RS423/cassette select, bit 7 motor
relay). Cassette is the reason the chips exist at all on a Model A, so when
present it shares the `SerialSocket` and lives in core with the chips. But its
presence is its own per-variant axis - present on A/B/B+/Master128, absent on the
Compact (disc/Econet only) - so gate it separately rather than deriving it from
chip presence. The PR does not implement cassette I/O - only the RS423 byte
transport - which is fine, but the seam shape should not foreclose cassette. The
byte-level `SerialPortDevice` seam suits RS423 but not cassette audio encoding,
so cassette wants its OWN seam (a `CassetteDevice` attaching to the same chips
via the cassette-select path), not to be forced through the serial-byte seam.
Keep the RS423 seam RS423-shaped so it does not accidentally become the cassette
path too; add the cassette seam later.

## Other gaps (independent of the two corrections)

- Status is polling, not streaming. `SerialService` only offers a
  `GetSerialStatus` snapshot. We already moved Econet to a server-pushed
  `WatchEconetStatus` stream and treat streaming as the convention. A new
  interface should ship with `WatchSerialStatus` from the start.
- Client parity. Python client only. No TypeScript client (we just completed the
  TS cutover for AUN/Piconet/Econet) and no macOS Swift client. Proto stubs are
  Python-only.
- Not presettable. `--serial` is CLI/RPC only; `serial_spec` is not in the
  preset schema. A serial setup (e.g. a FujiNet ROM in a sideways slot plus a
  transport) is a natural preset. Once transport selection moves to an
  extension, this should fold into the existing extension-configuration-in-preset
  mechanism rather than being a special case.
- Unbounded TX growth in the default mode. `SerialServiceImpl` attaches a
  `ScriptableSerialEndpoint` by default, whose `tx_` is an unbounded
  `std::deque`. A ROM that streams serial out while nothing drains it grows
  memory without bound. Prefer defaulting to NONE (observably identical: TX
  discarded) and only attaching scriptable on request, or cap the queue.
- CLI separator heuristic. `device:PATH:baud` splits on the last colon and
  guesses the tail is baud "if numeric". Fragile and inconsistent with our
  shell-safe separator direction (AUN moved to `@`; is_list consumers own their
  inner separator). Tidy when the extension owns its own argument parsing.
- Duplicated spec parsing. The `--serial` string-to-mode parsing is in
  `ServerMain.hpp`; the mode-to-action logic is in `SerialService`. Parse the
  spec in one place.
- Windows. `Win32SerialPort.cpp` exists but is unexercised (contributor is on
  Linux); PTY mode is reasonably unsupported on Windows. Device mode needs
  validation on the Windows dev machine (Slioch).
- The reader-thread mutex in `HostSerialEndpoint` is a genuine cross-thread
  queue (reader thread vs emulation thread), so it is justified, not a defensive
  mutex - acceptable under our threading principle.

## Proposed plan (on feature/serial)

The detailed, phased, cross-platform-first execution plan lives in
`docs/discussion/serial-refactoring-plan.md`. The high-level order is:

1. Settle the extension boundary (Correction 2): define a `SerialPortDevice`
   seam and attach it via `PeripheralExtension`/`ExtensionContext`, the same way
   the RTC uses `UserPortDevice` and SCSI uses `OneMHzBusDevice`; resolve the
   `SerialPort` naming collision. This is the load-bearing decision; it changes
   where the transport code lives, how it is configured, and how the GUI clients
   see it.
2. Make presence per-variant and absence-tolerant end to end (Correction 1).
3. Replace the snapshot RPC with `WatchSerialStatus` streaming.
4. Add TypeScript and macOS clients once the extension/UI shape is fixed.
5. Fix the unbounded-TX default; consolidate spec parsing; fold serial config
   into preset/extension configuration.
6. Validate the Win32 device path on Slioch.

## References

- PR #44: https://github.com/rob-smallshire/beebium/pull/44
- Contributor use case: fn-rom (BBC FujiNet ROM) end-to-end serial tests.
- `docs/serial-acia.md` (contributor's reference for registers/timing/transport).
- `docs/discussion/extension-ui-architecture.md`
- `docs/discussion/serial-port-selector-control.md`
- `docs/networking.md`, "Econet Across the Stack" (the ADLC-core / backend-extension precedent).
- The external-port device convention to mirror:
  `src/core/include/beebium/extension/UserPortDevice.hpp` + `UserPort.hpp`,
  `OneMHzBusDevice.hpp` + `OneMHzBusPort.hpp`, `PeripheralExtension.hpp`.
  Worked examples: the Acorn RTC (User Port) and the Acorn SCSI host adapter
  (1MHz bus) in `src/extensions/`.
- FujiNet (the motivating real device): https://fujinet.online
