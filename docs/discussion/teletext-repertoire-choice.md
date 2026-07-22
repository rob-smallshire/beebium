# Choosing How MODE 7 Bytes Are Read

**Status: built**, from the core through the wire to the Edit menu, and tested
at each level. The menu itself has not been looked at by a human. This document
is the design and what it became.

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

## Built

**Core** (commit `880dfab7`)

- `TeletextCharacters { Codes, Displayed }` in `TeletextText.hpp`.
- `teletext_alpha_codepoint(character, characters)`, defaulting to `Codes`.
- `teletext_text(...)` takes it and threads it through `convert`.
- `read_band(..., characters = Codes)` and the teletext strategy in
  `ScreenText.cpp` thread it. Defaulted so the eighteen existing test call
  sites compile unchanged.
- Tests in `tests/test_teletext_text.cpp`: both repertoires, that **only** the
  eleven divergent codes differ between them, and that a BASIC listing with an
  assembler block and a `^` survives the round trip.

**The wire**

- `ScreenTextCharacters` in `video.proto` (`..._CODES = 0`,
  `..._DISPLAYED = 1`) and `characters` on `GetScreenTextRequest`, beside
  `search` and `layout`.
- `to_screen_characters` in `VideoService.cpp`, mirroring `to_screen_search`,
  passed to `read_band`.
- Fingerprint synced, all four servers rebuilt, Python, TypeScript and Swift
  stubs regenerated.

**Clients**

- Python `screen_text(characters="codes"|"displayed")`, TypeScript
  `screenText({characters})`, both refusing a repertoire they do not have
  rather than silently falling back.
- Swift `ScreenTextCharactersMode` with the proto mapping, threaded through the
  `ScreenTextService` seam and `VideoClient`.
- Integration tests in Python and TypeScript that print `[]^~` in MODE 7 and
  read it back both ways, including that asking for `codes` explicitly matches
  the default.

**macOS**

- `Copy Teletext As` submenu in the Edit group, two items with checkmarks,
  sitting alongside the three copy commands rather than multiplying with them.
- `SelectionCoordinator.teletextCharacters`, applied to every copy the window
  makes, starting from the remembered value
  (`ScreenTextCharactersMode.remembered(in:)`, key
  `screenTextTeletextCharacters`) and written back by the menu.
- Coordinator tests: the default is Codes, the choice reaches all three copy
  paths, and an unrecognised remembered value is declined rather than trusted.

## `GetTeletextScreen` follows the same default -- decided

It shares `teletext_text()`, so it now reports codes too. Left that way
deliberately: the two APIs reading one capture differently would be worse than
either reading, and the integration test that asserts `GetScreenText` and
`GetTeletextScreen` agree about a MODE 7 screen now pins that. It is scaffolding
due for retirement, so it does not get a knob of its own.

## Left to do

Eyeball the menu. Everything else in the list this document used to carry is
built and tested.

## Not part of this

Sixels and control codes both copy as spaces and stay that way. Under a
Displayed reading sixels could be Symbols for Legacy Computing, but that is a
second axis and wants its own submenu group if ever wanted at all.
