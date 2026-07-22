# Choosing How MODE 7 Bytes Are Read

**Status: half built.** The core is done and committed (`880dfab7`); the wire,
the clients and the macOS menu are not. This document is the design and the
remaining work, written so it can be picked up cold.

## The problem

The SAA5050 draws eleven codes as characters ASCII puts elsewhere:

| Byte | ASCII | SAA5050 draws |
|---|---|---|
| `0x23` | `#` | `£` |
| `0x5B` | `[` | `←` |
| `0x5C` | `\` | `½` |
| `0x5D` | `]` | `→` |
| `0x5E` | `^` | `↑` |
| `0x5F` | `_` | `#` |
| `0x60` | `` ` `` | `―` |
| `0x7B` | `{` | `¼` |
| `0x7C` | `\|` | `‖` |
| `0x7D` | `}` | `¾` |
| `0x7E` | `~` | `÷` |

So a MODE 7 screen can be read two ways, and **only the person reading it knows
which they meant** -- is this a picture of text, or a listing of code?

Copying used to report the glyphs unconditionally. That silently corrupts the
commonest thing anyone copies off a BBC screen: MODE 7 is the default screen
mode, and in BBC BASIC `[` and `]` delimit assembler blocks while `^` is
exponentiation. A copied listing came back with arrows in it -- confident wrong
text, the failure mode this project refuses everywhere else.

## The decisions

**The transformation is server-side; the choice is the client's.** The mapping
is knowledge about the SAA5050's repertoire, identical for every client -- and
we have direct evidence of what happens otherwise, in the two screen scrapers
that implemented the same idea twice at two levels of correctness. The
*interpretation* cannot be inferred from anything the machine knows, so it
rides on the request beside `search` and `layout`.

**The default is Codes** -- the byte at face value. Wrong only for someone
capturing teletext as it looked, who can say so; right for the listing case,
which is both commoner and worse to get wrong.

**The control belongs in the Edit menu, not a settings pane.** A `Copy Teletext
As` submenu of two radio items, orthogonal to the three copy commands rather
than multiplying with them.

**Copy only.** Every codepoint the Displayed reading produces lies outside
ASCII, so the paste substitutions in `TextTranslation.cpp` already map `←` and
`[` onto the same `&5B`. The two readings converge on the way back in, so paste
needs no equivalent choice -- which is tidier than macOS manages with its own
Substitutions menu, where whether a setting affects copy or paste is unclear.

## Done (commit `880dfab7`)

- `TeletextCharacters { Codes, Displayed }` in `TeletextText.hpp`.
- `teletext_alpha_codepoint(character, characters)`, defaulting to `Codes`.
- `teletext_text(...)` takes it and threads it through `convert`.
- `read_band(..., characters = Codes)` and the teletext strategy in
  `ScreenText.cpp` thread it. Defaulted so the eighteen existing test call
  sites compile unchanged.
- Tests in `tests/test_teletext_text.cpp`: both repertoires, that **only** the
  eleven divergent codes differ between them, and that a BASIC listing with an
  assembler block and a `^` survives the round trip.

## Remaining

1. **Proto** (`video.proto`): a `ScreenTextCharacters` enum
   (`SCREEN_TEXT_CHARACTERS_CODES = 0`, `..._DISPLAYED = 1`) and a field on
   `GetScreenTextRequest` beside `search` and `layout`. Check the name against
   the core types first -- proto types generate into namespace `beebium`
   alongside them, which has bitten twice.
2. **`VideoService.cpp`**: map the request field to `screen::TeletextCharacters`
   and pass it to `read_band`, the way `to_screen_search` does.
3. **The proto-change dance**, which is where time is lost if skipped:
   `sync_protocol_fingerprint.py`, then `cmake --build build --target
   beebium-servers` (**all four**), then regenerate the Python, TypeScript and
   Swift stubs, then rebuild the macOS app.
4. **Clients**: a parameter on Python `screen_text()`, TypeScript
   `screenText()`, and the Swift wrapper, in the shape of the existing
   `search`/`layout` options. Tests in each.
5. **macOS**: the `Copy Teletext As` submenu, its persistence (follow the
   `VideoSettings` precedent -- per window, per machine cache, global default),
   and `SelectionCoordinator` passing the choice into `GetScreenText`. Tests for
   the coordinator; the menu itself needs eyeball verification.
6. **Decide what `GetTeletextScreen` does.** It shares `teletext_text()` and so
   now defaults to Codes, which is a silent behaviour change to a still-shipped
   RPC. It is scaffolding due for retirement once nothing uses it, so the
   cheapest honest answer is probably to leave it on the new default and say so
   -- but it is a decision, not an oversight.

## Not part of this

Sixels and control codes both copy as spaces and stay that way. Under a
Displayed reading sixels could be Symbols for Legacy Computing, but that is a
second axis and wants its own submenu group if ever wanted at all.
