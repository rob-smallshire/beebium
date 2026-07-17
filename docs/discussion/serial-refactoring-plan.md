# Serial subsystem: step-by-step refactoring plan

Status: working plan, on the `feature/serial` integration branch. Companion to
`docs/discussion/serial-architecture-review.md` (the why); this is the how.

Goal: evolve Mark Fisher's PR #44 (MC6850 ACIA + Serial ULA + host transport)
into the shape the review settled on -- the emulated chips in core, the
host-serial bridge as a built-in `PeripheralExtension` behind a `SerialPortDevice`
seam -- without ever leaving the tree broken, and with all three platforms green
at every step.

## Working principles

- Green ratchet: every phase ends with a commit that builds and tests clean on
  all three platforms. Small commits (see [[feedback_commit_ratchet]]).
- All-platforms-first: we do NOT begin refactoring until the as-merged code is
  green on macOS, Linux and Windows (Phase 0). Each later phase re-runs the same
  triad before it is considered done.
- Tests travel with the change: each phase names the tests it adds or moves. No
  phase is "done" on a successful build alone.
- No behaviour change until intended: Phases 1-2 are rename/structure only and
  must not alter runtime behaviour; behaviour moves in Phase 3+.
- Fix tools before leaning on them ([[feedback_fix_tools_first]]); targeted
  tests while iterating, full suite at phase end ([[feedback_targeted_tests]]).

## The cross-platform verification triad

Run these at the end of every phase. The exact serial test names are
`test_mc6850`, `test_serial_ula`, `test_serial_socket`, `test_serial_pty`
(POSIX only), plus the piconet ports tests that share the host primitives
(`test_posix_serial_port`, `test_win32_serial_port`).

macOS (local, here):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DBEEBIUM_BUILD_TESTS=ON -DBEEBIUM_BUILD_SERVICE=ON -DBEEBIUM_BUILD_SERVER=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R 'serial|mc6850|posix_serial|win32_serial|piconet'
ctest --test-dir build --output-on-failure   # full suite at phase end
```

Linux (Docker, mirrors the `server-linux` CI job; see `docs/linux-container.md`):

```bash
docker build -t beebium-linux-ci docker/linux-ci/   # first time / on Dockerfile change
docker run -d --name beebium-linux -v "$(pwd)":/workspace/beebium:ro -w /workspace \
  beebium-linux-ci sleep infinity
docker exec beebium-linux bash -c \
  "rm -rf /workspace/src && cp -r /workspace/beebium /workspace/src && rm -rf /workspace/src/build*"
docker exec beebium-linux bash -c \
  "cd /workspace/src && cmake -B build-linux -DCMAKE_BUILD_TYPE=Release \
     -DBEEBIUM_BUILD_TESTS=ON -DBEEBIUM_BUILD_SERVICE=ON -DBEEBIUM_BUILD_SERVER=ON && \
   cmake --build build-linux --parallel 2 && \
   ctest --test-dir build-linux --output-on-failure"
```

Windows (Slioch, over SSH): follow the documented workflow in
`docs/local-windows-development.md` (gitignored, machine-specific: SSH command,
VS DevShell preamble, configure/build/test commands, repo path). Do not
duplicate those commands here -- that doc is the single source of truth for the
Slioch setup.

The one wrinkle that doc does not cover: it assumes a `git pull`, but
`feature/serial` is not pushed. Two options, in order of preference:
- Rob pushes `feature/serial` once, then Slioch uses the doc's normal `git pull`
  / build / test flow each phase.
- Otherwise move the branch as a git bundle (no origin push):
  `git bundle create /tmp/serial.bundle master..feature/serial`, scp it over,
  then fetch from the bundle on Slioch and build/test per the doc.

Client suites (run when a phase touches a client or the proto):

```bash
# Python
cd clients/beebium-python-client && uv run --with pytest python -m pytest tests/ -v
# TypeScript (once a serial client exists)
cd clients/beebium-typescript-client && npm ci && npm run generate-protos && npx tsc --noEmit && npm run test:unit
# macOS GUI (once a serial panel exists)
xcodebuild test -scheme BeebiumTests -destination 'platform=macOS'
```

## Phase 0 -- Establish the green cross-platform baseline (no refactoring)

Prove the as-merged PR builds and tests on all three platforms before touching
it. This is the "start from everything working on all platforms" step and gives
us the regression net for everything after.

1. macOS: full configure/build/test (serial suite already passes here; confirm
   the service+server build and the full ctest are clean).
2. Linux/Docker: build-linux + full ctest. Expect possible GCC-vs-Clang and
   `<pty.h>` vs `<util.h>` / `posix_openpt` portability nits; fix minimally.
3. Windows/Slioch: bundle across, configure/build/test. Confirm:
   - `Win32SerialPort.cpp` compiles and links into `beebium_core`;
   - `test_serial_pty` is correctly skipped (`if(NOT WIN32)`);
   - `SerialService` builds (it includes `Win32SerialPort.hpp` under `#else`);
   - the `--serial device:` path is reachable.
