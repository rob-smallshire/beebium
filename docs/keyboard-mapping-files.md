# Keyboard Mapping Files

The file format every Beebium client reads and writes for keyboard mappings:
which host key produces which BBC Micro key. One file serves every client, on
every host operating system, so a mapping made on a Mac works on a Raspberry
Pi and the other way round. This is the contract behind section 7.4 of
`docs/client-specification.md`; the macOS client's migration to it is #231.

Status: normative, version 2 of the format. Version 1 is the macOS client's
present form, described at the end so that clients can read it and convert.

## 1. What a mapping is

A mapping is a named table from **physical host keys**, possibly qualified by
host modifiers, to **BBC keys**, possibly with the BBC SHIFT or CTRL forced
down. Two switches shape how a client uses the table:

- **Character mapping.** When on, a host key the table does not name is
  resolved by the character the host keyboard layout gives it, through the
  server's character-to-key catalogue (`KeyboardService.GetAllKeyMappings`),
  so that typing `*` on any layout produces the BBC's SHIFT+`:`. When off,
  unnamed keys do nothing. Game mappings turn it off so that stray keys stay
  silent.
- **Caps Lock synchronisation.** Whether the host Caps Lock state is pushed
  into the machine (client specification 7.3).

The table is keyed on **where a key is**, not what it prints: a key is named
by its physical position so the file is layout-independent. The server never
sees mapping files; it receives internal key numbers through
`KeyboardService.KeyDown` and `KeyUp`.

## 2. The file

JSON, UTF-8, one mapping per file, extension `.json`, kept in the client's
mappings directory (platform latitude) and offered to the user by `name`.

```json
{
  "version": 2,
  "id": "6b0d2c58-1f9b-4c2d-9a1e-2f6f5a1d0c11",
  "name": "Arcadians",
  "characterMapping": false,
  "synchronizeCapsLock": false,
  "keys": [
    { "code": "KeyZ",      "bbc": { "key": "Caps Lock", "action": "Left" } },
    { "code": "KeyX",      "bbc": { "key": "Ctrl",      "action": "Right" } },
    { "code": "Enter",     "bbc": { "key": "Return",    "action": "Fire" } },
    { "code": "Digit8", "with": ["Shift"],
                           "bbc": { "key": ":", "shift": true } },
    { "code": "F11",       "bbc": { "key": "Break" } },
    { "platform": "macOS", "keyCode": 179,
                           "bbc": { "key": "f0" } }
  ]
}
```

| Field | Required | Meaning |
|-------|----------|---------|
| `version` | yes | `2` for this format. A client refuses a higher version it does not know; it reads version 1 as in section 7. |
| `id` | yes | A UUID, stable for the life of the mapping; the client's settings refer to a mapping by `id`, not by `name` or file name. |
| `name` | yes | Shown to the user. Need not be unique, but a client may warn. |
| `characterMapping` | no, default `true` | Section 1. |
| `synchronizeCapsLock` | no, default `true` | Section 1. |
| `keys` | yes | The table, an array of entries in priority order (section 4). |

Unknown top-level fields are preserved when a client rewrites the file.

## 3. An entry

Every entry has a **physical key**, optional **host modifiers**, and a **BBC
side**.

### 3.1 The physical key: `code`

`code` is a W3C UI Events `KeyboardEvent.code` value (the "code values" list:
`KeyA`..`KeyZ`, `Digit0`..`Digit9`, `F1`..`F24`, `Enter`, `Escape`,
`Backspace`, `Tab`, `Space`, `Minus`, `Equal`, `BracketLeft`, `BracketRight`,
`Backslash`, `Semicolon`, `Quote`, `Backquote`, `Comma`, `Period`, `Slash`,
`IntlBackslash`, `IntlRo`, `IntlYen`, `CapsLock`, `ShiftLeft`, `ShiftRight`,
`ControlLeft`, `ControlRight`, `AltLeft`, `AltRight`, `MetaLeft`,
`MetaRight`, `ContextMenu`, `ArrowUp`..`ArrowRight`, `Home`, `End`,
`PageUp`, `PageDown`, `Insert`, `Delete`, `Numpad0`..`Numpad9`,
`NumpadAdd`, `NumpadSubtract`, `NumpadMultiply`, `NumpadDivide`,
`NumpadDecimal`, `NumpadEnter`, `NumLock`, `PrintScreen`, `ScrollLock`,
`Pause`, and the rest of that list). It names a position on the keyboard, the
same on every host, which is the point of the format.

