# screen-text

Turns images into text, given sets of glyphs.

Given a picture and some glyphs, it says which characters it can see and where.
It knows nothing about BBC Micros beyond a built-in Acorn glyph set that callers
may extend or replace, and nothing at all about emulators, CRTCs or screen
modes.

## The one-way dependency

This component depends on nothing else in Beebium. Beebium may use it; it never
uses Beebium. That is what keeps it extractable as a separate project, which is
the point of building it separately rather than inside the emulator.

Shared third-party code is fine -- the CLI uses the vendored `stb_image` -- but
Beebium's own code is not. If this library appears to need something from the
emulator core, the interface is wrong, and that is worth raising rather than
reaching across.

The library itself links nothing at all; only the CLI has a dependency, because
only the CLI reads files.

## Building

```
cmake --build build --target screentext      # the static library
cmake --build build --target screentext-cli  # the CLI
```

## Using it

```cpp
#include "screentext/ScreenText.hpp"

screentext::Image image = /* one byte per pixel, row-major */;

screentext::Band band;
band.bottom = image.height;
band.cell_width = 8;
band.cell_height = 8;

const screentext::Result result = screentext::read(
    image, {band}, {screentext::builtin_glyph_set("acorn-mos-1.20")});

for (const screentext::Run& run : result.runs) {
    // run.text, run.bounds, run.cells
}
```

The CLI covers the same ground without linking:

```
screentext read screenshot.png
screentext read screenshot.png --selection 64,32,320,64 --format json
screentext read screenshot.png --glyphs redefined.glyphs
```

`--format text` prints the text and nothing else, so it composes in a pipeline.
`--format json` emits runs, cells, positions and unmatched counts, for a caller
that needs the uncertainty. Exit status is zero whenever the image was read,
whatever was found: finding no text is not a failure.

## How matching works

A character cell on a BBC screen matches its font glyph byte for byte. That was
verified against a running machine before any of this was written, and it is why
there is no approximate comparison anywhere in the design.

1. Each cell is reduced to one bit per pixel. A cell drawn by the VDU drivers
   holds exactly two values -- the glyph's colour and its background -- so the
   pair is recovered from the cell itself. This is what removes colour from the
   problem, so the same glyph in any colour is one bitmap.
2. Both ways round are looked up in a hash built from the glyph sets: once
   taking one colour as the background, once taking the other.
3. Whichever matched says which colour the glyph was drawn in.
4. No hit either way means unmatched.

Precedence is fixed so that the answer never depends on iteration order: later
glyph sets beat earlier ones. Where *both* readings are glyphs -- which needs a
glyph set holding some glyph's complement, and no built-in set does -- the
reading whose background covers more of the cell is taken, the lower pixel
value breaking a tie.

### Colour is worked out, not declared, and there is no "inverted"

Nothing asks the caller which value is background, and there is no way to say.

A cell holds two values and a shape. Both ways of assigning them are tried, so
the character is recovered whichever way round it was drawn, and *which way
matched* says which colour the glyph is in. That is reported as it stands:

```cpp
cell.foreground  // the value the glyph is drawn in
cell.background  // the value around it
```

There is deliberately no `inverted` flag. Neither arrangement of a cell's two
colours is the real one that the other is a reversal of; calling one inverse
asserts a canonical orientation that does not exist. It is also strictly less
information than the two values, and it forced a guess -- deciding what counted
as inverse meant inferring one background for the whole screen, which is
meaningless on a screen where every cell has its own.

So a caller with several foreground colours, several background colours, or
both, passes them straight in; nothing links one cell's colours to another's. A
caller that does want to know which cells stand out from their neighbours
compares backgrounds itself, with the whole picture in view. That is a question
about a screen, and a cell cannot answer it.

A cell of three or more values is unmatched. It is not one glyph in one colour
on one background, and deciding which of its values were meant to be the glyph
would be a guess.

### Never guess

A cell that matches nothing is unmatched. It contributes a space to `Run::text`
so that columns stay where they were on screen, but the `Cell` records that it
was unread.

B-Em's `textsave.c` silently turns unmatched cells into spaces, which makes "I
could not read this" indistinguishable from "this was blank". That distinction
is what the whole design rests on, so it is tested directly rather than assumed.

The same applies one level up. Fonts collide: of sixty-eight period BBC fonts
surveyed, seventeen draw two or more characters with identical pixels -- `'0'`
with `'O'`, `'l'` with `'|'`, `'('` with `'['`, even `'5'` with `'S'`, and in
one font `'I'`, `'l'` and `'|'` all at once. Nothing can separate those from an
image, because the difference is not in the image.

So a cell whose bitmap fits more than one character carries the others in
`Cell::alternatives`, counted by `Result::ambiguous_cells`. `codepoint` still
holds one of them -- the lowest, chosen by value so that rearranging a font
file cannot change what a screen says -- and the text stays usable, but a
caller is told which characters were a choice rather than a reading.

Two *sets* disagreeing is a different thing entirely: that is an override, how
a `VDU 23` redefinition arrives, and is not ambiguous. Only a set colliding
with itself leaves a cell undecidable.

### Geometry is an input

Cell sizes, grid pitches, grid origins, band boundaries and the background value
are all supplied by the caller. This is not a BBC library and nothing here
assumes any of them. Glyph bitmaps carry their own dimensions for the same
reason.