4. Gaps to record, not necessarily fix yet:
   - There is no automated Windows serial round-trip (PTY is POSIX-only). Note
     options for later: a `com0com` null-modem pair on Slioch (manual), or a
     Windows named-pipe endpoint analogous to the PTY test.
   - CI builds serial everywhere but runs no serial transport round-trip on
     Windows; flag for Phase 7.

Commit: "serial: cross-platform baseline green" (with any portability fixes).
Tests: no new behaviour; ensure existing serial tests pass on macOS+Linux and
build on Windows.

## Phase 1 -- Introduce the device seam (DONE, commit 4d74a7f)

6. Introduce the `SerialPortDevice` seam -- the serial analogue of
   `UserPortDevice` / `OneMHzBusDevice`. Rather than folding `SerialDataSource` +
   `SerialDataSink` away (which would break the ULA's per-direction unit tests,
   since a single bidirectional loopback echoes a TX byte back mid-test), we
   KEPT the two half-seams as the bit engine's internal interface and made
   `SerialPortDevice` inherit both. So the ULA still pulls RX / pushes TX through
   the split (tests untouched), while the socket and the outside world speak one
   device. `SerialSocket` gained `set_device(shared_ptr<SerialPortDevice>)` in
   place of `set_source`/`set_sink`; `Loopback`/`Scriptable`/`Host` endpoints and
   `SerialService` updated to match. No behaviour change.
   Tests: existing serial tests still pass on all three platforms; added a
   socket-level `set_device` loopback round-trip test.

5. (MOVED to Phase 3) Resolve the `SerialPort` naming collision. The PR uses
   `SerialPort` / `PosixSerialPort` / `Win32SerialPort` / `PtyMaster` for the HOST
   OS port. Renaming these to a host-scoped family frees `SerialPort` for the
   BBC-side port handle -- but that handle is not introduced until Phase 3, and
   the rename is large churn across the whole Piconet subsystem and its tests.
   Deferred to Phase 3 (step 8a) so the rename lands together with the consumer
   that actually needs the freed name, rather than as speculative churn now.

## Phase 2 -- Model presence correctly (DONE)

See review Correction 1: Axis A = chips + RS423 (one `SerialSocket`,
`HasSerialSocket`), Axis B = cassette (own per-variant flag; absent on Compact).

7. DONE. `HasSerialSocket` already gates Axis A correctly: it detects the
   serial_socket member, so all current Model B variants satisfy it and a future
   Master Compact that omits the member is automatically excluded; the stack
   already guards on it (e.g. SerialService reports has_serial_socket = false,
   and apply_endpoint_mode errors with "Machine has no serial socket"). Hardened
   the concept's documentation to the two-axis model and added static_assert
   coverage (positive: ModelBHardware; negative: a socket-less stub) so a future
   socket-less variant is provably excluded.
8. DEFERRED with cassette. Axis B (cassette presence) is its own per-variant
   property, but there is no consumer for a cassette-presence flag yet and
   inventing the per-variant mechanism now would pre-empt the cassette follow-up
   (Phase 6). So the flag moves to that follow-up. The obligation discharged
   here is only that cassette is NOT derived from `HasSerialSocket` -- recorded
   in the concept's doc comment.
   Tests: static_assert that `HasSerialSocket` is true for Model B and false for
   a stub without the socket (the gating mechanism the whole stack relies on).

## Phase 3 -- Extract the host-serial bridge into a built-in PeripheralExtension

The core architectural move. Mirror the User Port / 1MHz bus pattern.

Design decisions (confirmed with Rob): mirror `UserPort` / `UserPortDevice`, NOT
the Tube (TubeSocket-in-ExtensionContext is a special coprocessor case, not the
peripheral-attachment convention). Names: BBC handle = `beebium::SerialPort`,
device = `beebium::SerialPortDevice`. Ownership: the device slot is NON-OWNING
(`UserPort::attach(UserPortDevice&)` model) -- attachers own the device (the
extension owns itself; SerialService keeps its scriptable/loopback in its own
members; tests keep locals).