Each client owns a translation table from its native physical identifier to
`code`: macOS virtual key codes, Linux evdev scancodes (Qt reports
evdev + 8 as `nativeScanCode` on both xcb and Wayland), Windows scan codes.
The table is needed anyway to interpret native key events; the file never
carries native codes except as in 3.4.

### 3.2 Host modifiers: `with`

`with` is an optional array of host modifiers that must be held for the entry
to apply: `"Shift"`, `"Control"`, `"Alt"`, `"Meta"`. Left and right are not
distinguished. The Caps Lock state is never a modifier here. An entry without
`with` applies regardless of modifiers, at the lowest priority (section 4).

`Meta` is the Command key on macOS and the Super or Windows key elsewhere. A
client whose platform reserves a modifier for the desktop (Meta under most
Linux compositors, Command on macOS) may decline to deliver entries that
require it and must say so when the mapping is loaded.

### 3.3 The BBC side: `bbc`

| Field | Required | Meaning |
|-------|----------|---------|
| `key` | yes | A BBC key by name from the server's catalogue (`GetAllKeyMappings`, field `name`): the letters and digits as printed, `"Return"`, `"Space"`, `"Shift"`, `"Ctrl"`, `"Caps Lock"`, `"Shift Lock"`, `"Tab"`, `"Escape"`, `"Delete"`, `"Copy"`, `"Up"`, `"Down"`, `"Left"`, `"Right"`, `"f0"`..`"f9"`, `"Break"`, and the punctuation keys by their unshifted legend (`":"`, `";"`, `","`, `"."`, `"/"`, `"-"`, `"^"`, `"\\"`, `"@"`, `"["`, `"]"`, `"_"`, and the pound sign written as the JSON escape `"\u00a3"`). Names are compared case-insensitively except that single letters are the key, not the case. |
| `shift` | no, default `false` | Hold the BBC SHIFT down with the key. |
| `ctrl` | no, default `false` | Hold the BBC CTRL down with the key. |
| `disabled` | no | Absent: the entry cannot be switched off. `false` or `true`: the entry can be switched off by the user in the client's keyboard view, and this is its default state. A game mapping uses it for keys the game does not want pressed by accident. |
| `action` | no | Free text shown beside the entry in the client's keyboard view ("Fire", "Left"). No effect. |

`Break` is special: it is the machine's BREAK line, not a matrix key; the
client sends it through the break path (client specification 5.5), as a
press and a release.

### 3.4 Platform-specific keys

