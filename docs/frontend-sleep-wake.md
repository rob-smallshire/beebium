# Front-end sleep/wake and connection recovery

How a Beebium front-end keeps its connection alive across host sleep. Written
for future platform front-ends (Windows, Linux): the model is
platform-independent; the macOS specifics are called out as such. See
[lifecycle-management.md](lifecycle-management.md) for the connection/liveness
model this builds on.

## Two kinds of sleep

- **Display sleep** — the screen turns off but the process keeps running. The
  connection survives; nothing needs re-establishing. The only requirement is
  that rendering resume when the display returns, which the video path already
  guarantees by **presenting on frame arrival** rather than from a free-running
  display link (a display link stalls while the display sleeps). A front-end
  must not tie presentation to a vsync/display-link callback that a slept
  display can halt.

- **Full system sleep** — the whole machine suspends. The process is frozen; on
  wake, any TCP connection is very likely dead and the server (if local) was
  suspended too. This is what needs active recovery, below.

The OS wake signal used for recovery must be the one that fires for **full
system sleep only**, not display sleep — otherwise a healthy display-sleep
session is needlessly torn down. On macOS that is
`NSWorkspace.didWakeNotification` (posted on `NSWorkspace.shared`'s own
notification centre); display sleep does not post it.

## The platform abstraction

Power events are consumed through a protocol so the recovery logic never names
an OS API:

```
protocol SystemPowerMonitoring: AnyObject {
    var onWillSleep: (() -> Void)? { get set }   // imminent full sleep
    var onDidWake:   (() -> Void)? { get set }   // woke from full sleep
    func start(); func stop()
}
```

Contract: callbacks fire on the main thread; `onDidWake` means "a live
connection is very likely dead — re-establish it"; `onWillSleep` is a chance to
stop futile work (a reconnect backoff) that a sleep would only run once on wake
anyway. macOS conforms via `MacSystemPowerMonitor` (NSWorkspace). A new platform
provides one conformer; nothing else changes.

## Active reconnect

Recovery is **active**, not passive — a dropped stream does not heal itself, so
the connection is rebuilt. It is driven by `ReconnectCoordinator`, whose whole
job is *when* to reconnect and *how long* to keep trying:

- **Trigger.** An unexpected drop — the connection state going to `error`, not
  an intentional stop — starts a backoff loop: reconnect, and on each renewed
  failure wait longer before the next try (0.5s doubling to an 8s cap), up to a
  fixed number of attempts, then give up to a terminal state the user can
  retry. A **graceful server shutdown is never fought** (it announces itself),
  and a user-initiated window close cancels the loop before teardown.
- **Wake accelerant.** `onDidWake` forces an immediate attempt and resets the
  backoff, so recovery is prompt instead of waiting out a stale delay from
  before the sleep. `onWillSleep` cancels the pending backoff.
- **One reconnect rebuilds everything.** Re-establishing every stream is
  expressed as a single reconnect of the client that owns the shared channel
  (the video client); the other clients reconnect off the resulting
  `connected` transition. A front-end should keep this property — one channel
  owner, the rest cascading — so recovery stays a single action.

The decision logic (trigger, backoff schedule, give-up, intentional-stop
suppression) is injected and unit-tested without any gRPC or OS dependency
(`ReconnectCoordinatorTests`). Keep the *policy* testable and the *OS event* and
*reconnect action* injected.

## Server side

The server needs no wake handling, but its pacing clock must not treat the
sleep gap as owed emulation to catch up on wake: a tick spanning more than a
second is taken as a suspend and the pacing baseline is re-anchored to the wake
(see `PacingClock::cycles_for_next_tick`). `std::chrono::steady_clock` keeps
advancing across macOS sleep, so without this the emulator would fast-forward
the whole sleep on wake.