8a. DONE (commit 11dbb2c, green macOS+Linux+Windows). Renamed the host OS serial
    port `beebium::serial::SerialPort` -> `HostSerialPort` (Posix/Win32/Pty
    derive from it unchanged); the piconet shim keeps `beebium::piconet::SerialPort`
    as an alias to `HostSerialPort`, so Piconet code/tests are untouched. This
    frees `beebium::SerialPort` for the BBC port handle.
9. DONE (commit ef5bda2, green macOS+Linux+Windows). Added `beebium::SerialPort`
   (extension/SerialPort.hpp): `attach(SerialPortDevice&)` / `detach()` /
   `is_occupied()`, single-device, throws on a second attach -- the `UserPort`
   analogue, header-only, non-owning. `SerialSocket::set_device` is now
   non-owning (raw `SerialPortDevice*`); SerialService keeps its endpoints alive
   and passes `.get()`. All four Model B variants expose `serial_port()`;
   `HasSerialPort` concept added; `ExtensionContext` gains `SerialPort*` (get/has),
   wired from ServerMain. Handle unit test added (attach/occupancy/throw/detach/
   round-trip). No consumer attaches yet -- that is step 10.
10. Create a built-in `HostSerialExtension : PeripheralExtension, SerialPortDevice`
    that owns the `HostSerialEndpoint` + host port + `PtyMaster` and offers the
    pty / device / loopback / scriptable modes. `attaches_to()` the serial port;
    register it in `BuiltinExtensions.hpp`.
11. Move transport selection out of `SerialService` into extensions. Done in
    slices, each green:
    - 11a DONE: removed pty/device from `SerialService` (host-serial owns them).
    - 11b DONE: built the `serial-loopback` and `rpc-serial` extensions.
    - 11c (TEARDOWN), in green-keeping order so no commit is broken:
      - 11c-A: add the Python `rpc-serial` client (regen stubs incl. the
        rpc_serial extension proto) and migrate the live-server scriptable/
        loopback integration tests onto `--rpc-serial` / `--serial-loopback`
        over the wire, while `SerialService` still has its endpoints. Additive.
      - 11c-B: remove `SetEndpointMode`/`Send`/`Receive`/the endpoint enum +
        transport fields and the default endpoint from `SerialService` and
        `serial.proto` (status-only); update the Python serial client to
        status-only; retire the `--serial` CLI. Breaking, but the tests already
        moved in 11c-A.
12. DONE on macOS+Linux (Windows pending Slioch). Bound the rpc-serial queues so
    no external client can leak memory or stall the emulator host (invariant:
    only the guest may stall, via faithful hardware back-pressure):
    - TX (Beeb -> device): `ScriptableSerialEndpoint` tx queue is bounded with a
      back-pressure mark that drives the MC6850 `/CTS` line (new
      `SerialDataSink::accepts_more()`, driven each TX bit period by the ULA).
      `/CTS` holds TDRE low so the guest's transmit loop busy-waits; lossless. A
      hard cap above the mark drops only if the guest also ignores `/CTS` (a real
      ACIA loses data there too); drops are counted.
    - RX (device -> Beeb): `RpcSerial.Send` is bounded and returns the `accepted`
      count (POSIX-write idiom); returns immediately, never blocks. Client
      retries the unaccepted tail.
    Tests: endpoint back-pressure + bounding; socket-level `/CTS` assert/release;
    `Send` cap returns `accepted`. Python `bbc.rpc_serial.send` returns accepted.

    DONE (no-stall invariant now holds for all three devices): `HostSerialEndpoint`
    used to write synchronously to the OS port on the emulation thread, so a stuck
    real peer could freeze the emulator. It now has an async TX queue + writer
    thread (mirroring its reader thread): `add_byte` only enqueues and returns;
    the writer drains to the port and backs off on EAGAIN. The queue is bounded
    and drives the same `/CTS` back-pressure seam (accepts_more) + hard-cap drop
    as RpcSerialEndpoint. Cross-platform `test_host_serial_endpoint` proves a
    stuck peer back-pressures without blocking the emulation thread. Lock-free
    queues are NOT needed -- serial is baud-rate-bounded (<20k ops/s), uncontended-
    mutex cost is <0.1% of the 2 MHz loop. See
    [[feedback_no_external_peer_stalls_emulator]].

    LAYERING: the concrete device endpoints moved out of core/serial (which now
    holds only the seam + bit engine + shared OS-port primitives) into their
    extensions -- RpcSerialEndpoint (rpc-serial), the folded loopback device
    (loopback-serial), HostSerialEndpoint (host-serial). Core ULA/socket tests use
    a tests/ SerialTestDevice fixture; ScriptableSerialEndpoint was renamed
    RpcSerialEndpoint (the old name described the mechanism, not the peer).

    CONFIGURABLE TX BUFFER + END-TO-END TEST: the TX back-pressure mark is now
    configurable per extension via `tx_buffer=N` (rpc-serial and host-serial CLI
    params; default 4096 from one shared constant serial::kDefaultTxBackPressure,
    no longer duplicated). This also enables a full-stack /CTS test driven by real
    BBC BASIC (clients/beebium-python-client tests/test_serial_cts.py): BASIC routes output to
    RS423 (*FX3,1) and transmits past a shrunk tx_buffer; with the rpc-serial
    client refusing to drain, the device buffer fills, the ULA asserts /CTS, and
    the MOS OSWRCH path blocks -- the guest stalls, the host stays live; draining
    unblocks it. The test synchronises by FEEDBACK (run_until_or_timeout polling
    the BASIC prompt / buffer fill / a zero-page completion sentinel), advancing
    emulated time -- no wall-clock sleeps, so it is robust on slow CI. Fixed the
    bit-rotted clients/beebium-python-client basic.py helper (memory.peek -> memory.address.peek)
    as part of this (it was unused and broken against the current memory API).