A key with no `code` value, which is rare (a few laptop function keys, some
media keys, the Touch Bar's virtual keys), is written with `platform` and the
native identifier in place of `code`:

```json
{ "platform": "macOS",   "keyCode": 179, "bbc": { "key": "f0" } }
{ "platform": "linux",   "scancode": 240, "bbc": { "key": "f0" } }
{ "platform": "windows", "scanCode": 290, "bbc": { "key": "f0" } }
```

The native identifiers: macOS `keyCode` is the virtual key code (kVK_*);
linux `scancode` is the evdev key code (`KEY_*` from linux/input-event-codes.h),
not Qt's `nativeScanCode`, which is evdev + 8 on xcb and Wayland; windows
`scanCode` is the hardware scan code (with the extended flag in bit 8), not
the virtual key.

A client applies the entries for its own platform and preserves the others
untouched when it writes the file. `platform` values: `"macOS"`, `"linux"`,
`"windows"`; unknown values are preserved too. A client must not write a
`platform` entry for a key that has a `code`; a conformance check rejects a
file that does.

### 3.5 Conditional entries: `when`

Any entry may carry a `when` object saying where it applies. Its members are
predicates; the entry applies only where every predicate holds. An entry
without `when` applies everywhere.

```json
{ "code": "F13", "when": { "platform": "macOS" },
                 "bbc": { "key": "Shift Lock", "action": "Shift Lock" } },
{ "code": "F11", "with": ["Alt"], "when": { "platform": ["linux", "windows"] },
                 "bbc": { "key": "Shift Lock", "action": "Shift Lock" } }
```

The predicates:

| Predicate | Value | Holds when |
|-----------|-------|------------|
| `platform` | a platform name, or an array of them: `"macOS"`, `"linux"`, `"windows"` (the names of 3.4) | the client runs on one of the named platforms |
| `keyboard` | a keyboard class, or an array of them: `"standard"`, `"pi-internal"` | a keyboard of one of the named classes is attached to the host |

The keyboard classes:

| Class | Which keyboards |
|-------|-----------------|
| `"pi-internal"` | The keyboard built into a Raspberry Pi 400 or Pi 500: a USB device, vendor `04d9` product `0007`, named "Raspberry Pi Internal Keyboard". It has no F11 or F12 key: F12 is Fn+F2, so a chord on F12 is a chord on Fn+F2, and Ctrl+Alt+F12 reaches the kernel as a virtual-terminal switch before any application sees it. |
| `"standard"` | Any keyboard that is not of a class listed above. On macOS and Windows a standard keyboard is always taken to be attached. |

A client decides which classes are attached when it starts, from the host's
input devices (on Linux, `/proc/bus/input/devices` or `/dev/input/by-id`);
following keyboards that come and go while it runs is platform latitude. A
host with a Pi 400 keyboard and an external keyboard has both classes
attached, so entries for either apply, and the keyboard view shows both.

```json
{ "code": "F12", "with": ["Alt"], "when": { "keyboard": "standard" },
                 "bbc": { "key": "Break", "action": "Break" } },
{ "code": "F12", "when": { "keyboard": "pi-internal" },
                 "bbc": { "key": "Break", "action": "Break" } }
```

This is Break in every built-in: guarded by Alt on a standard keyboard, so a
stray F12 does not reset the machine, and on plain F12 on the Pi keyboard,
where the Fn chord is already the guard and the Alt would put Ctrl+Break on
the virtual-terminal switch. The predicates of this table are the ones every
client implements; the paragraph below is about predicates not yet listed.

A predicate the client does not know (a name this document does not list,
or a value of a shape it does not describe) does not hold. So an entry with
an unknown predicate does not apply, and the client keeps it untouched when
it writes the file, exactly as it keeps other platforms' 3.4 entries. That is
how the format grows: a later predicate (a keyboard layout, say) reaches old
clients as entries they skip, never as a parse error or a wrong key.

Everything a client does with a mapping sees only the entries that apply:
resolution (section 4), the keyboard view's labelled keys (client
specification 7.4), and any editing. The example above is Logical's Shift
Lock: on F13 on macOS, and on Alt+F11 on the platforms whose keyboards
mostly lack F13, so each platform's keyboard view shows one Shift Lock row
and the key its users have.

`when` is a condition on an entry; the `platform` field of a 3.4 entry is
not, it names the namespace of the entry's native identifier. A 3.4 entry
may carry `when` as well, though it rarely needs to.

## 4. Resolution

Given a host key event with its physical `code` and the host modifiers held
at that moment, a client resolves it in this order, and the first rule that
yields a result wins. "Held modifiers" means the modifier keys the client
itself tracks as down when the event arrives, not counting the key of the
event: a press of ShiftLeft is resolved with no Shift held, whatever the
platform's event state says (X11 and Wayland differ on whether a modifier's
own press already carries it). Clients derive the set from their held-key
tracking, not from the event's modifier flags.

1. **Physical entries for this `code`** (and this platform's 3.4 entries for
   this native key), among the entries that apply here (3.5), considering
   only entries whose `with` set is a subset of the held modifiers (an entry
   needing Shift does not match without Shift):
   - an entry whose `with` set equals the held modifiers exactly;
   - else the first entry, in file order, whose `with` set is a proper
     subset of the held modifiers;
   - else the first entry with no `with`.
   If the entry that wins is a `disabled` entry currently switched off, the
   key is **ignored**: resolution stops here and does not fall through to
   character mapping, because the user switched the key off to stop it doing
   anything. A client may give feedback that a switched-off key was pressed
   (the macOS client plays its disabled-key sound); that is latitude.
2. **Character mapping**, if `characterMapping` is on: the character the host
   layout gives this key with the held modifiers, ignoring Control, looked up
   in the server catalogue; the catalogue's `needs_shift` drives the BBC
   SHIFT.
3. Nothing: the key is ignored.

A resolved key is sent with its BBC SHIFT and CTRL as the entry or catalogue
say, through the client's one held-key set and one send queue (client
specification 7.1, 7.2): the derived SHIFT and CTRL are held while any key
that needs them is held, and released when none does. Host SHIFT and CTRL
themselves map, by default, to the BBC SHIFT and CTRL through ordinary
entries (`"code": "ShiftLeft"` to `"key": "Shift"` and so on); the built-in
default mapping carries them.

