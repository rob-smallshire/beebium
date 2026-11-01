# Keyboard mapping conformance corpus

The shared test corpus of `docs/keyboard-mapping-files.md` (section 6). It is
client-neutral: every Beebium client's tests load these files and must resolve
them identically. Nothing here names a Linux evdev code or any other native
code, except where a case exercises a platform entry (section 3.4).

| File | What it is |
|------|------------|
| `rules.json` | Every resolution rule of section 4, the key-name rules of 3.3 and the platform entries of 3.4, as mappings plus host events with the expected BBC key, SHIFT, CTRL, Break, or "ignored". It carries the slice of the server's key catalogue its cases use. |
| `Arcadians.json`, `Boffin.json`, `Planetoid.json`, `Snapper.json`, `Thrust.json` | The built-in game mappings in version 2, converted from the macOS client's version 1 files (`clients/macos/Beebium/Beebium/Resources/KeyboardMappings/`) by the rules of section 7, with their present ids. |
| `macos-keycodes.json` | macOS virtual key codes (`NSEvent.keyCode`) to W3C `code` values, the table section 7's conversion uses. Keys with no `code` value (the Fn key, Touch Bar keys) are absent on purpose: they stay platform entries. |

The Default and Logical built-ins of section 5 are still to come.