## Phase 4 -- gRPC and clients aligned with conventions

### Decompose Mark's SerialService (the gRPC-level expression of Correction 2)

Mark's PR added ONE gRPC service, `SerialService`, with four RPCs:
`GetSerialStatus`, `SetEndpointMode`, `SendToDevice`, `ReceiveFromDevice`, plus a
fixed transport enum `SerialEndpointMode { NONE, LOOPBACK, SCRIPTABLE, PTY,
DEVICE }`. It is a fine working interim, but it conflates THREE concerns that
belong in different places. We will decompose it along the `SerialPortDevice`
seam:

- (A) Hardware/diagnostics -- the ACIA + Serial ULA register state in
  `SerialStatus`. This observes the on-board chips, independent of what is
  attached to the wire, so it stays a thin CORE serial status surface (see
  step 13). This is the only part that remains "a serial service".
- (B) Transport SELECTION -- `SetEndpointMode`'s closed enum. "What is on the
  other end of the wire" is determined by which `PeripheralExtension` is
  configured (CLI/preset), attaching a `SerialPortDevice` via the `SerialPort`
  handle. The set of transports is OPEN-ENDED (any extension), not a fixed enum.
  `PTY`/`DEVICE` are really "the real host-serial bridge" -- one built-in
  extension (Phase 3 step 10/11). A future emulated FujiNet is another. So
  `SetEndpointMode` dissolves into per-extension configuration; it is NOT a
  long-term RPC.
- (C) A specific device's I/O -- `SendToDevice`/`ReceiveFromDevice` are not
  generic serial operations; they are the API of ONE device, the in-process
  SCRIPTABLE endpoint used by tests/automation. That endpoint is itself a
  `SerialPortDevice` provided by a (built-in) scriptable/test extension, and
  those two RPCs become THAT extension's own typed RPC -- not a monolithic
  serial service's.

Net target: one core serial status surface (A) + N transport/device extensions,
each owning its configuration and, where useful, its own typed RPC. The
monolithic fixed-menu `SerialService` does not survive as-is.

