# macOS: Caps/Shift Lock resync on reset (issue #73)

> Supersedes the earlier draft of this doc. The earlier "make the machine
> authoritative and mirror the emulated state for both locks" was wrong for
> Caps Lock — it removed the host→guest Caps Lock sync feature. Caps and Shift
> Lock need different treatment.

## The bug

After the machine is reset (the Break key, however initiated), the macOS app's
Caps/Shift Lock state diverges from the machine. Reported symptom (#73): after a
crash then Break, Shift Lock is "stuck" so typing comes out lower case /
shift-mangled (`?&2000` → `?&"000`) until the user cycles Shift by hand.

Confirmed faithful server behaviour (Python test `test_shift_lock_break_reinit.py`,
on master): after every Break the MOS re-inits to **Caps Lock ON, Shift Lock OFF**,
and the lock latch (`GetLockState`), the `caps-lock-led`/`shift-lock-led`
indicators, and the typed-character case always agree. The divergence is app-side.

## Three separate concerns — keep them distinct

1. **LED indicators** (status bar) **always reflect the GUEST** state, from the
   `caps-lock-led`/`shift-lock-led` IndicatorService values. Display only. (Already so.)
2. **Caps Lock sync** is **host → guest**. On macOS you *cannot* drive the host
   Caps Lock from the guest, and cannot read/align it as guest→host. The whole
   point of the feature is: when Caps Lock sync is enabled, the app makes the
   GUEST caps match the HOST's caps. This must be **restored** — the previous
   attempt removed it.
3. **Shift Lock** on macOS has **no host equivalent**, so it is **guest-only**:
   the app just reflects the guest shift-lock (this is the actual #73 symptom).
   NOTE: this is macOS-specific — Linux/Windows hosts *do* have a physical Shift
   Lock, so which locks are host-synced must be a **per-platform, per-lock
   policy**, not hardcoded to "shift is always guest-only".

## The fix

### Reset is the trigger; boot is its power-on instance

The server now emits **`SERVER_STATUS_MACHINE_RESET`** on `WatchServerStatus`
(with `ResetKind` soft/hard) for every reset — see #101, on master. Use **one**
resync routine, fired by both:

- the initial connect (`SERVER_STATUS_READY`) — the boot case; and
- each `MACHINE_RESET` event — the reset case.

Because the event fires *before* the MOS finishes re-initialising the locks,
reuse the existing **delayed** sync (the `BEEBIUM_CAPS_SYNC_DELAY_MS` settle
that the L3FS work added) so the MOS doesn't overwrite the applied caps. Boot and
reset then share the exact same delayed-sync path — "boot is the power-on
instance of a reset".

### Caps Lock (host→guest, restore it)

On the resync trigger (connect or reset), after the settle delay, if Caps Lock
sync is enabled, push the **host** caps state into the guest (the pre-fix
behaviour). This is what re-aligns caps after a Break to what the user's Mac
Caps Lock says.

### Shift Lock (guest-only on macOS)

Keep reflecting the guest shift-lock (the `shift-lock-led` change → read
`GetLockState` → update the app's mirror/UI). The `LockStateReconciler` and the
`shift-lock-led` handling from the previous attempt are the right shape for this
half — keep them for Shift Lock (and for the LED display of both locks).

### Structure for other platforms

Make "which locks the host drives into the guest" a per-platform policy so a
future Linux/Windows front-end can host-sync Shift Lock too. On macOS: caps =
host-synced, shift = guest-only.

## Server surface to use

- `SystemService.WatchServerStatus` → `SERVER_STATUS_MACHINE_RESET`, field
  `reset_kind` (`RESET_KIND_SOFT`/`HARD`). **Regenerate the Swift system.proto
  stubs** to get it. Do **not** run `sync_protocol_fingerprint.py` — the
  fingerprint is already synced across all clients (including
  `ProtocolFingerprint.swift`); just regen the system stubs.
- `caps-lock-led` / `shift-lock-led` indicators (already consumed).
- `keyboard.GetLockState` (`caps_lock_on` / `shift_lock_on`) for the exact latch.

## Reproduce first

Keep the testable class approach. The reset-transition unit test from the
previous attempt still applies to the Shift Lock reflection. Add a test that the
resync routine, on a reset trigger, re-applies the host caps into the guest
(Caps Lock host→guest), and does not fight the MOS (it runs after the settle).

## Acceptance

- Unit tests: shift-lock reflection across reset; caps-lock host→guest re-applied
  on the reset trigger.
- `xcodebuild build -scheme Beebium` clean.
- Manual: with Caps Lock sync on, engage Shift Lock, Break → typing is upper
  (Caps re-on), not shift-mangled, indicators match the machine, and caps still
  tracks the host Caps Lock afterwards.

## Process

Spec: this doc (atpl-sidewise session). Implementation: `macos-client-developer`,
revising branch `issue73-lock-resync` (restore caps host→guest sync; wire the
resync to connect + the new reset event via the delayed settle; keep the
shift-lock reflection). Report changed files + tests; the atpl-sidewise session
reviews before merge. Don't push.
