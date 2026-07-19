# Test fixtures

Screenshots captured from a running machine and committed, so the library is
exercised against genuine output as well as against images it composed itself.

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
