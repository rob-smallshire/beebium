# Lifecycle and Connection Management

Beebium is a multi-process system: each emulated machine is a separate
**server** process, and one or more **clients** (the macOS app, the Python and
TypeScript libraries) talk to it over gRPC. That architecture buys clean
separation and platform-native UIs, but it makes *lifetime* a first-class
concern rather than an afterthought:

- A client and its server are **separate OS processes** with independent
  lifetimes. Either can vanish without warning, and the network between them
  (for remote connections, which are supported) can fail silently.
- A client holds **several long-lived gRPC streams** (video, audio, status,
  indicators, …) plus unary calls, multiplexed over one channel.
- The macOS app is **multi-window / multi-machine**: each window owns its own
  client stack and connection.
- The UI framework (SwiftUI) **overrides the obvious hooks** (window delegates,
  `onDisappear`), so teardown has to be wired in non-obvious ways.

The result is a fair amount of deliberate complexity. This document explains the
model, the subtleties behind it, and the rules that keep it correct, so the
machinery is not accidentally "simplified" back into a bug.

> Related: [grpc-server.md](grpc-server.md) (service API),
> [pacing.md](pacing.md) (emulation-thread timing),
> [disc-subsystem.md](disc-subsystem.md) (data write-back, a *data* lifetime
> concern covered there).

---

## 1. Connection liveness model

The authority for "is the server still there?" is the **`SystemService.
WatchServerStatus` stream** — the status facility — and *only* that. The video
stream is kept single-purpose; it is never used to infer liveness (frame rate
varies with emulation speed, and a paused emulator stops sending frames while
remaining perfectly connected).

The macOS client models liveness as `SystemClient.Liveness`:

| State | Meaning | How it is detected |
|-------|---------|--------------------|
| `active` | Connected and healthy | Events (incl. heartbeats) arriving |
| `stopped` | Server shut down **on purpose** | A `SHUTTING_DOWN` event preceded the stream ending |
| `died` | Server process ended **unexpectedly** (crash/kill) | The stream ended *without* a shutdown announcement |
| `unreachable` | Server alive but **no longer reachable** (network partition, frozen process) | The heartbeat **watchdog timed out** while the stream was still open |

The crucial distinctions:

- **`stopped` vs `died`** is the difference between an orderly stop and an
  unceremonious death. They read differently in the UI and are both **terminal**
  (there is nothing to reconnect to), so each shows an explanatory overlay with a
  single **Close**.