13. DONE (macOS + Linux; Windows pending Slioch). Added a server-pushed
    `WatchSerialStatus` stream (concern A) alongside the one-shot `GetSerialStatus`
    (kept -- still useful), mirroring `WatchEconetStatus`
    ([[project_econet_status_streaming]]). The stream writes an initial snapshot
    then pushes on change; rather than an Econet-style status-sequence counter
    (the ACIA's TDRE/RDRF toggle per byte), it samples at `min_interval_ms`
    (default 50) and pushes only when the serialized `SerialStatus` differs --
    self-contained in the service, no core change, and it coalesces rapid toggles.
    SerialService is already pure chip state (transport moved to extensions in
    11c). Python `bbc.serial.watch_status()` added (mock unit tests + a live
    integration test that pushes a ULA RS423/cassette change). TS/Swift clients
    are step 15.
14. "Keep BOTH surfaces" (decided) is now a PER-EXTENSION statement, not a
    monolith: each transport/device extension keeps a typed RPC for
    programmatic/scripting clients AND exposes `ExtensionUiService` for GUI
    clients (panel + indicator for free). Concretely:
    - host-serial bridge extension: owns pty/device config via CLI/preset +
      ExtensionUi, replacing `SetEndpointMode(PTY|DEVICE)`. DONE (step 10).
    - rpc-serial extension (renamed from the placeholder "test-serial"; and NOT
      "scriptable" -- that named the control mechanism): the client-driven peer.
      The RPC client IS the device on the other end of the wire; it owns
      `SendToDevice`/`ReceiveFromDevice` as its own typed RPC (concern C).
      "rpc-serial" names the domain role precisely and does not pigeonhole it as
      test-only (a real automation/scripting client may drive it).
    - serial-loopback extension: a SEPARATE, trivial built-in (DECIDED, revising
      the earlier "loopback is a mode of test-serial" note). Loopback is a
      self-contained TX->RX echo -- it is NOT RPC-driven, so it does not belong
      under "rpc-serial". One device = one extension is the purer split. It is a
      runtime "does my serial path work at all" smoke; zero config, no RPC.
    - future emulated FujiNet: its own typed RPC + UI.
    The underlying `LoopbackSerialEndpoint` and the client-driven endpoint stay
    as lightweight core `SerialPortDevice` test helpers in
    `serial/SerialDevice.hpp` (C++ unit tests construct them directly and attach
    via the `SerialPort` handle, no extension involved); the extensions wrap
    those classes for the RUNTIME-selectable forms.
    Typed RPCs and UI dispatch serve different audiences and are not redundant
    ([[feedback_extension_multi_api]],
    `docs/discussion/extension-ui-architecture.md`,
    [[project_serial_port_selector]]); keep both, but per extension.
15. Client parity: regenerate proto stubs; bring the Python client up to the new
    surfaces (a thin serial-status client + the rpc-serial extension's client);
    add TypeScript clients (parity with AUN/Piconet/Econet,
    [[project_typescript_client_cutover]]); add the macOS Swift client + serial
    UI panel/indicator (hidden when `has_serial_socket` is false).
    Tests: streaming test; Python integration (Linux + macOS + Windows, as the
    matrix already does for other services); TS unit + integration (Linux +
    macOS); macOS `BeebiumTests`. Triad for the C++ side.

    - Python: DONE -- `bbc.serial` (status + `watch_status` stream) and
      `bbc.rpc_serial` (send/receive/status), with unit + live integration tests
      (steps 11c/13 and earlier).
    - TypeScript: DONE -- `src/serial.ts` (`Serial`: getStatus + watchStatus) and
      `src/rpc_serial.ts` (`RpcSerial`: send/receive/getStatus), wired through
      connection.ts/client.ts (`bbc.serial`, `bbc.rpcSerial`) + index.ts; serial
      + rpc_serial protos added to generate-protos.sh. Tests: serial.test.ts,
      rpc_serial.test.ts (8 unit) + serial-integration.test.ts (stream + rpc
      round-trip over a real server). All pass; full TS suite green except two
      pre-existing tube-system-integration parasite-breakpoint failures unrelated
      to serial.
    - Swift / macOS: NO WORK NEEDED (verified by code inspection; Rob's call --
      "if our ducks are in a row, no Swift changes should be necessary"). The
      macOS client is a thin, fully data-driven renderer: `PeripheralTree.build`
      constructs the sidebar from whatever `ListExtensions` returns with no
      hardcoded extension whitelist; the `serial-port` extension point already
      has a display name; node labels come from the server (manifest display_name
      -- "Host Serial" / "RPC Serial" / "Loopback Serial");
      `hasUI` (= ui()!=nullptr) drives the panel; and `ExtensionViewRenderer`
      renders the generic Control tree (Label/Group, which is all our panels use)
      via a recursive switch. `PeripheralTreeTests` already exercises the tree
      generically, so a serial-specific Swift test would be redundant. With
      Phase 4.5 landed, the serial extensions surface in the Peripherals sidebar
      with zero macOS changes -- visually confirmable by launching with
      `--rpc-serial` / `--host-serial` / `--loopback-serial`.
    - The bespoke "SerialService chip-status panel" once sketched here is NOT
      needed: the extension panels cover the user-facing surface, and the chip
      register state (WatchSerialStatus) is a diagnostic surface available to the
      Python/TS clients. If ever wanted in the GUI it should be a generic
      ExtensionUi-style surface, not hand-written Swift.

## Phase 4.5 -- Serial extensions in the Peripherals sidebar (Extension UI)

DONE (macOS + Linux; the macOS Swift rendering rides along in step 15 Swift).
Only host-serial has an `ExtensionUi` (`HostSerialUi`) returned from `ui()`,
built with `beebium_extension_ui_proto` (the scsi-hard-disc pattern -- the UI
proto is always available, no BEEBIUM_BUILD_SERVICE gate). It shows the mode
heading (PTY / Device), the device path, and -- in device mode -- the baud, with
the path editable to re-point at runtime. rpc-serial and loopback-serial were
trimmed back to NO panel (`ui()` returns the base-class nullptr): a client-driven
peer and a fixed self-echo plug have nothing to show or configure (rpc-serial's
queue depths are debugger detail, available via `RpcSerial.GetStatus`). They
still surface in the sidebar BY NAME via the SAME generic discovery
(`PeripheralExtensionService.ListExtensions`); the macOS sidebar renders the
per-node panel only when `has_ui` (= `ui() != nullptr`) is set, so the panel-less
extensions list without a body. No client-side change needed. Tests:
test_host_serial_ui.cpp + a PTY-mode build_view assertion in
test_host_serial_extension.cpp. NOTE: Python has no peripheral-extension listing
client, so the end-to-end discovery check lives in C++
(test_grpc_peripheral_extension / test_grpc_extension_ui_service), not Python.

UI REFINEMENT -- host-serial interactive re-pointing (commit 6511001; mac+linux+
win): host-serial's panel is no longer read-only. HostSerialEndpoint is now
re-pointable at runtime (device path + baud) via the PiconetBackend cross-thread
pattern -- request_reopen (atomic slot) applied on the emulation thread's
has_data() tick (build a fresh port via a SerialFactory, swap under ui_mutex_,
restart the I/O threads), ui_snapshot() for the gRPC-thread read, and an
on_async_state_change -> ui_.mark_dirty() for hot-unplug / reopen. HostSerialUi
now offers a device ModalEditor (EditableChoice over serial::enumerate_ports() +
a baud-rate dropdown) + an open/error Indicator; always editable, no mode /
enable-disable (deferred). macOS renders ModalEditor/EditableChoice already, so
no Swift. rpc-serial / loopback panels remain the simple read-only form.

Inserted before Phases 5/6 (Rob's call). The three serial extensions have CLI
config + (rpc-serial) a typed RPC, but NO `ExtensionUi` surface -- so unlike
acorn-rtc / scsi-hard-disc / aun / piconet they do NOT appear in the macOS
Peripherals sidebar. (This corrects the earlier "step 14 is done" note: only the
typed-RPC half, concern C, landed; the GUI half lives here.) Give each extension
an `ExtensionUi` (subclass with `build_view(View*)` + `handle_event` +
`mark_dirty` to push, the AcornRtcUi/ScsiHardDiscUi pattern,
`docs/discussion/extension-ui-architecture.md`), discovered dynamically by id
like the transport panels ([[project_transport_ui_discovery]]), so serial shows
up via the SAME mechanism as every other extension -- no client-side hardcoding.

- host-serial: panel showing live config (mode pty/device, resolved path, baud,
  tx_buffer) + a connected/idle indicator; later, open/close controls.
- rpc-serial: indicator (tx/rx pending) + a panel noting it is client-driven.
- loopback-serial: a minimal "echo active" indicator (zero config).
- Live data from the extension's own state (and WatchSerialStatus where the chip
  view helps); the serial group is absent when `has_serial_socket` is false.
- Consider [[project_serial_port_selector]] (SerialPortSelector control) for the
  host-serial device/pty picker rather than a raw text field.
- Tests: per-extension `build_view` unit tests (acorn-rtc/scsi precedent) + macOS
  sidebar discovery; triad for the C++ side.

## Phase 5 -- Presets, CLI tidy, docs

16. DONE (commits 7f721eb + 16911c4; macOS + Linux + Windows). Loading extensions
    from a preset already worked via the generic "extensions" array; the gap was
    that `create-preset` could only capture --fdc/--sideways. It now recognises
    the same `--<name> [k=v:k=v]` flags as `start` (extension resolver +
    parse_extension_args) and emits them into the preset's `extensions` array,
    values typed per the manifest schema. Round-trip test in
    test_preset_subcommands (create-preset --rpc-serial -> PresetLoader). Doc:
    serial-acia.md preset section. (A Windows windows.h min/max macro collision in
    CliArgParsers.hpp, exposed by the test's new include chain, fixed by
    parenthesising numeric_limits.) Mutual exclusion: only one device owns the
    serial port; a preset + conflicting CLI serial device is rejected at attach.
17. DONE (mostly already accomplished by the 11c decomposition). There is no
    `--serial` flag and no bespoke serial parser any more: all three serial
    extensions (host-serial / rpc-serial / loopback-serial) are configured purely
    through the single generic `parse_extension_args`
    (`--<name> key=value:key=value...`), reading values via `config_value` only --
    no parsing code in the extension dirs, and no `ServerMain`<->`SerialService`
    duplication (the service is status-only). Cleanup done: removed the dead
    `Server::serial_service()` accessor (a `--serial`-wiring vestige; commit
    24f9d0d). RESOLVED later: the `split_colon_args` colon-in-value limitation is
    now fixed properly -- values containing `:` are wrapped in double quotes (the
    one rule; the partial `://` heuristic was dropped, with a helpful guidance
    error for the unquoted-URL blunder). ip232-serial gained a `url=` param on
    that basis, and rfc2217 will use the same `rfc2217://host:port` url form.
18. DONE (commit 24f9d0d): rewrote `docs/serial-acia.md` to the extension model
    (seam + SerialPort handle in core; the three extensions + their CLI syntaxes;
    /CTS flow control + configurable tx_buffer; status-only SerialService with
    WatchSerialStatus + rpc-serial's own RpcSerial service; Python/TS clients +
    generic macOS sidebar UI; current tests). Config is also self-documenting via
    `list-extensions` / `describe-extension <name>`.
    Remaining for Phase 5: step 16 (presets) + a preset round-trip test. Triad.

## Phase 6 -- Cassette seam (DEFERRED to an explicit later follow-up)

19. Cassette is out of scope for this effort (decided). It will be a separate,
    later piece of work: a `CassetteDevice` seam attaching via the ULA
    cassette-select path, gated by the cassette-presence axis, NOT routed through
    the byte-level `SerialPortDevice`. The only obligation on THIS effort is to
    keep the RS423 seam RS423-shaped so it does not foreclose that future seam
    (already covered in Phases 1-2); do not build cassette I/O now.

## Phase 7 -- CI integration and merge to master

20. Ensure the serial C++ tests run in CI on every platform (they build today;
    confirm they execute). Add the TS serial integration job alongside the
    existing AUN/Piconet ones. Decide on a Windows serial round-trip story
    (com0com on a self-hosted runner, a named-pipe endpoint, or documented
    manual Slioch validation).
21. Final full-suite run across the triad + all client suites.
22. Curate history as desired and merge `feature/serial` into `master`.
    DECIDED (Rob): NO squash; NO squash-merge; FAST-FORWARD merge (master is an
    ancestor of feature/serial, 60 commits ahead, so `git merge --ff-only` works).
    Rob does the merge + master push.

## Phase 7.5 -- host-serial typed gRPC API (ON feature/serial, BEFORE delivery)

DONE (commit 851f1ae; full parity: server + Python + TS + tests; green macOS +
Linux locally, Windows via CI). Scope (Rob): "full parity now" -- the deferred
piece is only the F2 client plugin restructure, not hand-written clients. Added
the HostSerial service (host_serial.proto + HostSerialService.hpp wired via
HostSerialExtension::grpc_services()): GetConfig + partial/diff SetConfig, driving
the same request_reopen as the GUI editor; runtime mode=pty rejected. Python
beebium.host_serial (bbc.host_serial) + TypeScript host_serial.ts (bbc.hostSerial).
Tests: C++ in test_host_serial_extension.cpp; Python + TS in the serial integration
suites (POSIX-gated for pty/device). The async reopen means SetConfig returns a
snapshot taken at call time -- callers re-read GetConfig to confirm post-apply.

SEQUENCING DECIDED (Rob): the host-serial typed gRPC API ships ON feature/serial,
before the master merge. The broader Python/TS client extension-API restructure
(F2 below) is deferred until AFTER delivery, with serial as its driving example.

The gap: host-serial's runtime config (mode / path / baud) is currently reachable
ONLY through the GUI ExtensionUi path (SubscribeView + Dispatch/EditorCommit), so
a scripting client must understand the declarative control-tree just to change a
baud rate. rpc-serial, by contrast, already has a clean typed service (RpcSerial).
This is the [[feedback_extension_multi_api]] principle (typed RPC for scripting +
Dispatch for GUIs, not redundant); host-serial should get a typed API too --
e.g. GetConfig -> {mode, path, baud} and SetConfig(partial, diff-applied: only
provided fields change), mirroring how the GUI EditorCommit ships just the edited
fields (TWO calls, not six getters/setters). It is an extension-provided service
(like RpcSerial), present only when --host-serial is loaded.

DECIDED (Rob): make this an **extension-SPECIFIC (bespoke) API, NOT a generic
schema-driven config RPC.** Rationale: a bespoke per-extension API better
expresses domain-specific needs, and some extensions could be quite complex -- a
flat generic config map would not do them justice. We already have ONE generic
(special-purpose) API for the *UI* extensibility axis (ExtensionUi); we do not
want to also funnel the *programmatic* axis through a generic shape. Revisit only
if more examples show the bespoke approach is causing real duplication pain.
(Assistant had recommended the generic route; Rob overruled -- bespoke it is.)

The host-serial config endpoint and the GUI ModalEditor must drive the SAME
underlying re-point path (HostSerialEndpoint::request_reopen), so a SetConfig and
a GUI edit are equivalent and each is reflected in the other's view.

## F2 -- Python (and later TypeScript) client extension-API design (AFTER delivery)

A dedicated design activity Rob wants, to make the CLIENT APIs as extensible as
the core. The core is open (new SerialPortDevice extensions need no core edits);
the CLI / ListExtensions / ExtensionUi are all generic over the manifest schema --
but the typed client APIs are hand-written per extension today.

Rob's direction (Python first -- he's more familiar, and willing to restructure
substantially; he has a lot of stevedore plugin/extension experience and thinks
that's the right model):
- A root `beebium` **namespace package**, functionally empty, registered on PyPI
  to claim the namespace.
- Probably an `emulator` package within it (e.g. `beebium.emulator`).
- A system of Python extensions -- `beebium.ext.host_serial`,
  `beebium.ext.acorn_scsi`, `beebium.ext.teletext_adapter`, ... -- each of which:
  - provides a Pythonic API for the corresponding extension, AND
  - crucially, can provide information to the core on HOW to launch beebium with
    that extension enabled and configured (the client extension owns both the
    "drive it" and the "spin it up with the right CLI" knowledge).
  Likely discovered/loaded via a stevedore-style entry-point plugin system.
- A TypeScript analogue is expected to be possible (entry-point / package-based
  plugin discovery), but the details are TBD -- Rob has less TS experience here.

This is deliberately ambitious. NOT pursued immediately; serial provides the
first concrete extensions to design against. F1 (bespoke typed server APIs) and
F2 (client plugin packaging) inform each other -- a `beebium.ext.host_serial`
Python package would wrap host-serial's bespoke typed config API from F1.

## Phase 8 -- Network-serial extensions (POST-MERGE; validates the seam)

After the core refactor is merged and working, investigate two network-backed
serial peers as further `SerialPortDevice` PeripheralExtensions. They are not
needed for the refactor; they are strong evidence that the open/extensible
seam was the right call (each is "just another extension attaching to the
serial port", alongside host-serial / rpc-serial / loopback-serial / a future
emulated FujiNet). Do NOT research now -- this is a tail-end task.

- IP232: a serial-over-TCP mode BeebEm supports (believed to map the emulated
  Beeb's serial port to a socket; confirm the exact framing/escaping).
- RFC 2217: a Telnet extension for remote control of serial ports over IP
  (the `rfc2217://<host>:<port>` URL form). Likely an IP<->serial bridge
  standard; pyserial implements a client.

Open questions for that investigation: are IP232 and RFC 2217 the same thing or
different; should Beebium offer one or both (e.g. an `ip232-serial` and/or
`rfc2217-serial` extension); and do they share enough with host-serial to reuse
the host-side primitives. Capturing here so it is not lost.

## Decisions (resolved with Rob)

- Pushing `feature/serial`: YES. Rob granted explicit, scoped permission to push
  and pull the `feature/serial` branch ONLY, between this machine and origin /
  Slioch. So Slioch follows the normal `git pull` workflow in
  `docs/local-windows-development.md`; no bundle needed.
- Phase 4: KEEP BOTH the typed `SerialService` RPCs (programmatic clients) and
  the `ExtensionUiService` path (GUIs). They serve different audiences.
- Phase 6: cassette is an EXPLICIT LATER FOLLOW-UP, not part of this effort.

## References

- `docs/discussion/serial-architecture-review.md` (the rationale).
- `docs/discussion/extension-ui-architecture.md`,
  `docs/discussion/serial-port-selector-control.md`.
- `docs/linux-container.md`, `docs/local-windows-development.md` (gitignored).
- `.github/workflows/ci.yml` (the platform/client matrix to extend).
- External-port precedents: `src/core/include/beebium/extension/UserPortDevice.hpp`,
  `OneMHzBusDevice.hpp`, `PeripheralExtension.hpp`; Acorn RTC + SCSI extensions.
- `docs/datasheets/MC6850_ACIA.pdf` -- the MC6850 ACIA datasheet; the
  authority for validating register semantics (control/status bits, TDRE/RDRF,
  /DCD, /CTS, IRQ behaviour) when reviewing and extending the ACIA tests.
