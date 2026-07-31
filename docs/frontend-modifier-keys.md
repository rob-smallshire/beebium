# Handling modifier keys in a Beebium front-end

This document captures how to correctly translate host keyboard input -- and
in particular the modifier keys SHIFT and CTRL -- into BBC key events, so that
each new platform front-end does not have to rediscover the same failure modes.
Every rule here was learned by fixing a real "stuck SHIFT" bug.

The macOS front-end is the reference implementation. The hard-won rules are
platform-independent; the AppKit specifics are called out separately.

The overriding goal: **the emulated keyboard must never be left with a key or
modifier stuck down.** A missing key press is a minor annoyance the user fixes
by pressing again; a stuck modifier corrupts everything they type next and looks
like the emulator is broken.


## 1. The model: derive BBC SHIFT/CTRL, do not pass them through

A front-end does **not** forward the host SHIFT key straight to a BBC SHIFT key.
The BBC SHIFT/CTRL state is *derived* from the whole set of currently-held host
keys, because host and BBC keyboard layouts differ:

- A host key resolves, via the server's key mappings, to a BBC key. That BBC key
  may itself be a *shifted face* -- e.g. on a US host `'` maps to the BBC `7`
  key's shifted face, so producing `'` requires BBC SHIFT even though the user
  did not hold host SHIFT.
- Conversely, a host `Shift+;` producing `:` may map to an *unshifted* BBC key,
  so BBC SHIFT must be *released* even though the user is holding host SHIFT.

So the front-end must:

1. **Resolve each host key at press time** to a BBC key, and capture immutable
   *facts* about that press:
   - `needsSyntheticShift` -- BBC SHIFT must be held while this key is down,
     regardless of physical SHIFT.
   - `forbidsShift` -- BBC SHIFT must be released while this key is down,
     regardless of physical SHIFT.
   - the same pair for CTRL.
   These are decided **once, at the moment of press, and frozen** for the
   lifetime of that press. Freezing them is what makes the model resilient to
   the user releasing physical SHIFT while a character key is still held.

2. **Track the set of held keys**, keyed by their physical identity (host
   keyCode / scancode), each carrying its captured facts.

3. **Derive the desired BBC SHIFT/CTRL** from that set, with this precedence
   (per modifier):
   1. any held key `forbids` the modifier -> modifier **off**;
   2. else any held key `needsSynthetic` -> modifier **on**;
   3. else the modifier follows the *physical* state (the user directly holding
      the mapped SHIFT/CTRL key).

4. **Send synthetic BBC SHIFT/CTRL down/up** to reconcile the derived state with
   what was last sent.

In the reference implementation these live in `BBCModifierState.desiredShift /
desiredCtrl` (pure functions over the held-key facts) and `KeyboardClient`
(which owns the held-key set and emits the synthetic modifier transitions).


## 2. Platform-independent rules

**R1 -- Track held keys as a set keyed by physical identity; derive, never
toggle.** The modifier state is a *function of the held set*, recomputed on
every change. Do not maintain a "SHIFT is down" boolean you flip on each event;
that desynchronises the moment any single event is missed or duplicated.

**R2 -- Decide each key's down/up from the event's absolute state, not by
toggling your own.** An event carries the current state; use it. Absolute
decisions are self-correcting: a spurious or missed event does not accumulate
error. (The server also de-dups -- see R8 -- but do not rely on that to paper
over a wrong set.)

**R3 -- Modifier identity is per-physical-key (left vs right).** Never decide a
specific SHIFT key's up/down from a coalesced "SHIFT is down somewhere" flag.
If the user holds both SHIFTs and releases one, the coalesced flag stays set, so
the released side is misread as still-down and its release is lost -> stuck.
Use the physical left/right identity of the key that changed. The same applies
to CTRL, Option/ALT and Command.

**R4 -- Release everything on focus loss.** When the machine view (or the app)
loses input focus, the OS delivers the pending key-*up* to whoever gains focus,
not to you. Any key or modifier held at that instant would stay down forever.
So on focus loss, release **all** held keys and modifiers on the emulated
matrix, and reset your local tracking. This is the single most important rule:
it is the classic mid-game "stuck SHIFT after Cmd-Tab / a notification / an
alert", and it needs no Copy/Paste or special feature to trigger.

**R5 -- When a host interaction takes over input, release everything; do not
selectively suppress.** Front-ends legitimately withhold some keys from the
emulator -- a menu shortcut, a drag-to-select gesture, an overlay. The trap is
suppressing a modifier's *down* but not its *up* (or vice versa), which strands
it. Depending on the order the user releases keys -- which you cannot control --
either edge can be the one you swallow. The safe rule is all-or-nothing: the
moment a host interaction takes ownership, release every emulated key; let the
user re-press afterwards. (A host-only modifier such as Command, which has no
BBC equivalent, is a clean signal that "this is a host action, not machine
input".)

**R6 -- On regaining focus, resync tracking but do not fabricate presses.**
Reconcile your local tracking to the real current modifier state, but do not
synthesise `down` events for keys the user "might still be holding". Require a
fresh press. A missing key is recoverable; a fabricated stuck key is not. Prefer
the release-and-repress direction every time.

**R7 -- Ignore host auto-repeat.** The MOS runs its own key auto-repeat. Forward
only genuine down/up transitions; drop repeat events.

**R8 -- Guard key-up dispatch by your held-set.** Only send an `up` for a key
you actually recorded as `down`. An up for a key that is not held becomes a
no-op. This keeps an accidental unbalanced up (e.g. left over from a suppression
path) from disturbing the emulated matrix.

**R9 -- CTRL is not a character-generating modifier; never resolve a key
through it.** SHIFT and ALT select *which character* a key produces -- `Shift+3`
is how a UK host types `£` -- so resolving those from the host's
modifier-applied character is correct. CTRL is different: a host reports
`Ctrl+M` as `"\r"`, which names a control code rather than the key that was
pressed, and the BBC performs that same folding itself in hardware and the MOS.
Resolving on it applies CTRL twice and presses a *different physical key*
(`Ctrl+M` -> RETURN, `Ctrl+I` -> TAB, `Ctrl+H` -> backspace, `Ctrl+[` ->
ESCAPE).

The character usually comes out the same either way, which is why this hides
easily -- but guest software that scans the keyboard matrix sees the wrong key.
Commstar's viewdata mode is a real example: it takes character 13, then calls
OSBYTE 122 to ask which key is held, and substitutes the viewdata `#` only for
RETURN. With CTRL mis-resolved, `Ctrl+M` became indistinguishable from RETURN
and no AT command could be typed.

So: resolve the base key from the *unmodified* character (or the physical key
code) whenever CTRL is held, and let CTRL reach the BBC independently. It
follows that a character-mapped key must never *forbid* CTRL either -- unlike
SHIFT, which legitimately suppresses a physically-held modifier when the
resolved character is the unshifted face.


## 3. The server contract

- You send key **down/up by matrix position** (or internal key number). BBC
  SHIFT and CTRL are ordinary matrix cells -- SHIFT at (row 0, col 0), CTRL at
  (row 0, col 1) -- but you send them **synthetically**, as the *derived* state
  from section 1, not as a passthrough of host SHIFT/CTRL.
- The server **de-dups**: a `down` for an already-down key, or an `up` for a
  key that is not down, is ignored. Lean on this for robustness (R2, R8), but it
  cannot rescue a genuinely wrong held-set.
- **Order matters on the wire.** Serialise sends so the machine sees modifier
  and key transitions in generation order; a keyDown whose synthetic SHIFT is
  computed after a following keyUp has already run its bookkeeping will drop the
  modifier. (The reference client chains each send behind the previous one.)
- **Boot-time SHIFT/CTRL is the ROM's job, not yours.** SHIFT-Break auto-boot,
  CTRL-Break, and the keyboard startup links are decided by OS 1.20 from the key
  state you deliver at reset. You only have to deliver *correct* key state; the
  ROM applies `boot = (SHIFT AND NOT CTRL) XOR autoboot-link` (which is why
  CTRL-Break is "reset without booting"). See `clients/beebium-python-client/
  tests/test_autoboot.py` for the full, verified behaviour.


## 4. macOS (AppKit) specifics

- **Events.** Regular keys arrive via `keyDown(with:)` / `keyUp(with:)`.
  Modifier keys arrive via `flagsChanged(with:)`, whose `event.keyCode`
  identifies *which* physical modifier changed.
- **Left vs right (R3).** `NSEvent.ModifierFlags` (`.shift`, `.control`, ...) is
  coalesced and cannot tell left from right. Read the **device-dependent bits**
  from `event.modifierFlags.rawValue` instead:

  | key | left mask | right mask |
  |-----|-----------|------------|
  | Shift   | `0x0002` | `0x0004` |
  | Control | `0x0001` | `0x2000` |
  | Option  | `0x0020` | `0x0040` |
  | Command | `0x0008` | `0x0010` |

  Decide "is *this* keyCode down" from its own bit; fall back to the coalesced
  flag only when neither device bit is present (some synthetic events). See
  `KeyboardMTKView.modifierKeyIsDown(keyCode:rawFlags:)`.
- **Command combinations** are returned to the menu system via
  `performKeyEquivalent(with:)`; `keyDown` early-returns while Command is held.
- **Focus (R4).** Observe `NSWindow.didResignKeyNotification` -> release all
  keys (`KeyboardClient.releaseAllKeys()`); observe `didBecomeKeyNotification`
  -> reset modifier tracking. There is *no* `onDisappear`/lifecycle callback you
  can rely on for a `WindowGroup` window, so use these notifications.
- **Selection / host takeover (R5).** When a Cmd-drag selection begins, release
  all held keys before it starts swallowing qualifier `flagsChanged`.
- **Caps Lock** is a toggle, not a held key -- handle it via its own
  (rate-limited) sync path, never as a normal down/up.
- **Auto-repeat (R7):** drop events where `event.isARepeat`.

Reference files: `KeyboardMTKView.swift` (event handling, `modifierKeyIsDown`,
focus/selection release), `KeyboardClient.swift` (held-key set, `releaseAllKeys`,
serialised send, synthetic modifiers), `BBCModifierState.swift` (the pure derive
functions).


## 5. Checklist for a new front-end

- [ ] Resolve each host key to a BBC key at press time and freeze its
      shift/ctrl facts (R1, section 1).
- [ ] Maintain a held-key set keyed by physical identity; derive BBC SHIFT/CTRL
      from it (R1).
- [ ] Decide each modifier's down/up from the event's absolute, per-physical-key
      state -- never a coalesced flag, never a toggled boolean (R2, R3).
- [ ] Release *all* held keys on focus loss; reset tracking; require re-press on
      return (R4, R6).
- [ ] Release all held keys when a host interaction (shortcut, selection,
      overlay) takes over input -- do not selectively suppress edges (R5).
- [ ] Drop auto-repeat (R7); guard ups by the held-set (R8); serialise sends in
      generation order (section 3).
- [ ] Resolve by the unmodified character (or key code) while CTRL is held, and
      never let a character suppress CTRL (R9).
- [ ] Treat Caps Lock as a toggle with its own sync, not a key.
- [ ] Deliver correct key state at reset and let the ROM decide booting
      (section 3); do not special-case SHIFT/CTRL-Break in the front-end.


## 6. Testing

Make the decisions *pure* so they can be unit-tested exhaustively, and cover the
integration behaviours manually or with the emulator in the loop:

- **Pure, unit-tested:** the per-key modifier-down decision
  (`ModifierKeyStateTests` over `modifierKeyIsDown`, including "release one SHIFT
  while the other is held"), and the derive functions
  (`BBCModifierStateTests` over `desiredShift`/`desiredCtrl`).
- **Integration / manual:** hold SHIFT in a game and Cmd-Tab away and back
  (focus-loss release); hold both SHIFT keys and release one, then the other;
  hold SHIFT, start and end a Cmd-drag selection.


## See also

- `docs/keyboard.md` -- the keyboard matrix and input handling in the core.
- `clients/beebium-python-client/tests/test_autoboot.py` -- the verified OS 1.20
  SHIFT/CTRL/link behaviour at reset (SHIFT-Break, CTRL-Break, the auto-boot
  link, and why CTRL suppresses the SHIFT auto-boot).