- **`unreachable` is recoverable.** It is *not* terminal: any subsequent event
  returns liveness to `active`. The UI shows a spinner ("Connection lost — Trying
  to reconnect…") that **auto-clears** when traffic resumes, with a single button
  to abandon. If the connection instead truly dies, the stream ends and it
  escalates to `died`.

`liveness` is deliberately **sticky across `disconnect()`** so a non-`active`
value survives the client-teardown cascade long enough to be shown; it resets to
`active` on the next `connect()`.

### The heartbeat

A silently-unreachable peer is the hard case: no shutdown event arrives and the
socket is never reset, so the stream just goes quiet. To make that detectable,
the server emits a periodic liveness tick:

- **Server:** `SystemService::WatchServerStatus` writes a
  `SERVER_STATUS_HEARTBEAT` event every **500 ms** (the watch loop already wakes
  every 100 ms to check for cancellation, so the tick slots in cheaply).
- **Client:** `SystemClient` runs a **2 s watchdog**, reset on *every* inbound
  event. If it fires, liveness becomes `unreachable`. 2 s over a 0.5 s heartbeat
  tolerates jitter and a few missed beats (e.g. a momentarily busy main thread)
  while still reacting within ~2 s.

`SERVER_STATUS_HEARTBEAT` is a proto enum value, so adding it bumped the protocol
fingerprint (see §5).

---

## 2. Why detecting loss is subtle

Several non-obvious gRPC/transport behaviours shaped the design. They are easy to
get wrong and were each verified empirically.

- **A server-streaming call's status future *succeeds* with a non-OK code; it
  does not throw.** In gRPC-Swift, `try await call.status.get()` returns the
  `GRPCStatus` (even `unavailable`) rather than throwing, so detection keyed on a
  `catch` never fires on a mid-session drop. Detect via the *status code* / the
  stream *completing*, not via a thrown error.

- **A streaming call does not notice a silently-dead connection.** With no RST
  and no application traffic, the call waits indefinitely (TCP's own timeout is
  minutes-to-hours). This is exactly why the heartbeat exists — absence of
  heartbeats is the only timely signal for a partition or a frozen peer.

- **Local kill ≠ network loss.** A local `SIGKILL` makes the OS reset the
  loopback socket, so the stream fails promptly (`unavailable`) → `died`, no
  heartbeat needed. A network partition produces no RST → only the heartbeat
  timeout catches it → `unreachable`.

- **Graceful shutdown holds the socket open.** On `SIGTERM` the server announces
  `SHUTTING_DOWN` and then waits for client streams to close (see §4), so the
  socket lingers. The client must act on the *event*, not wait for the socket to
  drop.

- **gRPC keepalive is not usable here.** Client-side gRPC keepalive pings are
  rejected by the C++ server's default policy with `unavailable(14): Too many
  pings`, and could tear down healthy connections. The application-level
  heartbeat over the existing status stream avoids the ping-policy fight
  entirely and is the chosen mechanism.

- **Intentional vs unexpected disconnect** is told apart by the completion
  status code: a `cancelled` status means *we* cancelled the stream (window
  close / reconnect) and is ignored; any other completion is a real loss. This
  is captured at completion time so a later teardown `cancel()` cannot
  retroactively mask a genuine failure.

### Simulating each case locally

| Case | Command | Expected |
|------|---------|----------|
| Graceful stop (`stopped`) | `pkill -TERM -f beebium-model-b` | "Emulator stopped", Close |
| Crash/kill (`died`) | `pkill -9 -f beebium-model-b` | "Emulator stopped unexpectedly", Close |
| Unreachable (`unreachable`) | `kill -STOP $(pgrep -f beebium-model-b)` | spinner after ~2 s |
| …recovery | `kill -CONT $(pgrep -f beebium-model-b)` | spinner auto-clears |

`SIGSTOP` is the key trick: it freezes the server process with its **sockets
open and no RST**, which is a faithful stand-in for "alive but unreachable" — the
call *hangs* (it does **not** return the prompt `unavailable` a kill gives).
Always finish with `SIGCONT` or the server stays frozen.

---

## 3. Client teardown: one long-lived event loop group

`VideoClient` owns a **single, process-wide `MultiThreadedEventLoopGroup`**
(`static let sharedGroup`), reused across all clients and reconnects and **never
shut down** (it is reclaimed at process exit). The other clients share
`VideoClient`'s gRPC channel, so it is the only owner of the group.

This is deliberate and load-bearing. The previous design created a fresh event
loop group per connection and tore it down on every `disconnect()`. When the
server vanished mid-stream, `GRPCChannelPool.close()` did not reliably cancel
gRPC-Swift's pending reconnect timer, and the following `syncShutdownGracefully()`
destroyed an unresolved `EventLoopFuture` — tripping a SwiftNIO precondition and
**aborting the app** (`EXC_BREAKPOINT` on `EventLoopFuture.deinit`). Event loop
groups are meant to be long-lived; keeping one for the process lifetime sidesteps
the teardown race whether the server exits cleanly or crashes.

**Rule:** `disconnect()` closes the *channel* (best effort, off the main thread);
it never shuts the group down.

---

## 4. Orderly teardown and shutdown

### Closing a window

SwiftUI's `WindowGroup` manages its own `NSWindowDelegate`, so `windowShouldClose`
and `onDisappear` cannot be used for cleanup. Instead, `WindowCloseCoordinator`
**redirects the close button's target/action** to itself. All decision logic
lives in `MachineManager` (`windowCloseAction`); the coordinator only runs the
flow. The disconnection overlays' **Close** button routes through this same
close-button action so it gets the identical teardown — not a raw `window.close()`.

### Disconnect before SIGTERM

The server's graceful shutdown **waits for active streams to close**. So when the
client initiates a server shutdown it must **disconnect its gRPC clients first**,
or the server blocks waiting for streams that the client still holds. The order
is encoded in `ClientGroup` (`Disconnectable` protocol): non-video clients are
dropped in reverse registration order, then the video client. All `disconnect()`
implementations are idempotent.

### Server-side shutdown

- **`SIGTERM` (graceful):** the server emits `SHUTTING_DOWN`, coordinates
  subsystem shutdown, and flushes disc drives so saved data reaches the host
  image (see [disc-subsystem.md](disc-subsystem.md) for the write-back triggers).
- **`SIGKILL`:** immediate; the OS resets sockets. Clients detect `died`. No
  disc flush runs, which is one reason DFS write-back also flushes on the
  in-emulation paths (step-away, write-inactivity) rather than only at shutdown.

### Recovery semantics

Two layers. **Passive**, for `unreachable`: while heartbeats have stopped but the
stream is still open, the client just waits for them to resume — covering a
frozen process (`SIGCONT`) and a network partition that heals while the TCP
connection survives.

**Active**, for a connection that truly dies (the stream ends — `error`/`died`):
`ReconnectCoordinator` runs a real reconnect loop — reconnect the channel-owning
video client (the rest cascade off `connected`), backing off 0.5s→8s over a
fixed number of attempts, then a terminal "couldn't reconnect" overlay the user
can retry. A **graceful shutdown (`stopped`) is never fought**, and a window
close cancels the loop first. Waking from a full system sleep forces an immediate
attempt (the accelerant) — see [frontend-sleep-wake.md](frontend-sleep-wake.md).
The `unreachable`/`error` spinner therefore now genuinely reconnects rather than
only waiting.

---

## 5. The protocol fingerprint

Client and server evolve and release together; there is no backward-compatibility
shim. To make a mismatched pairing fail loudly at connect rather than mysteriously
mid-call, each client carries a compiled-in **protocol fingerprint** and checks it
against the server's (`GetSystemInfo`).

- The fingerprint is computed from the **canonical proto descriptor set without
  source info**, so it is invariant to comments, formatting, declaration order,
  file names, and imports — only structural changes move it.
- After any proto change, run `scripts/sync_protocol_fingerprint.py` to
  regenerate the constant for **all** languages (C++ server, Python, Swift,
  TypeScript). If they drift, clients refuse to connect.

Adding `SERVER_STATUS_HEARTBEAT` was a structural change and therefore moved the
fingerprint; all four constants were regenerated together.

---

## 6. Code map

| Concern | Location |
|---------|----------|
| Liveness states, heartbeat watchdog | `clients/macos/.../SystemClient.swift` (`Liveness`, `armHeartbeatWatchdog`) |
| Disconnection overlays | `clients/macos/.../ContentView.swift` (`statusOverlay`, `disconnectionOverlay`, `reconnectingOverlay`, `recoveryFailedOverlay`) |
| Active reconnect loop | `clients/macos/.../ReconnectCoordinator.swift` (+ `ReconnectCoordinatorTests`) |
| Host sleep/wake abstraction | `clients/macos/.../SystemPowerMonitor.swift` (`SystemPowerMonitoring`, `MacSystemPowerMonitor`) |
| Shared event loop group, channel teardown | `clients/macos/.../VideoClient.swift` (`sharedGroup`, `disconnect`) |
| Window close flow | `clients/macos/.../ContentView.swift` (`WindowCloseCoordinator`), `MachineManager` |
| Bulk disconnect ordering | `clients/macos/.../Disconnectable.swift` (`ClientGroup`) |
| Heartbeat emit, status stream | `src/service/include/beebium/service/SystemService.hpp` (`WatchServerStatus`) |
| Status proto / heartbeat enum | `src/service/proto/system.proto` (`SERVER_STATUS_HEARTBEAT`) |
| Fingerprint generator | `scripts/sync_protocol_fingerprint.py`, `scripts/protocol_fingerprint.py` |
| Heartbeat cadence test | `clients/beebium-python-client/tests/test_heartbeat.py` |

---

## 7. Rules to preserve

1. **Liveness comes from `WatchServerStatus`, not the video stream.** Frame rate
   is not liveness.
2. **Never create/destroy an event loop group per connection.** One shared,
   long-lived group; `disconnect()` closes the channel only.
3. **Detect stream loss by the completion status code, not a thrown error.**
4. **Disconnect gRPC clients before asking the server to shut down.**
5. **Close windows via the close-button action** (`WindowCloseCoordinator`), not
   a raw `window.close()`.
6. **Keep `stopped`/`died` terminal and `unreachable` recoverable** — do not
   collapse the three into one "disconnected" state; they mean different things
   and the user needs to know which.
7. **Run `sync_protocol_fingerprint.py` after any proto change.**