**Cell size and pitch are different things.** The cell is the glyph box, and all
that is ever compared; the pitch is the step from one cell to the next. They are
usually equal, and a zero pitch means "the cell size".

They differ when the display leaves a gap. MODE 3 and MODE 6 are 25-row text
modes putting an 8-scanline glyph on a 10-scanline pitch, and the two spare
scanlines are *blanked* rather than painted with the background colour -- they
stay black however the palette is programmed. `VDU 19,0,4,0,0,0` makes this
plain, and famous: the character background turns blue while the gaps between
rows stay black.

So the gap cannot be treated as part of the cell. While the background is also
black, doing so costs nothing and looks correct; the moment it is not, every
cell on the screen stops matching at once. Reading MODE 6 with `cell_height =
10` and a blue background leaves all 1000 cells unread, and with `cell_height =
8, row_pitch = 10` leaves none. Both are in the tests.

Bands exist because a display need not have one geometry throughout: the CRTC
can be reprogrammed mid-frame, so the caller describes each band it found. A
cell not wholly inside its band, the image, and any selection is not read.

## The built-in glyph set

`acorn-mos-1.20` holds characters 32 to 126 of the MOS 1.20 font, generated from
a ROM into committed source by `tools/generate_acorn_glyphs.py`. The library
never reads a ROM at runtime and does not need one to build.

```
python3 tools/generate_acorn_glyphs.py roms/acorn-mos_1_20.rom \
    --name acorn-mos-1.20 --output src/AcornMos120Glyphs.cpp
```

The generated file records which ROM it came from and that ROM's SHA-256. Keep
the script so the same can be done for another MOS version.

Character 96 is a pound sign rather than a grave accent -- the one place the
Acorn set departs from ASCII, verified against the glyph shapes rather than
assumed.

The ROM holds eight more bytes, a solid block, at character 127, and they are
deliberately left out: `VDU 127` deletes a character rather than printing one,
so those bytes never appear on screen as a glyph and do not describe any text.

They were also excluded for a second reason that no longer applies. A solid
block is exactly the complement of a space, and while an upright match beat an
inverted one, every solid cell -- most of a reverse-video line -- came back as
`U+007F`, an invisible control code. Reading both ways round and preferring the
larger background settles that on its own now: a solid cell is a space in
whatever colour it is filled with. The first reason stands regardless.

## Supplied glyph sets

Sets passed to `read` are searched after the built-in ones, so they override
individual characters. That is how `VDU 23` redefinitions would arrive, and how
a caller with an entirely different machine's font uses the same tool.

The CLI reads sets from a plain text file, one glyph per line, so that a caller
generating one does not need a library to do it:

```
# codepoint, then one byte per row
U+0041 0x3C 0x66 0x66 0x7E 0x66 0x66 0x66 0x00
```

Codepoints may be given as `U+00A3`, `0xA3`, `&A3` or decimal.

## Off-grid text

`Search::IncludeOffset` finds `VDU 5` text, written at the graphics cursor at
arbitrary pixel positions rather than on the character grid. It is a separate
search, selected per call, and returns only off-grid runs, every cell flagged
`Cell::offset`; the aligned pass reads grid text and the two partition the
screen. A caller wanting both runs `read` twice and concatenates.

The position is the easy part. `VDU 5` paints no background, so the aligned
rule -- two colours in a cell, tried both ways -- rejects most of it, and the
question becomes "do the pixels of colour c in this window form a glyph,
ignoring everything else", asked once per colour present. A free search then
finds glyph-shaped patterns that are not glyphs, handled by ignoring glyphs
imagery could have drawn (those whose every row and column is one unbroken run)
unless they sit in registration with ones it could not. Drop shadows fall out
for nothing: the text colour forms the glyph, the shadow colour a crescent that
matches nothing.

The design, and its validation against five real games, is in
`docs/discussion/screen-text-offset-search.md`.

## Testing

Fixtures rather than an emulator, which is the main practical reason the library
is separate: a recognition bug is reproducible from an image rather than from a
machine state.

Most tests compose their input by stamping known glyphs at known positions, so
every case is constructed exactly and needs no file. The CLI is exercised
through its own interface, run as a process.

Real screens are committed under `tests/fixtures/`, for the one thing synthetic
images cannot vouch for: that the geometry and pixel values a real machine
produces are what the library expects.

`tests/fixtures/corpus/` covers every bitmap mode, MODE 0 to MODE 6, in two
variants -- a testcard filling every character cell with a cycling pattern of
characters 32 to 126, and the same screen after `VDU 19,0,4,0,0,0` -- plus a
MODE 2 screen carrying a different foreground and background pair in every one
of its cells. Every cell of every one of them is matched, and the text is
reconstructed exactly from an independently computed expectation. See the
README there.

`tests/fixtures/fonts/` holds five period BBC fonts, chosen by rendering
sixty-eight of them to a full screen and keeping the ones that showed something
the others did not. See the README there.

Colour is covered twice over, because the two say different things. The
captured screen proves a real machine produces what is expected of it; a
synthetic one covers a full screen of cells whose colours are drawn from the
whole range of byte values rather than any machine's palette, which no capture
could reach.

```
cmake --build build --target test_screentext_read
./build/src/screen-text/tests/test_screentext_read
```

Running the built binaries directly is much faster than going through `ctest`.