A key that was resolved on press is released by the same resolution it was
pressed with, whatever the modifiers do in between (the frozen per-press
facts of specification 7.2).

## 5. The built-in mappings

Every client ships, read-only, with the same `id` on every platform so that a
user's stored choice carries across clients:

| `id` (last group) | Name | What it is |
|-------------------|------|------------|
| `...0001` | Logical | The positional map of a modern keyboard onto the BBC keyboard, character mapping on, Caps Lock synchronisation on. This is the mapping every client treats as the default and active one, so its id must not change. The physical entries cover the keys whose position matters (cursor keys, function keys, Escape, Shift, Ctrl, Delete, Copy on F11, Break on Alt+F12); everything else comes through character mapping so that any host layout types correctly. |
| `...0002` | Thrust | Game mapping |
| `...0003` | Snapper | Game mapping |
| `...0004` | Arcadians | Game mapping |
| `...0005` | Boffin | Game mapping |
| `...0006` | Planetoid | Game mapping |

The ids are UUIDs of the form `00000000-0000-0000-0000-00000000000N`. The
game mappings keep the ids they ship with today, converted to version 2. User
mappings are copies with fresh ids.

Break is on Alt+F12 (Option+F12 on macOS) in every built-in on a standard
keyboard, and on plain F12 (Fn+F2) on the Raspberry Pi 400 and 500 keyboard,
by the two `when.keyboard` entries of 3.5. F11 is the BBC Copy key in the
Logical mapping and is the desktop full-screen key under the KDE conventions
the Linux client follows, so it is not Break anywhere.

Shift Lock is on F13 on macOS and on Alt+F11 on Linux and Windows (3.5),
whose keyboards mostly have no F13; Alt+F11 is beside Break and is not a
desktop binding under labwc, KDE or GNOME. One entry per platform, so each
keyboard view shows Shift Lock once.

Logical carries an `action` label (3.3) on the BBC keys that have no host
namesake - f0, Break, Copy, Shift Lock and Delete - so a client's keyboard view
names the host key that reaches each even for this general mapping; the game
mappings label their in-game functions. A client's keyboard view shows these
labels for built-in mappings as well as user ones (client specification 7.4, #234).

## 6. Conformance

- A client reads any version 2 file, applies the entries it can, preserves
  what it cannot (3.4, 3.5, unknown fields) when writing, and reports, not
  silently drops, entries it declined (3.2). An entry whose `when` does not
  hold is not declined; it is simply not for this client.
- A client writes version 2 only. It never writes a native code for a key
  that has a `code` value.
- A shared corpus under `tests/assets/keyboard-mappings/` (the built-ins and
  a file exercising every rule above, 3.5 included, with the expected
  resolution for a list of host events, each list stating the platform and
  the attached keyboard classes it is resolved under) is the conformance
  test: every client's tests load it and must resolve identically. The Linux client's `KeyboardInput` tests and
  the macOS `KeyboardMapping` tests both run it.

## 7. Version 1 (the macOS client's present format) and conversion

Version 1 files have `"version": 1`, `keyMappings` instead of `keys`, and
entries keyed on native codes in per-platform objects:

```json
{ "macOS": { "keyCode": 36, "shift": false, "control": false,
             "option": false, "command": false, "function": false },
  "windows": { "VKCode": 13 },
  "bbc": { "keyName": "Return", "shift": false, "ctrl": false,
           "disabled": false, "action": "Fire" } }
```

Conversion, done once by the macOS client when it first loads a version 1
file, and offered by any client that finds one:

- `macOS.keyCode` through the macOS virtual-key-to-`code` table becomes
  `code`; `shift`/`control`/`option`/`command`/`function` true become
  `with` (`Shift`, `Control`, `Alt`, `Meta`; `function` has no neutral
  modifier and keeps the entry as a `platform: macOS` entry with the
  original `keyCode` and a `"function": true` field).
- `windows.VKCode` is a virtual key, not a scan code, so it is not a
  position; it is dropped when the `code` is known from the macOS key, and
  kept as a `platform: windows` entry with `VKCode` otherwise.
- `bbc.keyName` becomes `bbc.key`; `shift`, `ctrl`, `disabled`, `action`
  are unchanged.
- The file is rewritten as version 2 beside the original; the original is
  kept until the user deletes it.

Keys that the macOS table cannot name (the Touch Bar's virtual keys, the
`function` modifier) stay platform entries; everything else becomes
portable.
