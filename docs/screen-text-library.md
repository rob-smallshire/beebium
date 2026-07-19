# Screen Text Library

Turns images into text, given sets of glyphs.

`src/screen-text/` builds a static library and a CLI over one interface. Given
a picture and some glyphs it says which characters it can see, where they are,
what colours they were drawn in, and -- importantly -- which cells it could not
read.

It is stream 1 of `discussion/screen-text-extraction.md`, specified as a work
order in `discussion/screen-text-library-spec.md`. This document says what
exists and how to use it. For why it is shaped the way it is, see
`src/screen-text/README.md`; for the design of the part not yet built, see
`discussion/screen-text-offset-search.md`.

## Status

The first increment is complete and verified on macOS, Linux and Windows.

| | |
|---|---|
| Library | `screentext` (static) |
| CLI | `screentext` (target `screentext-cli`) |
| Namespace | `screentext`, deliberately not `beebium::screentext` |
| Tests | 107, in four binaries under `src/screen-text/tests/` |
| Dependencies | none for the library; the CLI uses vendored `stb_image` |

The library depends on nothing else in Beebium and must continue not to.
Dependencies point one way: Beebium may use it, never the reverse. That is what
keeps it extractable as a separate project, which is the point of building it
separately from the emulator.

Not built: sub-cell offset search, for text that is not on the character grid.
`Search::IncludeOffset` and `Cell::offset` exist and are accepted; nothing sets
or honours them. See the end of this document.

## Building

```
cmake --build build --target screentext      # the library
cmake --build build --target screentext-cli  # the CLI
```

Both are part of the default build, and the tests are registered with CTest,
so CI covers them on all five platforms without any special wiring.

## Using the library

```cpp
#include "screentext/ScreenText.hpp"

screentext::Image image;          // one byte per pixel, row-major
image.width = 320;
image.height = 256;
image.pixels = /* ... */;

screentext::Band band;
band.bottom = image.height;
band.cell_width = 8;
band.cell_height = 8;

const screentext::Result result = screentext::read(
    image, {band}, {screentext::builtin_glyph_set("acorn-mos-1.20")});

for (const screentext::Run& run : result.runs) {
    // run.text is UTF-8; run.bounds is where it was; run.cells is per-cell
}
```

### What you supply

**An image**, reduced to one byte per pixel. What the values mean is not asked
for -- see "Colour" below.

**Bands.** A display need not have one geometry throughout: the CRTC can be
reprogrammed mid-frame. Each band carries its scanline range, the cell size,
the cell-to-cell pitch, and where the grid starts. A cell not wholly inside its
band, the image and any selection is not read.

**Glyph sets**, searched with later sets taking precedence over earlier ones.
That is how a `VDU 23` redefinition arrives: supply it as a later set and it
replaces what the built-in one said.

**Options**: which search, and an optional selection rectangle.

### What you get back

`Result::runs`, one per character row that holds anything worth reporting, with
leading and trailing blanks trimmed and interior spacing preserved. Each `Run`
has UTF-8 `text`, `bounds`, and its `cells`.

Each `Cell` carries its `bounds`, the `codepoint`, the `foreground` and
`background` values it was drawn from, which `glyph_set` matched, and
`alternatives` -- other characters the same bitmap could equally be.

`Result::unmatched_cells` counts what could not be read; `ambiguous_cells`
counts what could not be pinned down. They are different questions and are
answered separately.

## Using the CLI

```
screentext read <image> [options]
```

```
screentext read screenshot.png
screentext read screenshot.png --selection 64,32,320,64 --format json
screentext read screenshot.png --cell 8x8 --pitch 8x10        # MODE 3 or 6
screentext read screenshot.png --glyphs redefined.glyphs
screentext read screenshot.png --origin 4,0
```

`--format text` prints the text and nothing else, so it composes in a pipeline.
`--format json` emits runs, cells, positions, colours, alternatives and counts.
Exit status is zero whenever the image was read, whatever was found: finding no
text is not a failure. Non-zero is reserved for being unable to read the image
or the glyphs.

Supplied glyph sets are plain text, one glyph per line, so that producing one
needs no library:

```
# codepoint, then one byte per row
U+0041 0x3C 0x66 0x66 0x7E 0x66 0x66 0x66 0x00
```

## The three things worth knowing

### Geometry is an input, and pitch is not cell size

Cell sizes, pitches, grid origins and band boundaries are all supplied. Nothing
assumes 8x8.

The cell is the glyph box. The pitch is the step from one cell to the next.
They are usually equal, and a zero pitch means "the cell size" -- but **MODE 3
and MODE 6 put an 8-scanline glyph on a 10-scanline pitch**, and the two spare
scanlines are *blanked*, not painted with the background colour. They stay
black however the palette is programmed.

So `--cell 8x8 --pitch 8x10` for those modes. Getting it wrong costs nothing
while the background is also black and costs everything the moment it is not:
on a real MODE 6 screen with a blue background, treating the gap as part of the
cell leaves every one of its 1000 cells unread.

### Colour is worked out, not declared

Nothing asks which value is the background, and there is no way to say.

A cell holds two values -- the glyph's colour and its background -- and both
ways of assigning them are tried, so the character is recovered whichever way
round it was drawn. Which way matched says which colour the glyph is in, and
that is reported as `foreground` and `background`.

So several foreground colours, several background colours, or both, all work,
and nothing links one cell's colours to another's. Flashing colours are not a
special case either: a flashing colour is a colour.

A cell of three or more values is unmatched. It is not one glyph in one colour
on one background, and deciding which of its values were meant to be the glyph
would be a guess.

