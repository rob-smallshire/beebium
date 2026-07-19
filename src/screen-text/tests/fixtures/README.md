# Test fixtures

Screens from a running machine, committed, so the library is exercised against
genuine output as well as against images it composed itself.

Two sets: `corpus/`, covering every bitmap mode exhaustively, and two captured
screenshots at the top level.

The synthetic tests construct every case exactly and cover far more ground.
These exist for the one thing synthetic images cannot vouch for: that the
geometry and pixel values a real machine produces are what the library expects.

Capturing them needs an emulator, which is why they are committed rather than
generated at test time. Nothing in the library or its build reads a machine.

## Provenance

Both captured from `beebium-model-b` with `acorn-mos_1_20.rom` and
`bbc-basic_2.rom`, via the Python client's `video.capture_frame()`, saved as
PNG.

Frame geometry, identical for both:

| | |
|---|---|
| Image | 320x256, one logical pixel per pixel |
| Display region | lines 0-256, `pixel_width` 320 |
| Field order | progressive |
| Background | black (0), foreground white |
| Cell grid | 8x8 at origin 0,0 -- 40 columns by 32 rows, 1280 cells |

The grid origin needs no adjustment: the captured frame is exactly the display
area, so the default geometry reads it.

### `mode4-listing.png`

`MODE 4` followed by `LIST` of:

```
10 REM SCREEN TEXT
20 FOR I%=1 TO 3
30 PRINT "LINE ";I%
40 NEXT I%
50 END
```

All 1280 cells match a glyph and none is unread, which is the evidence for the
claim the design rests on -- that a character cell matches its font glyph byte
for byte.

The trailing `_` on the last line is the cursor, matched as an underline. It is
part of the screen, so it is part of the text; the library does not know what a
cursor is and should not pretend to.

### `mode4-inverse.png`

`MODE 4` followed by:

```
COLOUR 129:COLOUR 0:PRINT "INVERSE TEXT"
```

`COLOUR 129` swaps the background and `COLOUR 0` the foreground, which is how
the BBC produces inverse text. The printed line is inverse; the echo of the
command that printed it is not, so one screenshot covers both.

This fixture is why character 127 is excluded from the built-in glyph set. The
space inside `INVERSE TEXT` is a solid cell, exactly the complement of a space.
While the ROM's solid block at 127 was in the set, an upright match beat the
inverted one and that space came back as `U+007F`, an invisible control code.
`VDU 127` deletes rather than prints, so those ROM bytes never appear on screen
as a glyph and do not belong in a set that maps bitmaps to text.

## Recapturing

Drive a Model B from the Python client: type the program, `MODE4`, then `LIST`,
and call `bbc.video.capture_frame().save_png(...)`. Two things are easy to get
wrong:

- The machine must be **running**. `run_until_or_timeout` leaves it stopped,
  and keystrokes are only consumed while it runs, so resume it before typing.
- Let the display settle after `LIST` before capturing, or the frame catches a
  partly drawn screen.

If a recaptured image differs, check the geometry above before assuming the
library is at fault: a different border or field order changes the origin.


# corpus/

Every bitmap mode, MODE 0 to MODE 6, in two variants. Imported from the
emulator's own golden masters by `import_golden_corpus.py`; see that script for
why they are copied rather than referenced.

- `modeN-testcard.png` -- every character cell filled with a cycling pattern of
  characters 32 to 126, so a whole screen is checked rather than a line of it.
- `modeN-blue.png` -- the same screen after `VDU 19,0,4,0,0,0` recolours the
  background.

Every cell of all fourteen images is matched, none unread, and the testcard's
text is reconstructed exactly from an expectation computed independently of the
library.

## Geometry

Eight pixels per column in every mode, which is what lets one glyph set serve
all of them. Rows are where they differ:

| Mode | Image | Grid | Cell | Row pitch |
|---|---|---|---|---|
| 0 | 640x256 | 80x32 | 8x8 | 8 |
| 1 | 320x256 | 40x32 | 8x8 | 8 |
| 2 | 160x256 | 20x32 | 8x8 | 8 |
| 3 | 640x250 | 80x25 | 8x8 | **10** |
| 4 | 320x256 | 40x32 | 8x8 | 8 |
| 5 | 160x256 | 20x32 | 8x8 | 8 |
| 6 | 320x250 | 40x25 | 8x8 | **10** |

MODE 3 and MODE 6 are 25-row text modes: an 8-scanline glyph on a 10-scanline
pitch. The two spare scanlines are blanked rather than painted with the
background colour, so they stay black however the palette is programmed. The
blue variants of those two modes are the reason the library separates cell size
from pitch at all -- read with a 10-scanline *cell*, every one of MODE 6's 1000
cells goes unmatched.

## The cursor

The testcard leaves the last cell of the last row unfilled and the golden
masters render the cursor steady, so that cell holds an underline. It appears in
the expected text as an underline, because that is what is on the screen. The
library does not know what a cursor is, and should not.

## Re-importing

```
cd clients/beebium-python-client
uv run --extra imaging python \
    ../../src/screen-text/tests/fixtures/import_golden_corpus.py
```

Add `--check` to see what would be imported without writing anything. If the
emulator's golden masters are regenerated and these are re-imported, expect the
tests to keep passing: a change that broke them would mean the renderer had
stopped producing glyphs that match the font, which is the emulator's bug to
find, not this library's.
