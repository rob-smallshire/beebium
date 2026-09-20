# macOS: mirror the emulated Caps/Shift Lock state (issue #73)

## The bug

After the machine is reset — the Break key, Ctrl-Break, however initiated — the
macOS app's idea of Caps/Shift Lock diverges from the machine's. Reported
symptom (#73): after a crash then Break, Shift Lock stays "stuck" in the app, so
typing comes out lower case / shift-mangled (`?&2000` → `?&"000`) until the user
cycles Shift by hand.

## Root cause (confirmed, not assumed)

The emulation is **faithful**. A committed Python characterisation test
(`clients/beebium-python-client/tests/test_shift_lock_break_reinit.py`, on
master) proves that after every Break variant (plain, Ctrl, crash→Break) the MOS
re-initialises to **Caps Lock ON, Shift Lock OFF**, and the three server signals
— the lock latch (`GetLockState`), the `caps-lock-led` / `shift-lock-led`
indicators, and the actual typed-character case — always agree.

The divergence is entirely app-side: the macOS client drives the emulated lock
from the host keyboard and only resyncs **once**, on the first `caps-lock-led`
update (`IndicatorClient.hasTriggeredInitialSync`), plus some launch timers
(`BEEBIUM_CAPS_SYNC_DELAY_MS` in `ContentView`, `asyncAfter(2.0)` in
`MachineManager`). After a reset the machine re-inits its lock but the app's
mirror does not follow, so it keeps applying the stale Caps/Shift state.

## The fix (agreed shape)

Treat the **emulated lock state as authoritative** and mirror it continuously —
for **both Caps Lock and Shift Lock**, not just Caps.

- The emulated state is observable as a race-free **push** signal: the
  `caps-lock-led` and `shift-lock-led` indicators (already delivered to
  `IndicatorClient`). They change exactly when the MOS re-inits on reset, so
  mirroring them resyncs at the right moment with no timing race. `GetLockState`
  (`LockKeyState.caps_lock_on` / `shift_lock_on`) is available for a point-in-time
  read (e.g. initial state).
- Whenever a lock LED changes, resync the app's lock mirror (and the value it
  drives into the emulated keyboard) to it. This **subsumes** the one-shot
  `hasTriggeredInitialSync` and the launch timers — a reset is just another lock
  change the app now follows.
- Keep the user's host key working (pressing host Caps Lock still toggles the
  machine), but on a machine-driven change (reset) the **emulated state wins**.

The machine-reset event (#101) is intentionally **not** used here — the lock LEDs
already give a race-free signal, and #101 fires before the MOS finishes
re-initialising. #101 is a separate, later capability.

## Reproduce first (the actual defect)

Per #73's own to-do and the project's test-first rule, extract the lock-state
logic into a **testable class** (not SwiftUI callbacks) and write a **failing**
unit test first: drive the class with a simulated reset — the emulated lock LEDs
change to caps=on / shift=off while the app's mirror holds the pre-reset value —
and assert the mirror is now stale (reproduction). Then make it pass by having
the class adopt the emulated state, and add a Caps-Lock case too.

## Files (from the survey; the macOS dev owns the exact refactor)

- `IndicatorClient.swift` — `caps-lock-led` handling and `hasTriggeredInitialSync`
  (line ~145); add `shift-lock-led`.
- `KeyboardClient.swift` — `capsLockIsOn()` / `getLockState` (line ~501),
  `capsLock.sync` (line ~546).
- `ContentView.swift` (`BEEBIUM_CAPS_SYNC_DELAY_MS`) and `MachineManager.swift`
  (`asyncAfter(2.0)`) — the launch-timer resync this replaces.

## Acceptance

- A unit test of the lock-state class: reproduction goes red, then green after
  the fix; covers both Caps and Shift Lock and the reset transition.
- `xcodebuild build -scheme Beebium` clean.
- Manual: engage Shift Lock in the running app, Break; typing is upper case
  (Caps re-on) and not shift-mangled, and the app's lock indicators match the
  machine. A short recording of the before/after if practical.

## Process

Spec: this doc (atpl-sidewise session owns it). Implementation:
`macos-client-developer`. Report back the changed files + test; the atpl-sidewise
session reviews before merge. Branch off master (e.g. `issue73-lock-resync`);
don't push. No server/proto change is needed.
