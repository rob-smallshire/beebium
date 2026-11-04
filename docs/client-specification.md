# Beebium Client Behavioural Specification

Status: specification. This document says what a Beebium client lets a user
accomplish, how each operation behaves, what states and transitions exist, and
which server services each capability rests on. It is written to be realised
independently on each platform: macOS (AppKit/SwiftUI/Metal, the first
implementation), Linux (Qt 6 / Vulkan, in progress), and any later client.

The macOS client is the first implementation from which this specification is
derived. It is not the canon. Where a behaviour is an Apple affordance rather
than something intrinsic to Beebium, this document says so, so that a Linux or
Windows client can behave like Beebium without looking like a Mac application.

Companion documents:

- `docs/discussion/raspberry-pi-linux-client-discussion.md` - the three
  contracts and the "specify behaviour, not appearance" principle this
  document implements.
- `docs/discussion/linux-client-architecture.md` - how a Qt 6 client would
  realise this specification. That note is not normative; this document is.
- The binding front-end rule documents, named per section:
  `lifecycle-management.md`, `frontend-modifier-keys.md`,
  `frontend-local-server-gating.md`, `frontend-sleep-wake.md`,
  `video-subsystem.md`, `sound-subsystem.md`, `machine-directory.md`,
  `preset-schema.md`, `sideways-slots.md`, `indicators.md`,
  `disc-subsystem.md`.


## How to read this

Each capability has its own section with the same shape:

- Behaviour: what the user can do and see.
- States and transitions: a compact table where a capability has real state.
- Rests on: the server proto file and the RPCs the capability uses. RPCs are
  named as `file.proto Service.Method`. Where a capability uses the core
  executable's run-to-completion CLI rather than gRPC, the subcommand is named.
- Conformance: every behaviour is tagged one of three ways.
  - MANDATORY: any conformant client must do this. It is part of the wire
    contract or of the observable product behaviour that makes a client "a
    Beebium client".
  - LATITUDE: the behaviour is required, but the widget, dialog, menu
    placement, keyboard shortcut or exact timing is the platform's choice.
  - MACOS-ONLY: an artifact of AppKit, SwiftUI, Metal or this client's
    packaging. Named so a later client knows not to copy it, only the behaviour
    behind it.
- Profile: which conformance profile the capability belongs to (see section 2).
- Scenarios: observable scenarios a conformance suite can check, stated as
  input then expected observable result, independent of the UI toolkit.

Three kinds of consistency run through the whole document and are worth holding
in mind:

- Semantic consistency: the same operation means the same thing on every
  client. This is MANDATORY throughout.
- Interaction consistency: workflows are recognisably similar, but shortcuts,
  dialogs and menu placement follow platform conventions. This is LATITUDE.
- Visual consistency: the emulator display and specialist visualisations are
  shared; ordinary controls are native to the platform.

This document is long by design. The capability index below is the map.


## 1. Capability index

Section 3 states cross-cutting foundations that every later section assumes.

| Section | Capability | Profile |
|---------|-----------|---------|
| 3.1 | Server as single source of truth | Core |
| 3.2 | Local-server gating (path exchange) | Core |
| 3.3 | Event-driven state, not polling | Core |
| 3.4 | Error presentation registers | Core |
| 3.5 | Client-only settings persist across sessions | Core |
| 4.1 | Connecting and discovery | Core |
| 4.2 | Protocol compatibility handshake | Core |
| 4.3 | Connection liveness and the disconnect overlay | Core |
| 4.4 | Sleep and wake | Core |
| 4.5 | Window and server shutdown | Core |
| 5.1 | New machine from presets | Core |
| 5.2 | The machine draft and the configurator | Configuration editing |
| 5.3 | The machine directory as a document | Core |
| 5.4 | Save and Move | Core |
| 5.5 | Machine control: run, Break, reset, battery, shutdown | Core |
| 5.6 | Emulation speed | Core |
| 5.7 | Names, titles and the description subtitle | Core |
| 6.1 | Frame presentation | Core |
| 6.2 | Display modes and aspect ratio | Core |
| 6.3 | Scaling, pixel shape, display styles, background | Advanced display |
| 6.4 | Immersive mode | Core |
| 6.5 | Screen text selection and Copy As | Advanced display |
| 6.6 | Video settings and persistence | Advanced display |
| 7.1 | Hardware-path typing and type-ahead | Core |
| 7.2 | Modifier key rules and stuck-modifier safety | Core |
| 7.3 | Caps Lock versus Shift Lock | Core |
| 7.4 | Keyboard mapping files | Core |
| 7.5 | Paste as typing | Core |
| 7.6 | On-screen and auxiliary key input | Core |
| 8.1 | Format-driven audio fields | Core |
| 8.2 | The group mixer: master, sub-masters, channels | Core |
| 8.3 | Buffering, latency and buffer health | Core |
| 9.1 | Mounting disc images | Core |
| 9.2 | Recent discs | Core |
| 9.3 | Creating blank discs | Core |
| 9.4 | Drag and drop of disc images | Core |
| 9.5 | Write-back and eject | Core |
| 9.6 | Disc error reporting | Core |
| 9.7 | Drive status indication | Core |
| 10 | Indicators (real LEDs only) | Core |
| 11 | Sideways ROM/RAM slots | Peripheral management |
| 12.1 | Extension-supplied sidebar views | Peripheral management |
| 12.2 | The configurator and parameter forms | Configuration editing |
| 12.3 | Peripheral discovery tree | Peripheral management |
| 12.4 | Printer, serial, Econet and AUN surfaces | Peripheral management |
| 12.5 | Debugger surface | Debugging |
| 13.1 | Sidebar modes | Core |
| 13.2 | Application menus and commands | Core |
| 13.3 | Settings and preferences | Core |
| 13.4 | The entry point and the home window | Core |


## 2. The contract model and conformance profiles

Beebium separates three contracts:

1. The server capability protocol: the declarative descriptions, state,
   operations and notifications the server exposes over gRPC. Already
   implemented; this document only names the parts a client uses.
2. The client behavioural specification: this document.
3. Platform-specific presentation: how each client realises the behaviour.

Because the server describes what exists (machines, peripherals, audio fields,
sideways topology, extension views) and the client decides how to present it,
clients with very different interaction models can share the same contract. A
client therefore need not implement every capability. The capabilities group
into profiles:

- Core. The profile every client implements: connect and discover, machine
  lifecycle, display, keyboard, audio, discs, indicators, the error registers,
  the window/connection lifecycle. A client that implements only Core is a
  usable Beebium client.
- Configuration editing. Building and editing machines before launch: presets,
  the machine draft, the configurator and parameter forms. A client may launch
  only existing presets and omit this.
- Peripheral management. The live peripheral tree, extension-supplied sidebar
  views, sideways slot management, networking surfaces. A read-only or headless
  client may omit this.
- Advanced display. Display styles, pixel aspect choice, on-screen text
  selection and Copy As. A client may present a single fixed display style and
  omit this.
- Debugging. Breakpoints, memory inspection, disassembly. The macOS reference
  client does not implement this profile beyond the launch-unblock call; a
  conformant client may omit it entirely.

A capability's section names its profile. Within a profile, MANDATORY items are
required; LATITUDE and MACOS-ONLY items are not.


## 3. Cross-cutting foundations

### 3.1 Server as single source of truth

The server owns all machine knowledge. The client holds no duplicate model of
what a machine is, what peripherals exist, what a Music 5000 is, how many
drives there are, or what a screen mode means. The client learns everything it
shows from the server, by RPC, and renders it generically.

Conformance:

- MANDATORY: the client never hard-codes machine-specific facts that the server
  reports. Drive count and numbering, audio fields and groups, sideways
  topology, peripheral identities, display geometry, indicator identities,
  preset catalogues and extension parameter manifests all come from the server
  at runtime. A new extension, a new machine variant or a new audio source must
  render with zero client changes.
- LATITUDE: caching a fixed-for-the-session answer (the audio format, the
  extension list, the sideways topology) after one fetch.

### 3.2 Local-server gating (path exchange)

A client and server may run on the same host or on different hosts. Some
operations exchange filesystem paths: the client asks the server to open a
path, or offers to reveal in a file manager a path the server reported. Those
operations are meaningful only when the client can exchange paths the server
can open - that is, when both see the same filesystem.

State this capability as a single boolean the rest of the client consults: call
it "the server shares our filesystem" (the macOS client names it
`isServerLocal`). The default evidence for it is the host fingerprint:
`system.proto SystemInfo.host_fingerprint`, compared for equality with the
client's own fingerprint, computed by the shared recipe in `HostFingerprint`
(platform host identifier, domain-prefixed, SHA-256, lowercase hex; macOS uses
`gethostuuid()`, Linux uses `/etc/machine-id`). The comparison fails closed: an
unknown or empty fingerprint on either side is not a match. It is re-evaluated
on every connection and reset to false on disconnect.

The fingerprint is the default evidence, not the definition. The definition is
"can the client exchange paths the server can open". A per-platform override is
allowed where the fingerprint is not sufficient evidence:

- A sandboxed client (Flatpak, Snap, or a future macOS App Sandbox build)
  shares the host identity with a local server, so the fingerprint matches, but
  does not share the filesystem view. Such a client MUST fail closed: treat the
  server as not sharing the filesystem regardless of the fingerprint, unless it
  has positive evidence (for example a portal-granted shared directory) that a
  given path is mutually openable.

What gates and what does not (per `frontend-local-server-gating.md`):

- Gated (offered only when the server shares the filesystem): browse-for and
  insert a disc image by picked path; drag-and-drop a disc image; create a
  blank disc (it picks a host path for the server to write); reveal any
  server-reported path in a file manager; reveal a FileReference's file.
- Not gated: a FileReference's server-side actions (the server opens its own
  path); editing values such as AUN peers or subnets; "Copy Path" (it hands the
  user a string, honest on any host); pre-launch configuration paths that become
  the local server process's argv; the client's own files; read-only displayed
  paths.

Conformance:

- MANDATORY: compute and compare the fingerprint by the shared recipe, fail
  closed, re-evaluate per connection, reset on disconnect. Gate exactly the
  path-opening features above and never gate value-carrying or Copy-Path
  actions. A sandboxed client fails closed.
- MANDATORY: present gating as disabling with an explanation (or refusing a
  drag in flight), not as offering the action and then erroring.
- LATITUDE: how disabling is surfaced (tooltip wording, when a drop refusal
  becomes visible), the per-platform positive-evidence mechanism for a sandbox.
- MACOS-ONLY: `gethostuuid()` plus CryptoKit derivation; the specific gated
  views.

Scenario: connect to a server at a loopback address, a LAN address and a
`.local` route that all resolve to the same machine; all three must read as
"shares our filesystem" (the fingerprint decides, not the address). A
container- or sandbox-hosted server reads as not sharing the filesystem.

### 3.3 Event-driven state, not polling

Where the server offers a status stream, the client takes one initial snapshot
and then subscribes, mirroring the stream. It does not sample state on a timer.

Conformance:

- MANDATORY: disc drive state, sideways slot state, indicators, server
  liveness, Econet status and extension views are driven from their server
  streams. Sampling cannot represent an eject-then-insert excursion or
  distinguish a forced from a graceful eject, so it is wrong, not merely
  inefficient (`disc-subsystem.md`).
- LATITUDE: a stream's minimum publish interval (the macOS client uses 50 ms
  for indicators, 100 ms for sideways events).

### 3.4 Error presentation registers

Two registers, kept distinct:

- Server messages are lower-case fragments. They are shown verbatim and in
  full, never rewritten and never truncated. A long server diagnostic (a bad
  ROM, a port in use, a protocol mismatch) is shown complete, wraps rather than
  clips, is selectable, and is copyable so the user never retypes it into a bug
  report.
- Client-authored alerts are full sentences, with a capital and a full stop.
  Where a sentence must incorporate a server fragment, the client supplies the
  sentence frame and shows the fragment within it (a classification enum chooses
  the frame; the verbatim text still appears).

A further distinction the client must preserve: a self-explanatory precondition
("eject disc first") may be a transient, self-clearing notice; a server failure
message must be persistent, complete, and shown somewhere it cannot truncate
(the macOS client uses a short inline marker plus a popover holding the full
text). Every refused request must be made visible: a turned-down request
otherwise looks like nothing happened.

Conformance:

- MANDATORY: server error text verbatim and in full, with a copy affordance for
  long diagnostics; client alerts as full sentences; server failures persistent
  and untruncated; precondition notices may auto-dismiss; every refusal
  visible.
- LATITUDE: alert versus banner versus popover; the notice lifetime; marker
  wording.
- MACOS-ONLY: NSAlert, NSPasteboard, the specific transient-message model.

### 3.5 Client-only settings persist across sessions

Some settings belong to the client, not to the machine or the server: the
display style and pixel shape, the window background, the active keyboard
mapping and its per-key disables, the selected sidebar mode, the Mode 7 copy-as
preference, and the like. The user should not have to redo such a choice after
quitting and relaunching. A client remembers its client-only settings across
sessions.

Conformance:

