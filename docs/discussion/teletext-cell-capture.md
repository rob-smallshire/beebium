# Teletext Cell Capture: One Data Source, Two Transports

## What this is

MODE 7 is a character display. Every cell on screen is a 7-bit character code
plus resolved attribute state -- colours, character set, double height,
conceal, flash, hold graphics. All of that exists *before* the SAA5050 turns it
into pixels, and it is thrown away in the process.

Capturing it serves two features that are otherwise unrelated:

- **Copy as text.** The user selects part of the screen and copies it as
  characters.
- **Vector MODE 7 rendering.** The client redraws the display with a vector
  font at arbitrary resolution (`vector-mode-7.md`).

This document describes the shared capture layer and the copy-as-text feature.
The rendering proposal builds on the same capture and is documented separately.

**Neither feature depends on the other.** Copy as text needs no Metal work, no
font, no `DisplayStyle` change, and no client rendering path. Rendering was
described first, but it is the larger and more optional of the two.

## Why this beats the scrapers we already have

Screen text is currently reconstructed by reading screen memory and undoing the
hardware scroll:

- `clients/beebium-python-client/src/beebium/client/screen.py` -- ~300 lines,
  and the mature version: circular-buffer scroll correction from the CRTC start
  address, teletext control-code blanking, and pluggable row joiners including
  a `dewrapped` mode.
- `clients/beebium-typescript-client/src/screen.ts` -- reads linearly from
  `0x7C00` with no scroll correction, so it returns a rotated grid once the
  screen has scrolled. Same idea, quietly wrong.

A cell grid captured inside the SAA5050 is strictly better than either:

- **No scroll correction needed.** Capture happens as the CRTC walks the
  display, so cells arrive in display order by construction.
- **No control-code inference.** The attribute state is resolved, not guessed
  at by re-simulating the control codes from raw bytes.
- **Charset is known**, so a mosaic cell is distinguishable from an alpha one
  rather than inferred.
- **Held graphics are correct** without reimplementing the hold rules.

Adding a Swift scraper would make a fourth implementation of this, at a fourth
level of correctness. Capturing once in the core retires the divergence.

## The data source

As described in `vector-mode-7.md`: intercept in `Saa5050::byte()` (`Saa5050.hpp:243`),
where all attribute state is already resolved, and fill a double-buffered
40x25 `TeletextGrid` that swaps at VSYNC alongside the pixel framebuffer.

```cpp
struct TeletextCell {
    uint8_t character;         // 7-bit SAA5050 character code
    uint8_t fg : 3;
    uint8_t bg : 3;
    uint8_t charset : 2;       // Alpha, ContiguousGraphics, SeparatedGraphics
    bool double_height_top;
    bool double_height_bottom;
    bool concealed;
    bool flashing;
    bool cursor;
    bool is_control_code;
};
```

The capture is guarded on `m_raster == 0` so each cell is written once per
character row rather than twice per scanline, and the whole thing is behind a
null pointer check so the cost when nothing is watching is one test per
character.

Note the one addition the original proposal identifies: held graphics are
currently kept as pixel data (`m_last_graphics_data`), so the SAA5050 needs to
retain the *character code* of the last graphics character too. One byte of
state, needed by both features.

## Two transports, one source

This is the point of separating the features. They want the data at different
times and in different quantities, and there is no reason to force one shape.

### Snapshot, for copy

Copy needs **one grid, on demand**, when the user invokes the command. A
request/response RPC fits exactly:

```protobuf
rpc GetTeletextScreen(GetTeletextScreenRequest) returns (TeletextScreen);
```

Pushing 8 KB per frame at 50 Hz to every connected client on the chance that
someone might copy would be absurd. A client that wants to copy should also not
have to subscribe to a video stream to do it -- an automation script reading the
screen has no interest in frames at all.

### Per-frame, for rendering

Rendering needs **every frame, aligned with the pixels it replaces**. That is
the case `vector-mode-7.md` argues for putting an optional `teletext_data`
field on the existing `Frame` message: alignment becomes implicit because both
arrive in the same proto with the same frame number, and the per-window stream
count stays at one.

### Why both is not duplication

The grid is captured once. Two transports read the same double-buffered
structure -- one when asked, one on every swap. Neither knows about the other,
and either can exist without the other. If rendering is never built, the
snapshot RPC stands alone; if it is, nothing about the snapshot changes.

## Copy semantics

The decisions copy has to make and rendering does not.

### What does a graphics cell copy as?

Rendering maps mosaic cells to the Unicode Symbols for Legacy Computing block
(U+1FB00-U+1FB3B), which is right for reproducing the display. It is the wrong
answer for text going into a bug report, a source file, or a search box.

Proposed: **a graphics cell copies as a space.** A `separated_graphics` cell
likewise. The information is not text and pretending otherwise produces output
that looks like corruption everywhere it is pasted.

An option to copy mosaics as legacy-computing characters could be added later
for the person who genuinely wants a fidelity dump, but it should not be the
default and probably should not exist until someone asks.

### Control codes

Cells holding control codes display as spaces (or as the held graphics
character). They copy as **spaces**, preserving column alignment -- which is the
main reason anyone copies a teletext screen in the first place.

### Concealed text

The characters are present in the grid with `concealed` set. Copying the
concealed text would leak something the display deliberately hides; copying a
space is consistent with what is on screen.

Proposed: **copy what is displayed**, so concealed cells copy as spaces. If a
"reveal" toggle is ever added (`vector-mode-7.md` notes it falls out of the
grid for free), copy should follow whatever reveal state is active, so what you
copy is always what you can see.

### Double height

A double-height character occupies two rows, with the same character code
recorded on both as top and bottom halves. Copying both rows would double every
line. Proposed: **the top half copies as the character, the bottom half as a
space.**

### Trailing spaces and line joining

`screen.py` already solved this and the answers should carry over rather than
be reinvented: strip trailing spaces per row, and offer the choice between
joining rows as-is and de-wrapping continuation lines. The `linearise` function
and its row joiners (`no_separator`, `spaced`, `lined`, `dewrapped`) are the
prior art.

## Selection

Drag-to-select belongs to copy, not to rendering, and the grid gives it the
natural coordinate system: **selection in cell space, not pixel space**,
snapping to character cells.

The rectangular-versus-row-range choice is then simply how a cell rectangle is
linearised -- rectangular takes columns `[c0, c1]` from each row, row-range runs
from `(r0, c0)` to `(r1, c1)` following the text. Both operate on the same
selection rectangle; only the traversal differs.

Note this is an interaction layer sitting *above* the Display Style system, not
a style. `display-styles.md` warns against a Cartesian product of options; a
selection overlay composes with any style and should not become one.

## What this does not cover

Modes 0-6 have no character grid -- text there is bitmap pixels, and recovering
characters means matching glyphs against the MOS font. That is a genuinely
separate problem with no prior art in any BBC emulator surveyed (B2, B-Em and
BeebEm all copy text only by capturing OS output or the printer stream, never
by reading the screen). It shares nothing with this design except the
clipboard.

## Suggested order

1. `TeletextGrid` plus the `Saa5050::byte()` capture and the held-character-code
   addition. Testable in the core with no client involvement.
2. `GetTeletextScreen` snapshot RPC, and the copy semantics above.
3. Copy as text in the macOS client, whole screen only.
4. Drag-to-select in cell space, with the rectangular/row-range option.
5. Retire the Python and TypeScript scrapers in favour of the RPC, fixing the
   TypeScript scroll bug by deletion.

Vector rendering can be started at any point after step 1, or never.