There is deliberately no "inverted" flag. Neither arrangement of a cell's two
colours is the real one that the other reverses.

### It never guesses

A cell that matches nothing is unmatched. It contributes a space to the text so
that columns stay where they were on screen, but the `Cell` records that it was
unread. B-Em's `textsave.c` silently turns unmatched cells into spaces, making
"I could not read this" indistinguishable from "this was blank".

The same applies one level up. Of sixty-eight period BBC fonts surveyed,
seventeen draw two or more characters with identical pixels -- `0` with `O`,
`l` with `|`, and in one font `I`, `l` and `|` all at once. Where a font cannot
tell two characters apart, the cell carries the alternatives rather than
quietly picking one.

## The built-in glyph set

`acorn-mos-1.20`: characters 32 to 126 of the MOS 1.20 font, generated from a
ROM into committed source by `src/screen-text/tools/generate_acorn_glyphs.py`.
The library never reads a ROM at runtime and does not need one to build.

Character 96 is a pound sign, not a grave accent. Character 127 is excluded:
`VDU 127` deletes rather than prints, so those ROM bytes never appear on screen
as a glyph.

## Performance

Worst-case latencies, from `screentext-benchmark` (built with
`cmake --build build --target screentext-benchmark`). Each synthetic screen is
filled with matchable text, the most work either path can be given -- a blank
or graphical screen is cheaper. Best of repeated runs, single-threaded.

Two machines: an Apple M1 Max, and a Raspberry Pi 5, the slowest platform
Beebium ships to. A whole screen is read in **around a millisecond** on the
fast machine and **under three** on the slow one.

| Mode | Cells | Aligned, M1 Max | Aligned, Pi 5 |
|---|---|---|---|
| MODE 0 (640x256, 2 colour) | 2560 | 1.0 ms | 2.8 ms |
| MODE 1 (320x256, 4 colour) | 1280 | 0.5 ms | 1.4 ms |
| MODE 2 (160x256, 16 colour) | 640 | 0.25 ms | 0.75 ms |
| MODE 5 (160x256, 4 colour) | 640 | 0.25 ms | 0.75 ms |

MODE 0 is the aligned worst case: eight logical pixels per column throughout, so
the widest mode has the most cells. Colour depth barely matters to the aligned
path -- a cell of three or more colours is rejected early, so a deeper mode is
if anything slightly cheaper. The cost tracks cell count and nothing else.

The offset search is not built. Projected from its dominant cost -- the
candidate gather, sixty-four sub-cell offsets by the colours present in each
window -- it is far heavier, and both axes the client asked about show:

| Screen | Offset, M1 Max | Offset, Pi 5 |
|---|---|---|
| MODE 0 (640x256) | 80 ms | 168 ms |
| MODE 1 (320x256) | 40 ms | 84 ms |
| MODE 2 (160x256) | 22 ms | 47 ms |
| MODE 0, 8-colour noise (ceiling) | 228 ms | 421 ms |

Size dominates -- MODE 0 has four times MODE 2's positions -- and colour depth
multiplies the per-position work, which the noise row pushes to its ceiling: a
screen where almost every window carries the maximum colours, worse than any
real display. Even that stays under half a second on the slow machine.

This is comfortable because the search is opt-in and user-initiated. The design
reaches it only through free-form selection, at most once every few minutes,
never in a loop, so a few hundred milliseconds on the rare copy of unaligned
text is spent where a person is already waiting for a menu. It is also
trivially parallel across offsets, and the gather can stop early once a region
is claimed, neither of which the projection assumes.

## Testing

`src/screen-text/tests/`, four binaries. Run them directly rather than through
`ctest`; it is much faster.

```
cmake --build build --target test_screentext_read
./build/src/screen-text/tests/test_screentext_read
```

Most tests compose their input by stamping known glyphs at known positions, so
every case is exact and needs no file. Beyond that, `tests/fixtures/` holds:

- **`corpus/`** -- every bitmap mode, MODE 0 to MODE 6, in two variants: a
  testcard filling every cell with a cycling pattern of characters 32 to 126,
  and the same screen after `VDU 19,0,4,0,0,0`. Plus a MODE 2 screen with a
  different colour pair in every one of its 640 cells. All read with no cell
  unmatched.
- **`fonts/`** -- five period BBC fonts, chosen by rendering sixty-eight of
  them and keeping the ones that showed something the others did not.
- **`screens/`** -- frames from five real games: Waffle, Loopy Loop, Fruits,
  Rondo and Krazy Ape II. Written by other people, for real machines, with no
  thought for being read back.

Each fixture directory has a README saying what its contents are for and what
they showed.

## What is not built

Sub-cell offset search, for text drawn at the graphics cursor by `VDU 5`. The
design is written up and validated against real screens in
`discussion/screen-text-offset-search.md`, but no code implements it.

In short: the position is the easy part. `VDU 5` paints no background, so the
question has to change from "does this cell reduce to a glyph" to "do the
pixels of colour *c* in this window form a glyph, ignoring everything else".
A free search then finds glyph-shaped patterns that are not glyphs, handled by
ignoring glyphs that imagery could have drawn -- those whose every row and
column is one unbroken run -- unless they sit in registration with ones it
could not.

A prototype of that design finds all the off-grid text on Fruits, Rondo and
Krazy Ape II, including drop-shadowed titles, with no false positives on any
screen in the corpus.

The acceptance target is already written down: tests over `fruits-machine.png`,
`rondo-title.png` and `krazy-game.png` assert that the off-grid labels are
*absent* from the current output. When offset search lands, those expectations
invert.