- MANDATORY: client-only settings that a user sets persist across runs of the
  client, in the platform's preferences store. A user who sets a display style,
  a pixel shape, a keyboard mapping or a background does not find it reset on the
  next launch.
- LATITUDE: the storage mechanism; the scope at which a setting persists (global
  default versus per-machine); any shorter-lived in-session cache layered on top
  of the persisted value (for example the macOS client's per-machine,
  process-lifetime video-settings cache, which is a convenience within this
  rule, not a substitute for it); which settings are deliberately session-only
  (immersive mode is not persisted, by section 6.4).

This rule governs the persistence requirements referenced from sections 6.6
(video settings) and 13.3 (settings and preferences).


## 4. Connection and server lifecycle

### 4.1 Connecting and discovery

Profile: Core.

Behaviour. A user connects a window to a server in three ways: pick a
discovered machine from a live list; type an address (host and port); or open a
`beebium://connect` deep link. Discovery is mDNS (Bonjour on macOS, Avahi on
Linux) browsing the service type `_beebium._tcp` in domain `local.`. Discovered
machines are presented with their instance name and model; machines on this
host are distinguished from machines on the network. A machine already
connected to an open window is shown as such, and connecting to it again just
brings that window forward rather than opening a duplicate. The address entry
defaults to `localhost:48875` and offers recent successful connections.
Successful connections are remembered across launches.

The deep link `beebium://connect?host=H&port=P` (with optional `run`,
`provenance` and `sidebar` parameters) attaches a window to a server without
discovery or the connect dialog, including a server on an ephemeral,
un-advertised port. It is idempotent: a second identical link activates the
first window.

States and transitions:

| Area | States and transitions |
|------|------------------------|
| Discovery browser | available on ready; unavailable on failure or cancel |
| Machine list | service appears -> resolve -> add and sort (local first, then by name); service changes -> re-resolve; service removed -> drop |
| Address parse | `host:port` (port 1..65535) or bare host with default port 48875; else the connect action is disabled |
| Connect | already connected -> activate existing window; else record recent, set pending target, open a window on it |

Rests on: discovery is mDNS, no RPC; the TXT record carries `model`,
`model_name`, `uuid`, `role` and Econet keys. After the channel opens, the
handshake uses `system.proto SystemService.GetSystemInfo` (section 4.2) and
`system.proto SystemService.WatchServerStatus` (section 4.3).

Conformance:

- MANDATORY: discovery over `_beebium._tcp` mDNS with those TXT keys; connect by
  host and port with default port 48875; the `beebium://connect` deep-link
  contract; not opening a duplicate window for an already-connected target;
  remembering successful connections.
- LATITUDE: dialog layout, the two-section (this-host/network) split, the
  connected indicator, a "copy address" affordance, the recents cap and its
  storage.
- MACOS-ONLY: the NWBrowser/NetService resolution path, opening a window scene.

Scenarios: a server started with the advertise flag appears in the list, under
this-host if local else network. `beebium://connect?host=localhost&port=48875`
opens a connected window and leaves no empty welcome window behind; issued
twice it activates the first window. A server on an un-advertised port is
reachable by deep link but not shown in the list.

### 4.2 Protocol compatibility handshake

Profile: Core.

Behaviour. Each client compiles in a protocol fingerprint: a SHA-256 over the
canonical descriptor set of the service protos (plus the extension-ui proto),
generated and committed, identical across all clients and the server. On
connect, the client fetches the server's fingerprint and compares. A mismatch
is surfaced; an empty server fingerprint (a server too old to report one) counts
as a mismatch.

Whether a mismatch blocks the connection is platform latitude: the Python and
TypeScript clients refuse; the macOS client warns with a loud, actionable alert
that names the server binary path and both fingerprints and continues. A client
chooses, but it must not silently ignore a mismatch.

Rests on: `system.proto SystemService.GetSystemInfo`, fields
`protocol_fingerprint` and `executable_path`.

Conformance:

- MANDATORY: carry a compiled-in fingerprint derived by the shared recipe (not
  recomputed per language); fetch and compare the server's at connect; surface a
  mismatch; treat empty as mismatch.
- LATITUDE: refuse versus warn-and-continue; alert wording.
- MACOS-ONLY: the fault log and the SwiftUI alert.

Scenario: connect to a server built from a different proto revision; the client
reports the mismatch quoting both hashes and the server path. A logic-only
server change leaves the fingerprint equal and connects clean.

### 4.3 Connection liveness and the disconnect overlay

Profile: Core.

Behaviour. While the connection is healthy, nothing is shown. The sole
authority for liveness is the server status stream and its heartbeats, never the
frame rate. The client distinguishes four states and shows a distinct treatment
for each loss, over the frozen last frame:

- active: healthy; no overlay.
- unreachable: heartbeats stopped but the stream is still open; a spinner and
  "trying to reconnect" that clears itself if heartbeats resume.
- stopped: the stream completed after the server announced it was shutting down;
  "emulator stopped", terminal, with a way to close.
- died: the stream completed with no shutdown announcement; "emulator stopped
  unexpectedly", terminal, with a way to close.

A stream that completes because the client cancelled it (close or reconnect) is
intentional and leaves liveness untouched; loss is detected by the stream's
completion status, not by a thrown error.

States and transitions:

| State | Trigger | Terminal |
|-------|---------|----------|
| active | any status event; heartbeat re-arms the watchdog | no |
| unreachable | heartbeat watchdog fires while stream still open (from active only) | no; recovers to active on next event |
| stopped | stream completes (not client-cancelled) after a prior shutting-down event | yes |
| died | stream completes (not client-cancelled) with no shutdown announcement | yes |

Rests on: `system.proto SystemService.WatchServerStatus`, a stream of
`ServerStatusEvent` whose `status` enum includes ready, shutting-down,
identity-changed, heartbeat, shutdown-progress, machine-reset and guest-ready;
the server emits a heartbeat about every 500 ms.

Conformance:

- MANDATORY: liveness derived solely from the status stream; the four-way
  distinction; loss detected by completion status not by an exception;
  client-cancellation treated as intentional; stopped and died terminal,
  unreachable recoverable; an application-level heartbeat with a watchdog
  (transport keepalive is not a substitute). Do not collapse the three loss
  states into one.
- LATITUDE: the watchdog timeout (macOS uses 2 s over the 0.5 s heartbeat); the
  overlay look; the label (prefer the server's machine name, fall back to the
  address).
- MACOS-ONLY: the specific overlay views and the main-runloop timer.

Scenarios (sending signals to a local server): graceful terminate -> "emulator
stopped"; kill -> "emulator stopped unexpectedly"; suspend the process -> the
reconnecting spinner after about 2 s; resume it -> the spinner clears back to
live.

### 4.4 Sleep and wake

Profile: Core. Binding document: `frontend-sleep-wake.md`.

Behaviour. Display sleep must not disturb the connection or the picture: the
client presents on frame arrival, never from a display-linked callback that a
sleeping display halts, so rendering simply resumes when frames resume. Full
system sleep suspends the process and drops its connections; on wake the client
rebuilds them automatically. During recovery the user sees the same
"reconnecting" treatment as any unexpected drop, counting attempts; on success
it clears; if automatic attempts are exhausted, a terminal "could not reconnect"
state offers Retry and Close.

Recovery is active, not passive. One reconnect rebuilds the whole connection
(the channel owner reconnects, the other streams re-establish off its success).
The policy is a pure, testable unit: a backoff schedule (macOS uses 0.5 s
doubling to an 8 s cap, up to 8 attempts), wake as an accelerant that forces an
immediate attempt and resets the backoff, will-sleep cancelling a pending
attempt. It must never fight an intentional stop: if a graceful shutdown arrives
(even late, during a backoff), recovery stops rather than re-dialing.

The wake signal is a platform-abstracted monitor that fires for full system
sleep only, never display sleep. macOS uses the workspace sleep/wake
notifications; Linux uses logind PrepareForSleep on the system bus. The window
combines liveness and reconnect phase into one verdict: an active reconnect
outranks the raw liveness, so a dropped stream that reads as died is shown as
reconnecting while recovery is trying.

Rests on: no dedicated RPC; recovery re-establishes the same streams, including
`SystemService.WatchServerStatus`.

Conformance:

- MANDATORY: present on frame arrival so display sleep is a non-event; active
  recovery on a truly dead stream; one channel-owner reconnect with the rest
  cascading; never fight a graceful shutdown; a window close cancels recovery
  first; wake forces an immediate attempt; the wake signal is full-system-sleep
  only.
- LATITUDE: the backoff constants and attempt count; the overlay text.
- MACOS-ONLY: the workspace notifications and their bridge.

Scenarios: close the lid and reopen -> the window recovers to live with no user
action. Crash the server mid-session -> reconnecting attempts; if it never
returns, the terminal could-not-reconnect state with Retry and Close. A graceful
stop during a backoff -> recovery stops and the window shows stopped.

### 4.5 Window and server shutdown

Profile: Core. Binding document: `lifecycle-management.md`.

Behaviour. Closing a machine window is lifecycle-aware, not a raw close. If an
unsaved (transient) machine has written to its copied media, the user is first
asked whether to save. If other clients are connected to a server whose lifetime
this window owns, the user is offered: shut the server down, leave it running
(detach this window only), or cancel.

The decision of what a window close does is a pure function of whether this
client owns the server's lifetime and how many clients are connected:

| Situation | Action |
|-----------|--------|
| server not owned by this client, or window unlinked | disconnect this window only |
| this client owns the server and is its sole client | shut the server down |
| this client owns the server and other clients remain | ask the user |

The teardown order is load-bearing: the client disconnects all its gRPC streams
before asking the server to shut down, because the server's graceful shutdown
waits for client streams to close. The channel-owning stream disconnects last.
All disconnects are idempotent. The shared transport thread pool is never torn
down per connection (on macOS this is a SwiftNIO workaround; a Qt client's
equivalent is simply to destroy channels and join reader threads off the GUI
thread after cancelling).

On application quit, the client shuts down only those servers it both launched
and is the sole client of; multi-client or externally-started servers are left
running.

Rests on: `system.proto SystemService.RequestShutdown` (graceful or immediate,
with a grace period), authorised by the provenance instance UUID carried in
request metadata; client count from `SystemService.GetSystemInfo` and the status
stream. A client that launched the server may also stop it by signalling the
process it owns.

Conformance:

- MANDATORY: disconnect all streams before requesting shutdown; disconnect the
  channel owner last; idempotent disconnects; never tear down the shared
  transport pool per connection; route every "close" affordance (including
  overlay Close buttons) through the same lifecycle teardown; the provenance-
  authorised shutdown request; the sole-client-and-launched rule on quit.
- LATITUDE: the save-on-close and multi-client prompt wording and buttons; the
  signal-versus-RPC choice for a server the client launched; the fallback
  timing.
- MACOS-ONLY: intercepting the window close button (a SwiftUI workaround; a Qt
  client uses the real close event), the alert sheets, the window-keyed
  connection registry.

Scenarios: close the sole window of a client-launched server -> the server shuts
down and the window closes with no prompt. With a second client attached, close
one window -> the multi-client prompt; "leave running" detaches and closes while
the server runs on for the other client; "shut down" sends the shutdown request
and the other client sees stopped. A transient machine with disc writes ->
closing prompts to save; cancelling (or cancelling the save panel) keeps the
window open.


## 5. Machines

A structural fact shapes this whole chapter: the client never writes machine or
preset bytes itself. Presets, machine directories, battery state and thumbnails
are owned by the server. The client reaches the server two ways: run-to-
completion CLI subcommands against a `beebium-<model>` executable (for work that
has no running machine: listing, schema, resolve, describe, thumbnails), and
gRPC against a running server (for live machine control). This split is itself
MANDATORY.

### 5.1 New machine from presets

Profile: Core (launching presets). Binding document: `preset-schema.md`.

Behaviour. The user picks a preset from a catalogue grouped into built-in and
user presets, each with a one-line description and, where available, a thumbnail
of its last screen. Choosing a preset launches a machine from it. The catalogue,
the model display names and the thumbnails all come from the server; the client
invents none of them. A preset whose executable is absent from this build is
omitted. A preset marked as having no thumbnail is shown with a placeholder and
never triggers a thumbnail fetch.

