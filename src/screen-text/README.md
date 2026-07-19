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
2. The bitmap is looked up in a hash built from the glyph sets.
3. When `match_inverted` is set, the complement is looked up too, and the cell
   records that it matched inverted. The BBC produces inverse text routinely by
   swapping foreground and background, so this is not an edge case.
4. No hit means unmatched.

Precedence is fixed so that the answer never depends on iteration order: later
glyph sets beat earlier ones, and an upright match beats an inverted one.

### Colour is worked out, not declared

Nothing asks the caller which value is background, and there is no way to say.

It is not needed. A cell holds two values, and the two ways of assigning them
are precisely the upright and inverse readings -- both of which are tried
anyway. Whichever is which, the character comes out the same. So a caller with
several foreground colours, or several background colours, or both, passes them
straight in; nothing links one cell's colours to another's.

A cell of three or more values is unmatched. It is not one glyph in one colour
on one background, and deciding which of its values were meant to be the glyph
would be a guess.

What the background *is* still matters for one thing: whether a cell reads as
inverse video. That is not a property of a cell -- both readings are glyphs --
but of how the cell sits against the screen around it, so it is measured. Each
cell votes for the value covering more of it, and the winner is the screen's
background.

Cells vote, not pixels. Counting pixels lets a few solid cells outweigh a
screenful of text, since a solid cell contributes all its pixels and a letter
only its strokes; one cell of reverse video in four was enough to invert the
answer while this was being built. It follows that a screen made *entirely* of
reverse video has none: with nothing to be reverse of, dark letters on a light
ground is simply what it is.

### Never guess

A cell that matches nothing is unmatched. It contributes a space to `Run::text`
so that columns stay where they were on screen, but the `Cell` records that it
was unread.

B-Em's `textsave.c` silently turns unmatched cells into spaces, which makes "I
could not read this" indistinguishable from "this was blank". That distinction
is what the whole design rests on, so it is tested directly rather than assumed.

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
deliberately left out. `VDU 127` deletes a character rather than printing one,
so those bytes never appear on screen as a glyph. Including them would also
break the common case: a solid block is exactly the complement of a space, and
an upright match beats an inverted one, so every inverse space -- most of an
inverse-video line -- would come back as an invisible control code. That was
observed on a real screenshot, not reasoned about in the abstract; see
`tests/fixtures/README.md`.

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

## Not implemented

`Search::IncludeOffset` and `Cell::offset` exist and are accepted, so callers
can be written against the finished interface, but sub-cell offset search is not
implemented. Reading is aligned either way, and no cell is ever reported as
having matched at an offset.

That search is what finds `VDU 5` text, which is written at the graphics cursor
rather than on the character grid. ZEsarUX brute-forces all 64 sub-cell
positions, which works and costs roughly what it sounds like, which is why it is
opt-in rather than default.

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
