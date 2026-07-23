# Choosing How MODE 7 Bytes Are Read

**Status: built and in use**, from the core through the wire to the Edit menu,
and tested at each level. This document is the design and what it became.

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

**The control belongs in the Edit menu, not a settings pane.** A `Mode 7
Copies As` submenu of two radio items, orthogonal to the three copy commands
rather than multiplying with them.

Two things in that title were arrived at rather than chosen first.

It is not `Copy Mode 7 As`. A title beginning with "Copy" reads as a fourth
copy command sitting among the other three, when what it actually does is say
how those three behave. Naming it for the state rather than the action keeps
the distinction visible in the menu itself.

And it says **Mode 7**, not **Teletext**. Teletext is the broadcast data
service; Mode 7 is the BBC screen mode that borrows its character generator.
The two are not the same thing, and Beebium may one day emulate the Acorn
Teletext Adapter -- a 1 MHz bus peripheral with a TV tuner, which is teletext
in the real sense. A menu that had already spent the word on a screen-mode
concern would leave that peripheral nowhere to stand. Mode 7 is also simply
the more accurate name for what this governs: the screen mode, whoever put the
characters on it.

That distinction outgrew this document: the codebase says "teletext" in a good
many places where it means Mode 7, and the rule for telling them apart -- plus
what is misnamed today and when to fix it -- is in
[the video subsystem's naming section](../video-subsystem.md#naming-teletext-and-mode-7).

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

- `Teletext Copies As` submenu in the Edit group, two items with checkmarks,
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
`GetTeletextScreen` agree about a MODE 7 screen now pins that. It does not get a
knob of its own -- its text field is a convenience beside its real purpose,
which (as of the 2026-07 decision to keep it) is the per-cell attributes, not
the text. A caller wanting Displayed text asks `GetScreenText`.

## What the menu cost to get right

Two things worth remembering, because neither was visible from the code that
looked wrong.

**A menu item whose title changes is a different item.** The first version
built the two choices from Buttons and put the checkmark into the title. Every
toggle therefore replaced both items rather than updating them. A `Picker` is
the construct for a choice of one: stable titles, and AppKit draws the radio
checkmark itself.

**The submenu would not open, and the reason was not in the menu.**
`VideoClient` published six properties from `handleFrame` -- four lines above
its own comment explaining that the renderer is updated directly to bypass
SwiftUI. Frames arrive fifty times a second, so `objectWillChange` fired at
that rate and invalidated every view observing the client: `ContentView`, and
through it the whole window tree and every focused value it exports. The Edit
menu was rebuilt fifty times a second. A flat menu item survives that; a
submenu shimmers and never opens. Nothing read any of the six properties, and
`currentFrame` was retaining a copy of every framebuffer for no reader at all.
Fixed at source in `f713b1ce` -- per-frame state is no longer published.

## Sixels, which turned out to belong after all

This document originally put mosaics out of scope: they copied as spaces and
were to stay that way, with Symbols for Legacy Computing noted as a possible
second axis. That was wrong, and the menu's own wording is what showed it.
`Displayed Characters` promises what the screen showed; a screen of mosaics
that copies as blank space does not deliver it.

Unicode's **block sextants** (U+1FB00) are the same 2x3 pattern the SAA5050
draws, so this is an exact mapping and not an approximation -- the same
character, not a picture of one. Four patterns are not sextants because Unicode
already had them: blank, both half blocks, and the full block.

Contiguous and separated graphics map alike. Separation is how the chip draws a
mosaic, not a different character, in the way that italics are not different
letters.

Under **Codes** mosaics remain spaces, holding their columns: there the byte is
a graphics code and not text.

Two things fell out of doing it, both duplication that had gone unnoticed:

- **The per-cell rule existed twice** -- in `teletext_text()` and in the
  screen-text band reader -- kept in step by a comment saying so. Changing one
  silently broke the other, which is what the comment was for and could not do.
  Both now call `teletext_cell_codepoint()`.
- **`append_utf8` existed twice**, and the second copy stopped at three bytes.
  Invisible while nothing emitted a codepoint above U+FFFF, and silently wrong
  the moment something did. One copy now, in `Utf8.hpp`.

Control codes still copy as spaces, which is right: a control code displays
nothing. Except under hold graphics, where it displays the held mosaic -- and
the capture already resolves that, recording the held character and the
graphics charset, so it needs no special case here.