Rests on (CLI): `list-presets --all-models` (one listing executable returns
every model's presets as JSON lines), `describe-preset-schema` (model display
name and the editor schema), `show-preset --thumbnail` (a preset's screen PNG).
Management uses `create-preset`, `delete-preset`, `export-preset`,
`import-preset`. No gRPC.

States and transitions:

| State | Trigger | Next |
|-------|---------|------|
| discovering | list on launch | listed / error |
| listed | select a preset | draft and schema load (5.2) |
| editing | Create | launching (5.5) |

Conformance:

- MANDATORY: the catalogue comes from `list-presets`, thumbnails from
  `show-preset`, model names from `describe-preset-schema`; a preset whose
  executable is absent is omitted; a no-thumbnail preset is never fetched.
- LATITUDE: the picker or card presentation, grouping, thumbnail caching.
- MACOS-ONLY: the welcome window and its presets page (the entry-point rule
  behind it is in section 13.4).

Scenarios: a preset directory with a built-in and a user preset -> two groups. A
preset whose executable is removed -> absent from the picker. A no-thumbnail
preset -> placeholder, no thumbnail subprocess ever spawned.

### 5.2 The machine draft and the configurator

Profile: Configuration editing. Binding document: `preset-schema.md`.

Behaviour. Before launch, the configuration is held as a machine draft: the
preset JSON as an editable document, its keys matching the server schema
exactly. Every configuration section is an editor over part of this one
document. Two disciplines are intrinsic:

- Lossless pass-through. The draft keeps unmodelled keys and re-emits them, so a
  decode-encode-decode round trip is identity. The only normalisation is that an
  explicit null and an absent key both mean "unset".
- resolve-preset as the oracle. Loading a preset for editing runs
  `resolve-preset --json`: defaults filled, paths made absolute, default ROMs
  expanded into explicit slots. That resolved document is the editing source. As
  the draft changes, the client re-resolves it (debounced) to get diagnostics
  (severity, a JSON-pointer path, a message, a human location); error
  diagnostics block Create. Launch serialises the draft to a temporary preset
  and starts the server from it, with no per-setting command-line overrides, so
  one representation carries the whole machine.

An "exact record" rule governs editing: a field writes to the draft only when it
differs from the default, so an untouched preset stays byte-exact.

Rests on (CLI): `resolve-preset --json` (oracle and validation),
`import-preset` (save as a user preset), `start --preset <temp>` (launch).
`describe-preset-schema` drives the editor's capabilities.

Conformance:

- MANDATORY: edit a resolved document; resolve-preset as validation and
  normalisation oracle; launch the edited document as a preset; lossless
  unknown-key preservation; exact-record editing (an untouched preset stays
  byte-exact).
- LATITUDE: the unknown-key technique and the debounce interval.
- MACOS-ONLY: the new-machine dialog and welcome window.

Scenarios: load a preset with an unknown top-level key -> the key survives into
the launched machine. Introduce an out-of-range value -> an error diagnostic at
its path, Create disabled. Create a preset unedited -> the launched machine
equals the preset resolved. Set a field back to its default -> the key is
cleared from the draft.

### 5.3 The machine directory as a document

Profile: Core. Binding document: `machine-directory.md`.

Behaviour. A machine lives in a directory the server owns. It holds `machine.json`
(identity: UUID, description template, model, timestamps, source preset - the
name is not stored here), `preset.json` (configuration), `thumbnail.png`,
battery-backed state under `nvram/` and `extensions/<id>/nvram/`, an exclusive
lock held by the running server, and a document file inside. A directory is a
machine because it holds `machine.json`; the suffix and the inner document file
are convention.

A machine is either transient or saved, and the server tells the client which
(`machine_saved`, `machine_dirpath`). A transient machine's directory is
disposable and deleted on shutdown; the client must not present it as a document
on disk. A saved machine is a document: the client may attach whatever its
platform offers for rename, move, reveal and "locked" affordances, but the
client reads and writes no bytes in the directory - the server does.

The lock is held exclusively by the running server and released if it dies. The
client reads it only indirectly (through `describe-machine`) to show a "running"
badge and to decide open behaviour. A running machine's directory cannot be made
immutable (it would break atomic writes), so a "lock this document" affordance
is declined with a sentence.

An unsaved-changes signal is `template_media_written` on every status event:
true only while a transient machine has written to copies of its preset's media.
It drives the unsaved-changes indicator and the save-on-close prompt. It is
always false for a saved machine and is cleared by a save.

States and transitions:

| State | machine_saved | Document | Title source |
|-------|---------------|----------|--------------|
| transient | false | none; not shown as a disk document | server name |
| saved | true | attached | server name |
| transient -> saved | first Save or Save As | attach | unchanged name |

Rests on: `system.proto SystemService.GetSystemInfo` (`machine_saved`,
`machine_dirpath`, identity); CLI `describe-machine` for the saved-machines list
and for Open.

Conformance:

- MANDATORY: the directory format and its ownership by the server; the
  transient-versus-saved distinction from the server; not presenting a transient
  machine as a disk document; the unsaved-changes signal; declining to lock a
  running machine's directory.
- LATITUDE: whether the unsaved indicator lives on a window or a document
  object.
- MACOS-ONLY: NSDocument, the title-bar document popover, the proxy icon,
  autosave-as-no-op, the welcome window.

Scenarios: launch from a preset (transient) -> no disk-document affordances;
Save As disabled, Rename offered as a lightweight sheet. Save it -> it becomes a
document. Try to lock a running saved machine -> "a machine cannot be locked
while it is running." Open a directory whose `machine.json` is gone -> a
"could not read a saved machine" alert.

### 5.4 Save and Move

Profile: Core. Binding document: `machine-directory.md`.

Behaviour. The machine never stops to be saved or moved. The server pauses
emulation internally, flushes drives, writes to a temporary sibling, renames
into place, re-points open media and moves the lock.

Save has three forms:

| Machine | Destination | Result |
|---------|-------------|--------|
| not yet saved | new directory | saved there, same UUID |
| saved | none | state, screen and name written in place |
| saved | new directory | Save As: a copy with a new UUID; the machine runs on from the copy |

An existing destination is only replaced when the user's own file chooser
confirmed a replace; the client deletes nothing itself and passes a
"replace existing" flag. Move (and a saved rename, which is a move to a sibling
name) keeps the UUID, lock, document file and folder metadata; a same-volume move
is a rename that keeps open files open, a cross-volume move is a copy-then-
remove. Moving a transient machine is declined, not silently turned into a save.

Errors map to sentences by classification: a taken name, a missing parent, an
invalid destination, a not-replaceable precondition. On success the client
updates its record of the directory and the server pushes the new identity.

Rests on: `system.proto SystemService.SaveMachine`,
`system.proto SystemService.MoveMachine`,
`system.proto SystemService.SetMachineName` (a saved rename is a move to a
sibling). After a rename the client re-fetches `GetSystemInfo` because the new
directory is not in the rename response. Identity changes arrive as the
identity-changed status event.

Conformance:

- MANDATORY: the Save and Move semantics and the three Save forms; replace only
  on explicit confirmation; save and move without stopping; the identity-changed
  event; moving a transient machine is declined.
- LATITUDE: the save panel flow; where rename, move and duplicate live; error-
  sentence composition.
- MACOS-ONLY: NSSavePanel, the title-bar popover rename/move/duplicate.

Scenarios: save a running transient machine and reopen it -> same UUID, media
and battery as at the save. Save As a saved machine -> a new UUID, the original
left intact and unlocked. Rename a running saved machine to a taken sibling name
-> refused with a sentence, the machine runs on where it was. Move across
volumes -> new directory, UUID and created-date preserved, old directory gone.

### 5.5 Machine control: run, Break, reset, battery, shutdown

Profile: Core.

Behaviour. Servers are launched blocked until the client issues a run, so a
client that launches or deep-link-opens a machine with the run flag issues one
run call on connect. There need be no user-facing pause/resume or hard-reset
command (the macOS client ships none); pausing is an internal step of save and
move.

Break is the BBC Break key: not a matrix key but a reset line. Holding it halts
the CPU; releasing it begins a soft reset. The client sends a Break-down then a
Break-up. A hard reset (full re-initialisation) exists server-side (power-on,
Ctrl-Break, or a debugger reset); the client observes any reset through the
machine-reset status event, which carries soft or hard, and resynchronises its
lock mirror. "Forget battery-backed state" is a distinct call that takes effect
at the next start and lists the units it will forget.

Shutdown is covered in section 4.5.

Rests on: `debugger.proto DebuggerControl.Run` (start and resume);
`keyboard.proto KeyboardService.BreakDown` and `KeyboardService.BreakUp` (soft
reset); `debugger.proto DebuggerControl.Reset` (hard reset, if a client offers
one); `system.proto SystemService.ResetBattery`; the machine-reset and
guest-ready status events.

Conformance:

- MANDATORY: issue the run call for a machine launched blocked; Break as
  down-then-up on the Break RPCs (never a matrix key up); observe resets through
  the status event and resync; the battery-reset semantics (effective next
  start).
- LATITUDE: whether to expose an explicit pause or hard-reset command; how Break
  and battery-reset are presented. No client is expected to expose Pause or a
  hard Reset at present; the reference client exposes neither, and a client may
  omit both and still be conformant. The reference client has no on-screen
  Break control and no battery-reset command: Break is the mapped key, held,
  and `ResetBattery` has no caller in either client.

Scenarios: press Break -> a soft machine-reset event, RAM preserved. Forget
battery on a running machine -> the forgotten units are returned, the machine
runs on, the next start begins from seed.

### 5.6 Emulation speed

Profile: Core.

Behaviour. The user sets an emulation speed multiplier: real-time (1x), a faster
or slower multiple, or unlimited (as fast as the host runs). The client shows the
speed actually achieved alongside the requested speed, so a host that cannot keep
up is visible. Pre-launch speed is a preset key.

Rests on: `system.proto SystemService.SetSpeedMultiplier` (0.0 is unlimited, 1.0
is real-time); `system.proto SystemService.GetPacingStats` and
`WatchPacingStats` for the achieved speed.

Conformance:

- MANDATORY: set the multiplier (with 0.0 meaning unlimited); show the achieved
  speed from the pacing stats; honour the real-time-required constraint some
  transports impose (section 12.4).
- LATITUDE: the control (the macOS client uses a log2 slider with an achieved
  caret and an unlimited toggle); the pacing poll interval.

Scenario: set unlimited then back -> the wire carries 0.0 then the multiplier;
the achieved-speed readout tracks the pacing stream.

### 5.7 Names, titles and the description subtitle

Profile: Core.

Behaviour. A machine's name is plain text, never a template. The window title is
exactly the name ("Beebium" before the server answers). A saved machine's name
is its document name; a transient machine's default is a supplied name, else the
preset name plus a per-session ordinal ("Model B (Disc) #2") until renamed. The
ordinal is counted per preset per run and never reused within a run.

Below the name, the window shows a description: a template rendered by the server
against named placeholder values, re-rendered as those values change and pushed
as identity changes. The template is set at new-machine time as a preset key.
Placeholders are a server-side registry: the client carries no list of keys, it
asks the server, and each placeholder carries its key, label, description, group,
the exact string a picker inserts, its current value, and whether it applies.
Keys are owned by core server providers and are stable once shipped. Template
syntax (brace-delimited keys, doubled braces for literal braces, unknown key
verbatim, inapplicable key empty) is rendered by the server, not the client.

The macOS client sets the name through a rename sheet (transient) or the
document popover (saved) and sets the description only pre-launch as plain text
(there is no running machine to render against yet); it does not ship a live
description editor, though the server offers one.

States and transitions:

| Field | Source | Change path |
|-------|--------|-------------|
| name | identity name | set-name or move or directory rename -> identity-changed |
| title | the name | follows the name |
| subtitle | identity description | template re-rendered about 1 Hz -> identity-changed |
| template | preset key at launch | set-description (server offers it; macOS does not use it) |

Rests on: `system.proto SystemService.SetMachineName`; for a description editor
`SystemService.SetMachineDescription`, `ListDescriptionPlaceholders`,
`PreviewMachineDescription` (offered by the server; unused by the macOS client);
the identity-changed status event carries the new identity.

Conformance:

- MANDATORY: the name is plain text and is the title; the name is not stored in
  the preset (a per-session ordinal composes the default); the description is a
  server-rendered subtitle; placeholders are discovered by query, not hard-coded;
  the template syntax is the server's; identity changes propagate through the
  status event.
- LATITUDE: the rename affordance; a pre-launch plain-text description field;
  whether to ship a live description editor; the ordinal-per-preset numbering.
- MACOS-ONLY: the rename sheet and the document popover.

Scenarios: two windows from one preset -> titles end "#1" and "#2"; close #1 and
launch again -> "#3" (no reuse). A preset whose description template references a
placeholder -> the subtitle shows the resolved value and re-renders when it
changes. Rename a saved machine to a free name -> title and directory change,
same UUID; to a taken name -> refused, title unchanged.


## 6. Display

The division that runs through this chapter: the server dictates screen mode,
logical pixel data (BGRA32 at logical resolution), per-frame dimensions, borders,
display width and height, interlace field order, and split-screen regions, and
all recognised screen text. The client never learns "the mode"; it receives
geometry and pixels. The client chooses how to scale and aspect-fit, the pixel
shape, the display style, the background fill, immersive presentation, and the
selection and copy gestures.

### 6.1 Frame presentation

Profile: Core. Binding document: `video-subsystem.md`.

Behaviour. The picture updates continuously (about 50 Hz). On connect the client
fetches the frame configuration once, then opens the frame stream; each frame is
uploaded and drawn. Presentation is on frame arrival, not free-running: a new
frame (or a resize, or a display-setting change) triggers a redraw. When the
emulator is paused or the picture is frozen for a selection, the last frame
stays. The client does not drive presentation from a vsync or display-link
callback, because such a callback stalls when the display sleeps and does not
restart (section 4.4). Frame-level state must not invalidate the whole UI 50
times a second.

States and transitions: disconnected -> connecting -> connected, or -> error;
the stream ending or being cancelled maps to error, distinguishing a server end
from a lost connection by the prior state.

Rests on: `video.proto VideoService.GetConfig` (once, for frame dimensions);
`video.proto VideoService.SubscribeFrames` (a server stream of frames carrying
frame number, width, height, BGRA pixels, borders, display width and height,
field order, and regions).

Conformance:

- MANDATORY: one config fetch then a continuous frame stream; present whole
  frames; keep showing the last frame when none arrive; present on frame arrival
  (do not poll, do not invent frames, do not tie presentation to a display-link a
  sleeping display halts).
- LATITUDE: client-side texture caching, the paused-view redraw trick, a
  received-frame-rate probe to tell a frontend freeze from a core stall, whether
  to bypass UI batching.
- MACOS-ONLY: Metal, MTKView, the texture pixel format. A Qt client uses a Vulkan
  or GLES surface and should upload into an RGBA8 texture and swizzle in the
  shader to avoid depending on BGRA texture support.

Scenarios: on connect, exactly one config fetch then a continuous frame stream
whose frame count advances. Pause -> the picture holds. Kill the server
mid-stream -> the state goes to error. Resize while paused -> the picture
repaints.

### 6.2 Display modes and aspect ratio

Profile: Core. Binding document: `video-subsystem.md`.

Behaviour. All BBC modes 0 to 7, including Mode 7 teletext and custom CRTC modes
(split screens, wide and letterbox modes), render with correct geometry at a
constant apparent picture size; switching mode does not resize the active
picture. Aspect ratio is always preserved; the picture is letterboxed or
pillarboxed into the window and never stretched to fill it.

The client computes geometry only from server-supplied values, never from a mode
number: it stretches logical texels to each region's width and then to the
display width (so a 320-wide mode and a 160-wide mode both fill the display
width), applies the pixel aspect, doubles the height for progressive (bitmap)
modes while using interlaced Mode 7 as-is, and aspect-fits the result into the
drawable. Up to eight split-screen regions drive per-band horizontal scale. All
teletext rendering (character generation, control codes, hold graphics, double
height, flash, conceal) is the server's; the client only displays the pixels.

Conformance:

- MANDATORY: aspect ratio always preserved, never stretch-to-fill; letterbox or
  pillarbox the remainder; per-region horizontal scaling for split screens;
  height-doubling for progressive modes; the pixel aspect applied; the client
  treats mode only through server geometry, never by mode number; the server owns
  teletext rendering.
- LATITUDE: the sampling filter choice; how the quad or shader is structured.
- MACOS-ONLY: the Metal shader. A Qt client bakes equivalent shaders at build
  time for both backends.

Scenarios: a 320-logical mode with a 640 display width is 2x horizontally. Mode 7
to a bitmap mode -> the interlace flag flips, the apparent picture size holds. A
two-region split -> both bands fill the full display width. A wide or letterbox
custom mode -> a wider or taller aspect, correctly boxed.

### 6.3 Scaling, pixel shape, display styles, background

Profile: Advanced display.

Behaviour. The client offers a pixel-shape choice: authentic (the BBC PAL CRT
pixel aspect, about 0.96) and crisp (square pixels, 1.0, cleaner integer-ish
scaling). Scaling itself is continuous aspect-fit, not snapped to integers. The
client offers at least one display style that shows only the active picture with
the surrounding area filled by a configurable background (default near-black); a
development "debug" style that also shows the CRTC borders in distinct colours is
optional. A configurable inner edge margin is an optional refinement of the
standard style.

Conformance:

- MANDATORY (within this profile): a style that shows only the active picture
  with a configurable background fill; an authentic non-square pixel aspect
  available; the picture boxed, not stretched.
- LATITUDE: additional styles, the edge-margin feature, the background default,
  reset-to-default affordances, the crisp option.
- MACOS-ONLY: Metal pipelines, the color picker and stepper widgets.

Scenarios: toggle authentic and crisp -> the horizontal size changes by about
4%. Set a background colour -> the boxing colour updates, even while paused.

### 6.4 Immersive mode

Profile: Core.

Behaviour. Immersive mode is a Beebium "lights-down" presentation: the emulator
display fills the available client area with no application chrome (no title bar,
no status bar, no sidebar), in the current display style, with the surrounding
area the configured background and the pointer auto-hiding after a short idle.
Immersive mode is distinct from the operating system's own full-screen mode;
both may exist, and the client must provide a reliable way to leave immersive
mode. Emulation continues uninterrupted; only the framing changes. Immersive mode
is per window, is not persisted across launches, and auto-exits when the
connection is lost (it is only meaningful while live). Configuration surfaces
(sidebar, status) remain reachable while immersive, appearing over the picture
rather than reflowing it.

What is mandatory versus latitude needs care, because a client cannot assume it
can capture every host keystroke. On Wayland in particular, whether a full-screen
client receives compositor shortcuts (the Super key, Alt-Tab, the compositor's
own bindings) depends on the compositor honouring the keyboard-shortcuts-inhibit
protocol, which is not guaranteed. On Raspberry Pi OS it is not available: labwc
0.9.7 does not offer keyboard-shortcuts-inhibit (its wlroots implements the
protocol, but labwc never creates it), and Qt 6.8's Wayland client never
requests it either (checked on Raspberry Pi OS trixie, 2026-11-01; see
`docs/discussion/linux-client-architecture.md` section 10). The specification
therefore does NOT require that all host shortcuts reach the emulator in
immersive mode.

| Aspect | Conformance |
|--------|-------------|
| Display fills the client area without application chrome | MANDATORY |
| Aspect ratio preserved, remainder is the background | MANDATORY |
| A reliable exit that does not depend on the compositor yielding any shortcut | MANDATORY |
| Distinct from OS full screen | MANDATORY |
| Per window, not persisted, auto-exits on disconnect | MANDATORY |
| Config surfaces reachable as overlays, not reflowing the picture | MANDATORY |
| All host shortcuts reach the emulator | NOT MANDATORY |
| Requesting shortcut inhibition from the compositor | LATITUDE |
| Pointer auto-hide and its timing | LATITUDE |
| Implementing immersive atop OS full screen, or as a bespoke borderless mode | LATITUDE |
| The entering and leaving shortcut | LATITUDE |

A client may request shortcut inhibition where the platform offers it (on Wayland
via keyboard-shortcuts-inhibit; macOS captures ordinary keys already). When the
compositor refuses, the client must remain usable: the exit path must not be a
shortcut the compositor might swallow (an on-screen affordance, or a key the
client is certain to receive, qualifies), and the client may surface that some
host shortcuts are reserved by the desktop. Losing a captured shortcut must never
be able to trap the user in immersive mode.

Rests on: none; immersive mode is purely a client presentation concern.

Conformance: as tabulated. MACOS-ONLY: the native full-screen machinery, the menu
bar and Dock auto-hide, the pointer hide API.

Scenarios: enter immersive -> chrome gone, picture fills the screen, the command
toggles to "exit". Open the sidebar while immersive -> it appears over the picture
without reflowing it. Lose the connection while immersive -> the window returns to
windowed layout and shows the disconnect overlay. On a compositor that refuses
shortcut inhibition, the Super key still reaches the desktop, yet the user can
still leave immersive mode by the client's own exit affordance.

### 6.5 Screen text selection and Copy As

Profile: Advanced display. Binding document: `screen-text-library.md`. This
capability is optional and need not be present from a client's first release; a
client may ship without on-screen selection and add it later.

Behaviour. The user can select text directly on the screen in any mode and copy
it. A modified drag over the picture starts a selection; the picture freezes on a
server-captured still (the emulator keeps running) so the user selects on exactly
the frame being read; a highlight shows what was recognised and what is selected;
the selection stays live after release so the user can change interpretation and
copy; Escape or a plain click dismisses it. Three interpretations: rows (aligned,
reading order), columns (an aligned rectangular block), and anywhere (every glyph
in the marquee, including off-grid VDU-5 text). Copy writes plain text, one line
per covered row. With no selection, Copy copies the whole screen aligned.

For Mode 7, a persisted preference chooses how a byte is copied: as its character
code (keeping a BASIC listing's brackets and symbols) or as the glyph the screen
displayed (as Unicode). The client reads the preference fresh at each copy; it is
a setting the copies obey, not a fourth copy command.

All text recognition is the server's. The client never performs OCR; it asks the
server for the character grid and the recognised runs and maps them to the
picture geometry (the exact inverse of the display geometry, so highlights stay
registered through a resize).

Rests on: `video.proto VideoService.HoldScreen` (hold id, grid bands, and the
captured still), `ReleaseScreen`, `GetScreenGeometry` (the character grid), and
`GetScreenText` (a request carrying the search mode, layout, character mode, an
optional pixel region and the hold id; a response of supported flag, text,
unreadable and ambiguous cells, and per-cell matched runs). Holds expire
server-side, so a lost release is survivable.

Conformance (within this profile):

- MANDATORY: the server reads the text and returns runs plus a grid; the client
  never OCRs; hold the screen and read the captured still so the preview is
  honest; region coordinates in frame pixels matching the server's; the three
  interpretations; the Mode 7 code-versus-displayed distinction; plain-text output
  and whole-screen aligned copy.
- LATITUDE: the drag gesture and modifier mapping, overlay colours, the shortcuts,
  menu placement, where the Mode 7 preference is stored.
- MACOS-ONLY: the pasteboard, the mouse handling, the overlay drawing.

Scenarios: drag across a Mode 7 page -> the picture freezes, the highlight tracks
the recognised text, Copy writes the rows. Hold the columns modifier -> a snapped
column block. Hold the anywhere modifier over a VDU-5 title -> off-grid glyphs
highlighted. Toggle the Mode 7 preference -> a copied listing's symbols differ.
Resize mid-selection -> the highlight stays registered. Escape -> the selection
clears, the hold is released, the display resumes.

### 6.6 Video settings and persistence

Profile: Advanced display.

Behaviour. Video settings (display style, pixel shape, background, and any
per-style options) are a client concern and are per window. A preferences surface
sets the defaults applied to new windows; changing a default does not change open
windows. The macOS client additionally caches a machine's settings for the
process lifetime so sibling windows on the same machine pick up the latest, and
resets to defaults on quit; that caching is latitude.

Rests on: none; these are client preferences. The only server value involved is
the machine UUID, used as a cache key.

Conformance:

- MANDATORY (within this profile): video settings are a client concern and are
  per window; defaults for new windows are distinguishable from open windows'
  current settings; video settings persist across sessions per the client-only
  settings rule (section 3.5).
- LATITUDE: which settings exist; any per-machine caching and its lifetime (the
  macOS client's per-machine, process-lifetime cache is a convenience within the
  section 3.5 rule, not a substitute for persisting the defaults); the storage
  format; which parameters persist versus reset.
- MACOS-ONLY: the preferences store and the SwiftUI controls.

Scenarios: set a default style and open a new window -> it starts in that style;
open windows unchanged. Malformed stored values -> fall back to built-in defaults.


## 7. Keyboard

Binding documents: `frontend-modifier-keys.md`, `keyboard.md`.

The chapter turns on keeping three concepts apart:

1. Physical scancode: the host key position, layout-independent (macOS
   NSEvent.keyCode, which is an ANSI physical position; Linux the XKB keycode,
   that is the evdev code plus 8).
2. Host layout symbol: the character the host's active layout produces.
3. BBC key position: the BBC keyboard-matrix cell, encoded as an internal key
   number (row in the high nibble, column in the low nibble).

The server expects only the BBC key position. It knows nothing of host scancodes
or host layout symbols except through the canonical character-to-key table it
publishes, which the client may cache.

### 7.1 Hardware-path typing and type-ahead

Profile: Core.

Behaviour. Every physical keystroke is delivered to the machine as a BBC
keyboard-matrix press and release, exactly as on real hardware. There is no
"insert this character at a cursor" path: the client resolves a host key to a BBC
matrix position and sends a key-down and a key-up for that position. This is why
typing works everywhere typing works. Host auto-repeat is dropped; the machine
generates its own repeat. Wire sends are serialised so the machine sees events in
order.

Type-ahead lets the client queue text the server paces at machine speed, draining
only while the machine runs; the client watches a typing-status stream for drain
rather than polling. The status distinguishes idle, pending characters, whether
the machine is running, whether the guest is ready (the first OS input request
since reset), and how much was discarded by a reset.

Rests on: `keyboard.proto KeyboardService.KeyDown` and `KeyUp` (matrix presses);
`BreakDown` and `BreakUp` (the reset line); `GetState`; type-ahead `TypeQuickly`,
`WatchTypingStatus`, `GetTypingStatus`, `ClearTyping`; the canonical table
`GetAllKeyMappings`.

Conformance:

- MANDATORY: hardware-path matrix typing (send BBC key positions, never character
  injection); Break via the Break RPCs; serialised in-order sends; drop host
  auto-repeat; type-ahead via TypeQuickly and the status stream.
- MANDATORY: the emulated keyboard wins. The machine display is a control that
  can have keyboard focus, and while it has focus every key the active mapping
  sends to the machine goes to the machine, including CTRL and ALT with letters
  that the host platform would otherwise treat as application shortcuts (the
  emulator must see CTRL normally). The client shows clearly whether the
  display has focus (a visible focus indication on the display and a state in
  the status bar or equivalent), and stays reachable without the keyboard
  shortcuts it has ceded: menus by mouse and by mnemonic, full screen and the
  immersive exit on keys the mapping does not claim. Application shortcuts
  apply whenever the display does not have focus. (Rob, 2026-11-01. macOS is
  not affected in practice because its shortcuts use Command, which no mapping
  claims; Linux and Windows clients, whose conventions use Ctrl, are.)
  Refinement (Rob, 2026-11-03): a client on such a platform puts its
  machine-window shortcuts on modifiers the BBC Micro lacks, Alt or Meta,
  never on Ctrl or Shift alone, so that they work with the display focused,
  as Command shortcuts do on macOS; the Linux client's scheme is in
  `docs/discussion/linux-client-architecture.md`.
- LATITUDE: a two-stage resolver (direct scancode map then character map);
  whether the canonical table is cached; which keys outside the mapping the
  window keeps (F11, Alt mnemonics on Linux).
- MACOS-ONLY: using NSEvent.keyCode as the physical identity. A Qt client uses
  the native scancode (XKB keycode).

Scenarios: type a letter -> the server receives a matrix down then up for that
key's internal number. Hold a key -> exactly one down-up pair despite host
auto-repeat. Queue text -> it returns immediately with pending characters, and the
status stream eventually reports idle. Reset mid-queue -> a non-zero
discarded-by-reset count.

### 7.2 Modifier key rules and stuck-modifier safety

Profile: Core. Binding document: `frontend-modifier-keys.md` (rules R1 to R10).

Behaviour. BBC SHIFT and CTRL are never passed through from the host; they are
derived from the set of currently-held keys so the right character always results
and no modifier is ever left stuck down. Each key's modifier facts (does it need a
synthetic SHIFT or CTRL, does it forbid one) are frozen at press time. The desired
state of each modifier is computed over the whole held set: any held key that
forbids it wins (off), else any that needs it wins (on), else follow the physical
modifier. Only transitions are emitted. On a press, modifiers are driven before
the key; on a release, the key is released before the modifiers.

CTRL is asymmetric: a character never forbids CTRL, and while CTRL is held the
base key is resolved from the unmodified character (the host folds, for example,
Ctrl-M to a carriage return, which would mis-press RETURN).

Stuck-modifier safety is the single most important rule. On focus loss, on the
host taking over (for example starting a modified drag), and before swallowing a
qualifier for a client gesture, the client releases every held key and both
modifiers, synchronously (not on a deferred task). An untracked key-up is dropped.
On focus gain the client resynchronises to the real modifier state and fabricates
nothing.

Rests on: BBC SHIFT and CTRL are matrix cells sent synthetically through
`KeyboardService.KeyDown` and `KeyUp`; the server de-duplicates.

Conformance:

- MANDATORY: derive, do not pass through; freeze facts at press; the
  forbid-over-need-over-physical precedence; CTRL never forbidden and resolved
  from the unmodified character under CTRL; guard ups by the held set; serialise
  sends; release everything synchronously on focus loss and host takeover;
  resync (do not fabricate) on focus gain.
- LATITUDE: the internal data structures; whether the derivation is a pure unit.
- MACOS-ONLY: decoding AppKit device-dependent left/right modifier bits (Linux
  reports each modifier key separately); the synchronous release mechanism.

Scenarios: on a US layout, type the apostrophe with no physical shift -> the
server sees SHIFT down and the correct base key, producing an apostrophe. Hold
both shifts and release one -> the released side's up is sent, the other stays
down (not stuck). Ctrl with a letter -> the server sees CTRL plus that letter, not
RETURN. Hold SHIFT, switch away and back, type -> lowercase (SHIFT was released).

### 7.3 Caps Lock versus Shift Lock

Profile: Core. Binding document: `keyboard.md`.

Behaviour. The machine is authoritative over both the Caps Lock and Shift Lock
latches and re-initialises them on every reset (power-on default is Caps on,
Shift off); the client follows it rather than fighting it. Caps Lock may be driven
from the host where the platform can read the host's Caps state (opt-in); Shift
Lock has no common host equivalent and is only reflected from the guest, never
driven. Caps Lock is treated as a toggle with its own path, not as a held key; the
guest latch is toggled with a full down, a wait for a MOS scan, then an up,
because the MOS toggles on key-down only.

The decision to drive the guest is exact-latch based, not LED-brightness based,
and taps only when the host and guest disagree (so it never fights a reset). The
authoritative latch is read from the keyboard service, not inferred from the
lock-LED brightness (which is duty-cycle filtered). The sync points are: a user
keypress, window focus, connection, the guest becoming ready after any boot or
reset, and the sync-enabled toggle.

The "guest ready" sync point is the `guest_ready` edge of
`KeyboardService.WatchTypingStatus`: it rises when the MOS first asks for
input after a reset, so it is exact at any emulation speed and covers a reset
the client did not initiate. A client must not substitute a wall-clock settle
delay for it (the macOS client still does, and migrates; see #232).

Which locks are host-driven is deliberately per-platform. macOS drives Caps from
the host and leaves Shift Lock guest-only; a client on a host with a real Shift
Lock could drive both.

Rests on: `keyboard.proto KeyboardService.GetLockState` (the exact Caps and Shift
latch bits, correct at any emulation speed); the guest Caps toggle is ordinary
matrix key-down and key-up on the Caps Lock cell.

Conformance:

- MANDATORY: treat Caps Lock as a toggle with its own path; use the exact latch
  from the keyboard service, not the filtered LED, for sync and automation; the
  machine is authoritative and the client adopts rather than fights on reset; tap
  only on disagreement; sync after boot or reset on the `guest_ready` edge, not
  on a wall-clock delay.
- LATITUDE: which locks are host-driven (per platform); the toggle timing.
- MACOS-ONLY: Caps host-driven and Shift guest-only; detecting the host Caps
  toggle; the known limits (a client cannot read the host Caps state while
  unfocused).

Scenarios: toggle the host Caps while unfocused, refocus -> the guest Caps aligns
with one tap. Reset the machine -> the latches return to Caps-on/Shift-off and the
client mirrors without re-poking. Engage Shift Lock in the guest -> the client
reflects it but never drives the host. The lock-state query returns correct bits
even at unlimited speed.

### 7.4 Keyboard mapping files

Profile: Core.

Behaviour. A keyboard mapping resolves a host key (by physical position, and/or
by host layout symbol) to a BBC key position, with explicit per-entry BBC SHIFT
and CTRL overrides (not derived from the host), plus an optional "character
mapping" flag that enables host-layout-symbol resolution against the server's
canonical table. Mappings are stored as files; built-in mappings are read-only
and user mappings are editable, cloneable, importable and exportable. A client may
offer a graphical key-grid editor or a list; the macOS reference client currently
offers a sidebar list, not a grid.

Where a client presents the active mapping, it shows each entry's action label when
the mapping carries one, for the built-in mappings as much as for user mappings: a
built-in is not treated as unlabelled. The built-in Logical mapping labels the BBC
keys with no host namesake - f0, Break, Copy, Shift Lock and Delete - so the user
can see which host key reaches each; the game mappings label their in-game
functions ("Fire", "Thrust"). How the labels are laid out - a reference list, notes
on a grid, tooltips - is the client's choice, and whether a client offers a keyboard
view at all is latitude (GitHub issue #234).

What the macOS mapping files key on today, and the recommendation. The macOS files
key the direct map on the host physical scancode, stored in a per-platform section
(for example a macOS section carrying a key code) with explicit BBC SHIFT and CTRL
overrides; the format already keeps and round-trips per-platform sections it does
not understand, and names BBC keys by name (resolved to an internal number through
the server's table). A Linux client would add a Linux section keying on the evdev
code. That works, but it means a mapping authored on one platform does not serve
another without a per-platform section being added by hand.

The adopted form. A mapping file MUST key its physical entries on a
platform-neutral physical key code - the W3C UI Events code values such as "KeyZ"
and "ShiftLeft" - so one mapping file serves every client. Both macOS key codes
and evdev codes translate to and from these neutral codes by fixed, unit-testable
tables. Per-platform sections remain, but only for the few keys that have no
neutral code, and for round-tripping sections a client does not understand. This
makes a mapping a portable artifact (a user's game mapping follows them from a Mac
to a Pi) and keeps the translation table (needed anyway) as the single point of
platform knowledge. Each client needs its own platform-to-neutral table; existing
per-platform files can be read for back-compatibility and rewritten to neutral
codes on save. No server change is involved, since the server never sees mapping
files.

The macOS client does not yet conform: its keyboard mapping area is
underdeveloped and still keys the direct map on the host physical scancode in a
per-platform section rather than on neutral codes. The migration is: add a
platform-to-neutral translation table (already needed to interpret native
events), read the existing per-platform files for back-compatibility, and write
neutral codes on save, preserving unknown sections throughout. This is tracked as
GitHub issue #231.

Rests on: `keyboard.proto KeyboardService.GetAllKeyMappings` and `GetKeyMapping`
for the canonical character-to-BBC-key table used by character mapping.

Conformance:

- MANDATORY: a mapping resolves a host key (physical and/or layout symbol) to a
  BBC key position; BBC SHIFT and CTRL are explicit per-entry overrides, not
  host-derived; the canonical character-to-key table is the server's.
- MANDATORY: a new mapping file keys its physical entries on platform-neutral W3C
  UI Events code values, with per-platform sections only for keys lacking a
  neutral code and for round-tripping unknown sections. The macOS client does not
  yet conform (tracked as GitHub issue #231).
- MANDATORY: where a client presents the active mapping to the user, it surfaces
  that mapping's per-entry action labels where present, for built-in mappings as
  well as user mappings; a built-in is not shown as unlabelled (GitHub issue #234).
- LATITUDE: the editor (graphical, list or text), the built-in set, import and
  export, disableable keys, per-session overrides; whether to offer a keyboard view
  at all, and the form it takes (reference list, grid, tooltips).
- MACOS-ONLY: the macOS section's key-code form (a non-conforming legacy to be
  migrated per #231); the sidebar list UI.

Scenarios: select a non-default mapping -> subsequent keys resolve per it. A
mapping entry binding a physical key to Break triggers the Break RPCs. Import a
mapping with a section for another platform -> it round-trips on export unchanged.
A mapping authored on one platform using neutral codes -> it loads and works on
another with no hand-editing. Activate a game mapping -> the keyboard view lists its
labelled bindings; activate Logical -> the view still names the host keys for f0,
Break, Copy, Shift Lock and Delete.

### 7.5 Paste as typing

Profile: Core.

Behaviour. Paste types the clipboard's text on the emulated keyboard; there is no
OS text cursor. Translation is on by default (line endings to carriage return,
typographic punctuation to ASCII, teletext glyphs folded back) because pasting
moves characters, not bytes. The client waits for the server-reported drain
(through the typing-status stream), not a client timeout, so a cancel has
something to cancel; Escape cancels an in-flight paste (already-typed characters
remain). A non-text clipboard is refused with a notice; a failure is a sentence.

An optional "paste at full speed" runs the machine unthrottled until the queue
drains and mutes audio for the duration (nothing in the audio path is speed-aware,
so a boost without muting produces chopped noise); it restores the prior speed and
mute on every exit path, including failure. It is withheld when a real-time
network transport is active (a boost would sever the link), with the reason shown.

Rests on: `keyboard.proto KeyboardService.TypeQuickly` (text and a translate
flag), `WatchTypingStatus` (drain), `ClearTyping` (cancel).

Conformance:

- MANDATORY: paste is type-ahead over the matrix, not injection; translation on
  by default; wait for the server-reported drain, not a client timeout; restore
  any speed or audio change on every exit path.
- LATITUDE: the full-speed boost and its audio-mute coupling; the real-time-
  transport guard; Escape-to-cancel.
- MACOS-ONLY: the pasteboard and the menu wiring.

Scenarios: paste a string -> it arrives as typed, pasting is marked in progress
until the status stream reports idle. Full-speed paste -> audio muted during,
unmuted after, speed restored. Full-speed paste with a real-time transport active
-> the command is unavailable with a reason. Escape mid-paste -> the queue is
cleared, already-typed characters remain.

### 7.6 On-screen and auxiliary key input

Profile: Core.

Behaviour. A client may offer on-screen or auxiliary key controls (the macOS
client provides a Touch Bar strip of BBC keys and LED status). An auxiliary key
names a BBC key directly (it does not resolve a host scancode); it synthesises and
forbids nothing. Any such control, and any keyboard-view presentation of the
active mapping, shows the mapping's action labels where it carries them (7.4,
#234). The mandatory requirement it embodies (rule R10) is that every input path -
physical keyboard, on-screen, scripting - routes through one held-key set and one
send queue, keyed by an identity broad enough to tell them apart, so that
release-all on focus loss covers every path, and a non-matrix key such as Break
has its own release message.

Conformance:

- MANDATORY: one held-key set and one send queue across all input paths, keyed to
  disambiguate; a non-matrix key released by its own message.
- MACOS-ONLY: the Touch Bar in its entirety.


## 8. Audio

Binding document: `sound-subsystem.md`.

### 8.1 Format-driven audio fields

Profile: Core.

Behaviour. The audio stream is self-describing. Before decoding, the client asks
the server for the format: the sample rate, the number of 32-bit fields per frame,
and for each field its source name, encoding, channel names, group and a bipolar
flag. The client needs no hard-coded knowledge of which extensions add sound. The
SN76489 occupies the first fields; extensions (Music 5000, Music 3000) occupy
later fields. The format is fixed for the life of the server, so it is fetched
once.

The bipolar flag decides mixing: a bipolar field is signed with zero as silence
and is mixed as-is; a unipolar field's zero is not the AC midpoint, so the client
must remove its DC before mixing (the SN76489 is unipolar; the macOS client
high-passes it). A field is mixed only when its encoding is a signed stereo format
and it is flagged bipolar.

Rests on: `audio.proto AudioService.GetAudioFormat` (the format, with per-source
encoding, channel names, group id and bipolar flag).

Conformance:

- MANDATORY: interpret samples strictly per the format; honour the encoding;
  honour the bipolar flag to choose DC-removal versus mix-as-is; the server is the
  single source of truth for what each field is; the SN76489 is unipolar and its
  DC must be removed.
- LATITUDE: fetch-once caching; the DC-removal cutoff, normalisation and mix gains;
  whether to render extension fields at all.
- MACOS-ONLY: nothing intrinsic; the Swift unpacking is an implementation of the
  mandatory wire format.

Scenarios: on an SN-only server, the format reports the first fields as signed
stereo and not bipolar, no bipolar extension slot is set, and output matches the
SN-only path. With a Music 5000, a later field reports signed stereo and bipolar
and is summed with no DC-removal.

### 8.2 The group mixer: master, sub-masters, channels

Profile: Core.

Behaviour. A three-level mixer, driven entirely by the server-reported groups: a
master volume and mute at the top; one section per group, in the format's order,
each with its own sub-master and mute; and, for the group that owns per-channel
sources (the SN76489), per-channel sliders beneath its sub-master. A group that is
a stereo extension shows the sub-master only. Groups must never be hidden: every
group the server reports gets a section. Volume and mute are client-side DSP; there
is no RPC to set volume, because the stream carries raw samples and the client
mixes. The gains default to unity so the mix is bit-identical until a control
moves.

Rests on: `audio.proto AudioService.GetAudioFormat` (the groups, channel names and
grouping); mixing is entirely client-side.

Conformance:

- MANDATORY: the three-level hierarchy driven by the server-reported groups
  (master, per-group sub-master and mute, per-channel for the channel-owning
  group); groups, channel names and grouping from the format; never hide a group;
  mixing and volume are the client's responsibility (the server sends samples, not
  a mixed stream).
- LATITUDE: the volume curve, the mix-gain constants, a soft limiter, panning,
  channel row ordering, whether pan is exposed, the layout.
- MACOS-ONLY: the SwiftUI view and the atomic bridge to the render thread.

Scenarios: an SN-only server -> one group with a sub-master and four channel rows.
Mute the group -> it goes silent, the master is unaffected. With a Music 5000 -> a
second group with a sub-master only. Set the noise channel to zero -> only noise is
silenced. Master mute -> all silent and the group sliders grey.

### 8.3 Buffering, latency and buffer health

Profile: Core.

Behaviour. Audio has low steady-state latency but adapts upward if the host
stalls, trading latency for glitch-free playback. Between the receive thread and
the playback callback sits a lock-free single-producer single-consumer ring; any
parallel per-field rings are written in lockstep (equal capacity, identical
counts) so a given frame lines up in each and an overflow drops the same count from
each. Playback holds a target amount buffered (the latency); on an underrun the
target rises by a step up to a cap and is held for the session (no decay, so no
oscillation), and playback re-primes so there is one audible gap then stable
output; a new connection resets the target to the floor. Wire-level drops are
detected exactly from a per-chunk sequence number. A small "underruns, dropped"
line appears only once a glitch has occurred; zero means the host kept up.

The server does not pace to playback, so the client must buffer and must drop or
substitute silence rather than block.

Rests on: `audio.proto AudioService.SubscribeAudio` (a stream of chunks carrying a
sequence number, a sample count and samples); all buffering, pre-roll and health
are client-side.

Conformance:

- MANDATORY: a jitter buffer between receive and playback; detect wire drops from
  the chunk sequence number; keep any parallel per-field rings frame-aligned
  (equal capacity, lockstep read and write); never block the producer - buffer and
  drop or substitute.
- LATITUDE: the whole adaptive pre-roll policy (the floor, the step, the cap, the
  hold-no-decay and re-prime); the ring capacity; the chunk size; how buffer health
  is surfaced.
- MACOS-ONLY: the CoreAudio engine specifics and its post-wake mitigation. A Qt
  client may output through PipeWire or an abstraction over it.

Scenarios: a steady host -> health all zero, latency at the floor, no health line.
A short stall -> one underrun, the target rises by a step, one audible gap then
stable, the health line appears. Prolonged starvation -> the target climbs in steps
to the cap and never decays within the session. A new connection -> the latency
returns to the floor and the counters zero. The server drops samples -> the chunk
sequence jumps and the exact drop count is detected.


## 9. Storage

Binding document: `disc-subsystem.md`. Service: `disc.proto DiscService`.

### 9.1 Mounting disc images

Profile: Core.

Behaviour. Each floppy drive is one row. The client renders exactly the drives the
server reports, with the server's numbering; it does not hard-code a drive count.
An empty local drive offers a browse control (and recent and blank-disc entries);
an empty remote drive explains that the server is elsewhere. The set of accepted
image extensions comes from the server, not the client. On selecting or dropping an
image, the client sends an insert with the image URL; success is reflected through
the event stream, failure through the error surface (section 9.6). The image format
(SSD, DSD, HFE and so on) and side count are reported back by the server for
display; the client does not choose them. A disc is removed through eject, never by
inserting over an occupied drive (the server rejects that).

States and transitions: drive state is empty, loaded or ejecting, mirrored only
from the disc event stream, never polled; the initial snapshot comes from the
drive-status query.

Rests on: `disc.proto DiscService.InsertDisc`, `GetDriveStatus`,
`SubscribeDiscEvents`. The accepted content types come from the core (a preset
query), not the client.

Conformance:

- MANDATORY: insert is a server operation with a URL the server can open; insert
  fails on an occupied drive (removal is via eject); the drive list, numbering and
  formats come from the server.
- LATITUDE: the browse affordance and its layout; filtering the picker by the
  server-supplied content types.
- MACOS-ONLY: the open panel, Finder-alias resolution.

Gating: browse and insert-by-picked-path are gated on the server sharing the
filesystem (section 3.2).

Scenarios: insert into an empty drive -> a success event and the row shows the disc
name and, if write-protected, a read-only mark. Insert into a loaded drive -> the
client blocks it locally and the server would reject it. Browse is disabled when the
server is remote.

### 9.2 Recent discs

Profile: Core.

Behaviour. A shared, cross-drive list of recently inserted images, newest first,
capped and persisted. Only inserts the user made through the client are recorded,
and only on the server's insert success; discs a preset mounts at launch are not
recorded. Entries are de-duplicated by path; an entry whose file no longer exists
is shown but disabled; a clear command empties the list. This is client state; no
RPC. Re-inserting a recent disc goes back through the normal insert.

Conformance:

- MANDATORY (where a recent-discs feature is implemented): only inserts the user
  made through the client are recorded, and only on the server's insert success;
  discs a preset mounts at launch are never recorded. A client need not offer a
  recents feature at all, but one that does must follow this policy.
- LATITUDE: the list presentation, the cap, de-dup by path, stale entries
  disabled.
- MACOS-ONLY: the storage and the menu placement.

Scenarios: insert two images and eject both -> both in the list, newest first.
Insert the same path twice -> one entry, moved to front. Delete a recent file on
disk -> the entry shows but is disabled. Launch a preset that mounts a disc -> that
disc is not in the list. A failed insert -> not recorded.

### 9.3 Creating blank discs

Profile: Core.

Behaviour. The client fetches the server's blank-disc templates once on connect; a
failure is non-fatal and leaves the feature empty. Each template is offered by its
server-supplied label. Choosing one asks the user for a destination (restricted to
the template's own extension, because the server detects format from the extension)
and creates it, then inserts the created image. Create and insert are distinct
steps. The server refuses a wrong extension, so the client preserves the template's
extension.

Rests on: `disc.proto DiscService.ListBlankDiscs`, `CreateDisc`. Errors are
classified (unknown label, destination exists, write failed, wrong extension).

Conformance:

- MANDATORY: templates are enumerated by the server, not invented; creation writes
  to a server-host path; the format is carried by the extension and the server
  refuses a wrong one, so the client preserves it; create and insert are distinct.
- LATITUDE: the save flow; auto-insert after create.
- MACOS-ONLY: the save panel and the menu placement.

Gating: blank-disc creation picks a host path for the server to write, so it is
gated on the server sharing the filesystem.

Scenarios: a server with no templates -> the feature is disabled. Create with a
name lacking the extension -> the extension is added and the server accepts. Save
over an existing file -> the chooser confirms and the create replaces it. Create
success -> the disc is inserted and appears in recents.

### 9.4 Drag and drop of disc images

Profile: Core.

Behaviour. A hovering drag is judged only by what is knowable without reading the
file: the drive's state and whether the server shares the filesystem. What the file
is is never judged on hover (the bytes are not available and validity is the
server's call). Two hover refusals: the server is elsewhere, and the slot is
occupied. A droppable drag highlights the target; a refused drag shows the reason
inline. On drop the refusal is re-checked (state may have changed), a multi-file
drop is refused, and the single file URL is handed to insert (live) or to pre-launch
validation (in the configurator).

Conformance:

- MANDATORY: do not promise acceptance of a file the server has not judged;
  occupied-drive and remote-server are the only hover-time refusals consistent with
  the contract.
- LATITUDE: the inline refusal text; single-file enforcement; the highlight.
- MACOS-ONLY: the drop delegate and the file-URL handling.

Gating: a drop is refused outright when the server does not share the filesystem,
ahead of the occupancy check.

Scenarios: drag onto an occupied drive -> refused with "eject disc first". Drag when
the server is remote -> "server is on another host". Drop two files -> "drop a single
disc image". Drop a garbage file -> accepted on hover, then the server rejects it as
a failure.

### 9.5 Write-back and eject

Profile: Core.

Behaviour. Write-back is entirely the server's: floppy writes are flushed on
head-step-off, on write-inactivity, on shutdown and on eject; hard-disc writes are
write-through. There is no client flush. The client's only lever on durability is
eject, so disc removal must route through eject and the client must not assume a
disc's bytes are on the host file until an eject (or server policy) has flushed.

A safe eject is non-blocking and completed by the server once the drive is quiet;
the server never forces on its own and never times out. The client shows an ejecting
state and a cancel; after a short wait on a busy drive it may offer a force eject
(an immediate eject), which still flushes. Forcing is always the user's choice.

Rests on: `disc.proto DiscService.EjectDisc` (safe or immediate), `CancelEject`.

Conformance:

- MANDATORY: removal routes through eject; safe eject is non-blocking and
  server-completed; the server never times out; a client that wants to give up
  re-issues immediate or cancels; the client never assumes bytes are flushed before
  an eject.
- LATITUDE: the "offer force eject" threshold; the labels and the cancel/force
  affordances.

Scenarios: write to a disc and eject -> flushed before it leaves. Force eject a busy
drive -> still flushes, risking only an in-flight operation.

### 9.6 Disc error reporting

Profile: Core.

Behaviour. The server returns, for a refused disc operation, both a verbatim
human-readable message and a machine classification (cannot open, empty image, read
error, unrecognised format, no controller, invalid drive, drive occupied; and for
create: unknown label, destination exists, write failed, wrong extension). The
client reacts by classification without parsing the text, and always shows the
verbatim message. The failure is shown persistently and completely (the macOS client
uses a short inline marker plus a popover with the full text, anchored to the drive),
until the user clears it, a new outcome replaces it, or the disc is ejected. A
precondition notice ("eject disc first") may auto-dismiss. An unmapped future
classification degrades to an honest generic marker while still showing the verbatim
text. The same classification is used whether the image is judged by the running
server or, pre-launch, by the core's describe-image path.

Rests on: the classification enums and verbatim error strings on the insert, eject,
cancel and create responses.

Conformance:

- MANDATORY: the server returns both a human message and a machine kind; the client
  reacts to the kind without parsing the text and shows the verbatim text; every
  refusal is visible; an unmapped kind degrades to an honest generic.
- LATITUDE: popover-versus-inline; the marker strings; the sentence frames; the
  notice-versus-failure split and the notice lifetime.
- MACOS-ONLY: the transient-message model and the popover.

Scenarios: insert a zero-byte file -> an empty-image marker and the server text in a
popover. Insert on a machine with no controller -> a no-controller reaction. A future
kind not yet mapped -> a generic "insert failed" (still honest). Create over an
existing file without replace -> a destination-exists sentence.

### 9.7 Drive status indication

Profile: Core.

Behaviour. The drive identity, state and motor status come from the server. The row
title carries the server's drive number; a running motor is shown from the motor
events; a loaded disc shows its name and, where the server shares the filesystem, a
reveal affordance (Copy Path is always available). A write-protected disc shows a
read-only mark. Peripheral storage devices (hard discs) show activity from the
indicator stream (section 10). All of this is mirrored from the disc event stream,
never sampled.

Rests on: `disc.proto DiscService.GetDriveStatus` (drive number, state, motor,
current track, write-protect, disc metadata) and `SubscribeDiscEvents` (insert,
eject, motor on and off, and so on).

Conformance:

- MANDATORY: drive identity, state and motor from the server; drive the UI from the
  event stream, not polling (so an eject-then-insert is not missed); motor and
  activity are distinct (motor is drive state; the floppy activity LED is an
  indicator).
- LATITUDE: the motor glyph; showing or omitting the current track; the
  write-protect mark; Copy Path and reveal.
- MACOS-ONLY: the reveal, the pasteboard, the context menus.

Scenarios: a catalogue command spins the motor -> a motor indication appears then
clears. A quick eject-then-reinsert -> the row shows the swap, not a stale disc. A
write-protected file -> a read-only mark. A remote server -> no reveal, Copy Path
still present.


## 10. Indicators (real LEDs only)

Profile: Core. Binding document: `indicators.md`. Service:
`indicator.proto IndicatorService`.

Behaviour. The client renders every indicator the server reports, in the server's
registration order, each labelled and coloured from its metadata, each a brightness
(0 to 255), not a boolean, because real LEDs are duty-cycle driven. Indicators model
only physically observable hardware LEDs (the Caps and Shift lock LEDs, per-drive
activity LEDs, and any an extension registers such as a hard-disc activity LED). The
indicator channel is not a general UI-state channel: there is no "paused",
"connected" or "error" indicator here; those are UI concerns elsewhere. The client
hard-codes no indicator identities; it draws whatever is registered. Because the
lock LEDs track the hardware latch exactly, a change to them doubles as a race-free
signal for the client to re-read the authoritative lock state (section 7.3).

Rests on: `indicator.proto IndicatorService.ListIndicators` (metadata and order,
once) and `Subscribe` (a stream of value changes).

Conformance:

- MANDATORY: indicators are discovered, not hard-coded; values are 0 to 255
  brightness; only real hardware LEDs are exposed; metadata carries label and colour;
  render whatever is registered, in registration order.
- LATITUDE: the glyph (an annulus, a dot); the colour and brightness maths; choosing
  the subscribe over the poll RPC; a click-to-toggle via a metadata related-key; the
  lock-LED-as-change-signal optimisation.
- MACOS-ONLY: the colour handling and the SwiftUI shapes.

Scenarios: a Model B -> two lock LEDs, no drive LEDs. A Model B+ -> two more drive
LEDs. A catalogue command -> the floppy activity LED pulses. Attach a SCSI extension
-> an extra activity LED appears automatically. A client must not invent an LED the
server did not register.


## 11. Sideways ROM/RAM slots

Profile: Peripheral management. Binding document: `sideways-slots.md`. Service:
`sideways.proto SidewaysService`.

Behaviour. The topology is the server's: logical slots versus physical sockets,
aliasing (where one socket answers several slot numbers), per-socket capabilities,
motherboard links, and whether a socket is runtime-configurable. The client displays
sockets (with their aliased slot sets), not raw slots, for a hardware-faithful view,
highest boot priority first. Each socket shows its ROM title (parsed server-side from
the ROM header), a ROM/RAM/empty status, and, where applicable, a write-protect or
hide (read-protect) control. ROM-header parsing and all topology are the server's;
the client only maps and displays them.

The live view is read-only except for protection. Protection is modelled
board-agnostically as named groups of slots a single switch governs (a one-slot
write group, a whole-board write group, a read-protect "hide" group), with two kinds:
write-protect and hide. The client engages protection by group id and kind and then
re-reads the state (there is no push event, and the switch acts on every covered slot
at once); the control reflects the server-reported state, never an optimistic click.
Loading or clearing a slot's content is a pre-launch concern done through the machine
draft (section 5.2), not a live RPC; runtime slot reconfiguration requires a
configure call and is gated server-side on the runtime-configurable flag.

A `*SRLOAD` into sideways RAM on a running machine updates the socket's title live,
because the client opts into header-change monitoring on the event stream.

Rests on: `sideways.proto SidewaysService.GetSlotStatus` (topology, sockets, aliasing,
ROM headers, protection groups), `SubscribeEvents` (with header-change monitoring),
`SetSlotProtection`. The configure and read-data RPCs exist but the reference client
does not use them.

Conformance:

- MANDATORY: topology and ROM-header parsing from the server; display sockets with
  their aliased slot sets; protection engaged by group id and kind with a re-read
  after (no push event); a protection control reflects server state, not optimistic
  clicks; live slot-content reconfiguration requires the configure RPC and is
  server-gated.
- LATITUDE: a read-only live view; opting into header monitoring; priority-descending
  order; the protection control's presentation.
- MACOS-ONLY: the ROM-browse panel, Copy Path and reveal, the list and menus.

Gating: revealing a slot's image file requires both that the server shares the
filesystem and that the path exists on this host.

Scenarios: a Model B -> four sockets, each listing its aliased slots, with an aliasing
note. A `*SRLOAD` on a running machine -> the live title updates without a full
refetch. Toggle a one-slot write group -> one slot flips; a whole-board group -> all
covered slots flip together after the re-read. Hide a socket's ROM -> it is
read-protected even though it is a ROM. A protection change against a machine with no
such switch -> rejected, the control unchanged.


## 12. Extensions and peripherals

The governing theme: the sidebar panels and the extension parameter forms are
server-driven declarative UI. The server describes a view or a manifest; the client
renders generically by control type, never by id or by a hard-coded extension name.

### 12.1 Extension-supplied sidebar views

Profile: Peripheral management. Service:
`extension_ui.proto ExtensionUiService`.

Behaviour. An extension supplies a view the client renders. The model is
server-owned state and a thin client: the server holds all state and computes a view;
the client subscribes per extension, receives a view immediately and a fresh one on
every change, and posts user actions back. There are no per-control RPCs. A view is a
tree of control primitives: label, indicator, toggle, button, choice, text input,
group, modal editor, editable choice, editable list, and file reference. Each view
carries a monotonic revision the client must echo in every action, so the server can
reject stale actions. An action is fire-and-observe: the response says only whether
the action was accepted; the state change appears as the next pushed view.

The client renders by control type, recursively, keying widget identity on the
control id so updates patch in place. A new extension therefore renders with no client
change. An indicator's state is semantic (unknown, ok, warn, error) and the client
chooses the rendering. When a subscription ends because the extension is gone or
unloaded, the client drops the cached view (a missing extension is not an error that
blocks the UI); when it ends because the server was lost, the client keeps the last
view and may show it as stale.

Rests on: `extension_ui.proto ExtensionUiService.SubscribeView` (a stream of views)
and `Dispatch` (a user action carrying the view revision).

Conformance:

- MANDATORY: render the control tree by type; key identity on the control id; echo
  the view revision in every action; treat actions as fire-and-observe (the state
  change comes on the stream); a missing extension does not read as a blocking error;
  a lost server keeps the last view.
- LATITUDE: the widget chosen for each primitive; the stale presentation.
- MACOS-ONLY: the specific SwiftUI widgets and a type-erasure workaround for a Release
  build issue.

Scenarios: subscribe to a live extension -> a view renders within a round trip;
toggling a control dispatches a value and the next pushed view reflects it. An action
with a stale revision is rejected and changes nothing. Unload an extension mid-session
-> the panel is dropped, not shown as an error. Kill the server -> the last view is
retained.

### 12.2 The configurator and parameter forms

Profile: Configuration editing.

Behaviour. The configurator edits a client-owned draft (section 5.2) built from the
extension manifest, reusing the same control primitives as the live views but binding
them to the draft rather than to server-held state. Parameter kinds are classified
from the manifest (choice, boolean, integer with min/max, serial port, string,
filepath and dirpath, structured list, string list, map-file), and each maps to a
native control. A filepath parameter's access (read or write) decides the chooser: a
write parameter (the server creates the file) offers a create-destination affordance
(a save chooser), a read parameter offers an open chooser; a dirpath offers a
directory chooser. The noun shown for a path is taken from the manifest, not
hard-coded per extension.

Editing is commit-on-end: a field edits a local buffer and commits only on Return or
focus loss, never per keystroke, so the owning view is not re-rendered mid-edit and
the caret survives. One coordinator enforces the order commit, then release, then act,
so a pending edit lands before any sibling control acts and a field never commits a
stale value over another control's change. Editor forms (modal editors, and
editable-list add and edit) buffer values and bundle an atomic commit, so the server
never sees a partial edit.

Two shared composite controls:

- Editable list: a titled list whose rows, add/remove affordances and per-item actions
  all come from the server; the client never invents an action the view did not list;
  an action may carry its own prefilled editor or a confirmation warning. The same
  stack serves both a live extension view and a structured-list draft parameter.
- File reference: a named file with a state badge and a menu of server actions
  (dispatched by id), plus client-side reveal (only when the server shares the
  filesystem and the path is non-empty) and Copy Path.

Structured list items use a manifest template of brace-delimited placeholders with
literal separators and a typed field list; the client converts between the object, the
field strings and a one-line display, and always writes objects (keeping an unparseable
integer verbatim so the resolver can flag it). Mutually-exclusive parameter groups
render as one picker; an exclusion pair disables the excluded field with a note; the
exact-record rule (write only what differs from the default) keeps an untouched preset
byte-exact.

Rests on: the extension manifest from `peripheral_extension.proto
PeripheralExtensionService.ListExtensions` (and the preset schema); the live editable
list and file reference ride `extension_ui.proto` (section 12.1).

Conformance:

- MANDATORY: honour the manifest's parameter kinds, the filepath access semantics (a
  write parameter offers a create destination), the structured-list template and the
  typed fields; commit-on-end with the commit-release-act order; atomic editor
  commits (no partial applies); never invent a server action; exact-record editing.
- LATITUDE: the widgets; the add/remove affordance; the chooser type.
- MACOS-ONLY: the open and save panels, the combo box, the segmented add/remove
  control, the NSText-backed commit machinery.

Scenarios: a write-access filepath opens a create-destination chooser, a read-access
one an open chooser. Editing a field then clicking a checkbox commits the field first.
A structured-list item edited in the configurator writes the identical object the live
view would. Setting a field to its default clears the key from the draft.

### 12.3 Peripheral discovery tree

Profile: Peripheral management. Service:
`peripheral_extension.proto PeripheralExtensionService`.

Behaviour. The client discovers the live peripheral set by listing it once on connect
(the set is fixed at server start). Each extension carries its type, id, label, display
name, description, whether it has a UI, its config, its storage devices, what it
attaches to and what it provides, and its attachments with their provider ids. The
client builds a tree using the server-chosen provider id as each node's parent, in the
server's attachment-point order, trusting the server's labels and display names (a
client humaniser is only a last-resort fallback). A node that has a UI embeds an
extension view (section 12.1); storage devices a peripheral publishes surface in the
storage view.

Pre-launch, the configurator projects the extension schema's attachment points over
the draft: each point is a node, its instances are children, and a child that itself
provides points nests further. Single-occupancy points offer add, change and remove;
multi-occupancy points offer add while there is room. A chooser lists candidate
extensions with their description, what they need (their parameters) and what they
provide (the points they add); every string is catalogue text, nothing per-extension
in the client. Removing a provider does not cascade: an orphan is left for the resolver
to diagnose rather than silently deleting more.

Rests on: `peripheral_extension.proto PeripheralExtensionService.ListExtensions`
(extensions and attachment points); live per-node state via `extension_ui.proto`.

Conformance:

- MANDATORY: discover by the listing RPC, not hard-coded; a node's parent is the
  server-chosen provider, not re-derived; trust the server's labels and display names;
  non-cascading removal with server-diagnosed orphans; the chooser's content is
  entirely manifest-derived.
- LATITUDE: the tree layout, indentation, badges.
- MACOS-ONLY: the tree and chooser widgets.

Scenarios: two host adapters each providing a bus -> each nests its own children under
the right adapter. Removing a provider leaves its dependents as diagnosed orphans.
Adding to a full single-occupancy point is impossible.

### 12.4 Printer, serial, Econet and AUN surfaces

Profile: Peripheral management. Services: `serial.proto SerialService`,
`econet.proto EconetService`, `econet_transport.proto EconetTransportService`, and
transport-specific control over `extension_rpc.proto`.

Behaviour.

- Printer. A printer sink is just an extension on the printer-port attachment point;
  it has no bespoke client code. Its configuration (including a write-access filepath
  for a file sink) and its status panel are built from the manifest and the generic
  framework.
- Serial. The on-board serial state is read-only status: whether a serial socket is
  fitted, the connector type, and the RS423 transmit and receive baud (shown only while
  RS423, not cassette, is selected). The user does not set baud here; host-side serial
  devices are configured as serial-client extensions through the generic parameter form
  (which offers a list of host serial ports). The client shows an interface line above
  the serial extension panels.
- Econet and AUN. The user sees link state, the Econet station number (the number in
  force, and, when a change has been set but not yet adopted, the configured number as
  pending with a Break affordance that adopts it at the next Break), a validated
  station-number editor (bounds from the server), a collision gauge (a count currently
  in effect, cleared by the server, not a running total), and a banner when the active
  transport needs real-time emulation but the speed is not 1x. Per-transport control
  (AUN peers, the map file, connect and disconnect) lives entirely in the transport
  extension's own server-driven panel, keyed by the transport's opaque server-assigned
  id; the client holds no knowledge of specific transport types, so a new transport
  surfaces with no client change. A lost status stream keeps the last-known
  configuration but marks it stale and clears the live gauges.

Pre-launch Econet configuration (on/off, station mode and number within the server's
bounds, and the transport chosen through the same chooser, with a "none" meaning fitted
but not bridged) lives in the draft and reuses the generic parameter form; AUN peers and
subnets are structured lists edited exactly as the live surface edits them.

Rests on: `serial.proto SerialService.WatchSerialStatus`;
`econet.proto EconetService.GetEconetStatus`, `WatchEconetStatus`, `EnableEconet`,
`DisableEconet`, `SetStationId`, `SubscribeEconetEvents`;
`econet_transport.proto EconetTransportService.ListTransports`, `GetActiveTransport`;
transport control via `extension_rpc.proto` routed by instance id.

Conformance:

- MANDATORY: printers (and sinks) are plain extensions, no printer-specific client
  code; on-board serial state is read-only status; the Econet station settled-versus-
  pending state machine; station bounds and the collision gauge come from the server;
  address transports by their discovered opaque id, never by name; honour a transport's
  real-time requirement.
- LATITUDE: the interface line wording; the station editor presentation.
- MACOS-ONLY: the specific panels and popovers.

Scenarios: set a station number -> the row shows the pending number until a Break, then
collapses to it. Two stations claim one number -> the collision warning appears and
clears when resolved. A transport with a UI renders its panel keyed by its id; one
without renders none. Set the speed off 1x with a real-time transport -> the paused
banner appears.

### 12.5 Debugger surface

Profile: Debugging (optional). Service: `debugger.proto DebuggerControl` and
`CoprocessorDebuggerControl`.

Behaviour. The server offers full execution control (state, run, stop, reset, step,
execution-state watch, breakpoints, watchpoints, memory access, CPU state) and a
coprocessor debugger. A client may expose a debugging UI over these, or none. The
macOS reference client exposes none beyond the single run call that unblocks a server
launched blocked (section 5.5). The Memory sidebar mode is not a debugger view: it is
the read-only sideways slot view (section 11), driven by the sideways service, and it
does not read arbitrary memory.

A full debugger will most likely be a separate, dedicated debugger client rather
than part of a GUI emulator client. This profile therefore stays optional for a
GUI client, and a dedicated debugger client is the expected home for the
debugging surface. The server's execution-control services are the contract both
would share.

Conformance:

- MANDATORY: nothing (the whole profile is optional); a client that launches
  blocked-start servers must issue the run call.
- LATITUDE: the entire debugging UI if present.

Scenario: launch a blocked-start server -> the client's run call unblocks it; nothing
else in a Core/Peripheral client touches the debugger.


## 13. Application shell

### 13.1 Sidebar modes

Profile: Core.

Behaviour. A machine window offers a set of sidebar modes in a fixed order: storage,
memory (sideways), peripherals, video, sound, keyboard, processor (emulation speed),
network. The sidebar shows one mode at a time; the selection persists. Extension views
are not a separate mode: they are embedded inside the peripherals and network modes,
one panel per node or transport that has a UI, each subscribing on appear and
unsubscribing on disappear, so switching away from a mode releases its extension
subscriptions. Each mode shows a header and, when the server is not there to back the
shown state, an unavailable banner with the content dimmed and disabled. Where a
client also offers a configurator, its sections follow the same order and omit modes
it has nothing to configure (video, sound), so the two surfaces cannot drift.

Conformance:

- MANDATORY: switching away from a mode releases that mode's extension subscriptions;
  a mode shows an unavailable treatment when the server is gone; where both a sidebar
  and a configurator exist, they share an order.
- LATITUDE: the mode switcher widget; which modes a client offers (tied to its
  profiles); the staleness presentation.
- MACOS-ONLY: the SF Symbol icons and the Command-digit shortcuts.

Scenarios: switch from peripherals to storage -> the peripheral panels' subscriptions
end. The server dies -> every mode shows the unavailable banner and dims its content.

### 13.2 Application menus and commands

Profile: Core.

Behaviour. A client organises its commands into menus with platform-appropriate
shortcuts. The reference client's command set, as a guide to what a client offers (the
grouping and shortcuts are latitude; KDE-style Ctrl-based shortcuts and a traditional
File/Machine/View/Tools/Help menu bar are the Linux recommendation):

- File: New (open the presets view), Open a saved machine, Open Recent (a previews
  view, not a names submenu), Rename Machine (for a transient machine), Import Presets,
  Connect, Save, Save As, Close. On a platform with a home window (section 13.4) the
  application-level commands among these (New, Open, Open Recent, Import Presets,
  Connect, Configure) live in the home window, and a machine window's File menu holds
  the machine's own commands plus one that brings the home window forward.
- View: Show/Hide Status Bar, Enter/Exit Immersive Mode, Show/Hide Sidebar, and one
  command per sidebar mode.
- Edit: the standard editing commands (they apply in the client's own text fields),
  plus the screen-text copy variants grouped with Copy (copy as columns, copy text from
  graphics), Paste at Full Speed grouped with Paste, the Mode 7 copy-as setting, and a
  switch-keyboard-mapping command.

Conformance:

- MANDATORY: the commands exist for the capabilities the client implements (new/open
  a machine, connect, save, the view toggles, the copy variants where Advanced Display
  is implemented, paste).
- LATITUDE: menu organisation, shortcuts, whether a command is a menu item, a toolbar
  button or both. On Linux, Ctrl-based shortcuts, a traditional menu bar, F11 for OS
  full screen with an obvious exit, F1 for help.
- MACOS-ONLY: the Command-key shortcuts, the macOS menu placement workarounds, the
  Settings scene, the Touch Bar.

### 13.3 Settings and preferences

Profile: Core (the surface) with per-area profiles.

Behaviour. App-wide preferences are distinct from per-machine configuration. The
reference client groups them as general, machines (the user's preset library: list,
duplicate, edit-name, export to a preset archive, delete, reveal), keyboard (the active
mapping, per-key disables, the Caps-sync override), and video (pixel shape, display
style, per-style options, background). The division of concerns and which settings are
app-global versus per-machine is latitude. A client that edits presets must round-trip
them as the server's preset archive format and keep an untouched preset byte-exact.

Conformance:

- MANDATORY: preset editing round-trips the server's archive format and keeps an
  untouched preset byte-exact (the exact-record rule, section 5.2); client-only
  preferences persist across sessions (section 3.5).
- LATITUDE: the panes, the grouping, which settings are global versus per-machine, the
  storage.
- MACOS-ONLY: the Settings scene and its tabs.

Scenarios: duplicate a preset -> a uniquely-named user copy persists across launches.
Export a preset -> the suggested name and type come from the stored file, not a
hard-coded extension.

### 13.4 The entry point and the home window

Profile: Core.

Behaviour. Launched with no machine, link or other request, the client leads with
the most common thing a user does: start a new machine from a preset. Opening a
saved machine and connecting to a running one are reachable from the same place
in one step, in that order of prominence, since that is their order of frequency.
Connecting to a running server is the rarest and most expert of the three and is
never the front door. Launched with a request (a saved machine, a preset id, a
connect link), the client opens that machine's window only and shows no front
door (section 4.1).

On macOS the front door is the welcome window, which the always-present menu bar
lets vanish without losing the application's commands. On Linux and Windows there
is no application-level menu bar: a machine window carries only its own menus, so
the application's commands need a window of their own. The presentation on those
platforms is a home window, in the manner of a virtual-machine manager or a
project manager: a persistent, ordinary window that is the application's face. It
holds, as pages or sections in this order: the presets catalogue with a Launch
command (section 5.1); saved machines, recent ones first, with
Open (section 5.3); running machines, discovered on this host and on the network,
with Connect and the address entry (section 4.1); and the application's own
commands: Configure (section 13.3), the preset library and editor where the
client has them (sections 5.2, 13.3), About, Quit. A machine window has no New,
Open, Connect or Configure commands of its own; it has a command that brings the
home window forward.

The page shown first follows the macOS welcome window's rule: a user with saved
machines in the recent list lands on saved machines, a user with none lands on
the presets catalogue; a command that opens the window (New, Open Recent)
chooses its page. The welcome window's presets and saved-machine pages are the
model to follow, as a guide rather than a drawing: cards with the thumbnail or
a placeholder, the name, and the model description or the last-used time; a
saved machine that is open is marked as running; a saved machine offers Open,
reveal in the file manager, and remove from the list.

Lifetime. Closing the home window does not quit while a machine window is open;
the home window can be reopened from any machine window. When the last machine
window closes and the home window is closed, the client quits. Quit, from the home
window or a machine window, closes every machine window under the rules of
section 4.5. The home window is never a duplicate: a request to show it brings
the one forward.

Conformance:

- MANDATORY: new-from-preset leads; open and connect are one step away from the
  entry point; a launch with a request shows no front door; the home window's
  lifetime rules above where a client has one.
- LATITUDE: whether the front door is a welcome window or a home window; the
  pages and their layout; which application-level commands the home window
  carries beyond New, Open, Connect and Configure.
- MACOS-ONLY: the welcome window and the always-present menu bar it relies on.

Scenarios: launch with no arguments -> the presets catalogue, with saved and
running machines reachable without a further dialog. Launch with a saved
machine's path -> that machine's window and nothing else. On a home-window
platform: close the home window with a machine running -> the client stays; close
that machine's window -> the client quits. Close the last machine window with the
home window open -> the client stays, showing the home window.


## Appendix A. Server services used, by capability

| Proto | Service and representative RPCs | Capability sections |
|-------|---------------------------------|---------------------|
| system.proto | SystemService: GetSystemInfo, WatchServerStatus, RequestShutdown, SaveMachine, MoveMachine, SetMachineName, SetSpeedMultiplier, GetPacingStats, WatchPacingStats, ResetBattery, SetMachineDescription, ListDescriptionPlaceholders | 3.2, 4.1-4.5, 5.3-5.7 |
| video.proto | VideoService: GetConfig, SubscribeFrames, HoldScreen, ReleaseScreen, GetScreenGeometry, GetScreenText | 6.1, 6.2, 6.5 |
| keyboard.proto | KeyboardService: KeyDown, KeyUp, BreakDown, BreakUp, GetLockState, TypeQuickly, WatchTypingStatus, ClearTyping, GetAllKeyMappings | 5.5, 7.1-7.5 |
| audio.proto | AudioService: GetAudioFormat, SubscribeAudio | 8.1-8.3 |
| disc.proto | DiscService: InsertDisc, EjectDisc, CancelEject, GetDriveStatus, SubscribeDiscEvents, ListBlankDiscs, CreateDisc | 9.1-9.7 |
| indicator.proto | IndicatorService: ListIndicators, Subscribe | 10 |
| sideways.proto | SidewaysService: GetSlotStatus, SubscribeEvents, SetSlotProtection | 11 |
| extension_ui.proto | ExtensionUiService: SubscribeView, Dispatch | 12.1, 12.2 |
| peripheral_extension.proto | PeripheralExtensionService: ListExtensions | 12.2, 12.3 |
| serial.proto | SerialService: WatchSerialStatus | 12.4 |
| econet.proto | EconetService: GetEconetStatus, WatchEconetStatus, SetStationId, EnableEconet, DisableEconet, SubscribeEconetEvents | 12.4 |
| econet_transport.proto | EconetTransportService: ListTransports, GetActiveTransport | 12.4 |
| extension_rpc.proto | ExtensionRpc (transport control, routed by instance id) | 12.4 |
| debugger.proto | DebuggerControl: Run (and the full debugging surface) | 5.5, 12.5 |
| (CLI) | list-presets, describe-preset-schema, show-preset, resolve-preset, import-preset, describe-machine, start | 5.1-5.4 |


## Appendix B. Conformance profiles summary

- Core: 3.1-3.4, 4.1-4.5, 5.1, 5.3-5.7, 6.1, 6.2, 6.4, 7.*, 8.*, 9.*, 10, 13.1-13.3.
- Configuration editing: 5.2, 12.2 (and the configuration parts of 11, 12.3, 12.4).
- Peripheral management: 11, 12.1, 12.3, 12.4.
- Advanced display: 6.3, 6.5, 6.6.
- Debugging: 12.5 (optional in full).
