# Fonts

Five period BBC fonts, in the plain glyph-set format the CLI accepts.

From J.G.Harston's collection at <https://mdfs.net/Apps/Font/>, in `Fonts1.zip`
to `Fonts4.zip` -- "various BBC fonts", dated 1989 to 1991, held there as
streams of `VDU 23, character, b0 ... b7` definitions. Converted by
`../import_fonts.py`.

## Why these five

Not taste. Sixty-eight of the collection parse as text fonts; each was rendered
to a full screen and read back, and **seventeen did not survive the round
trip**. Every one of those failures was the same thing: characters a font draws
with identical pixels, which no reader can tell apart because the difference is
not in the image.

| Font | Glyphs | What it shows |
|---|---|---|
| `broadway` | 95 | Differs from the MOS font in 94 of 95 glyphs, no two alike. The control: a font this far from the ROM's still reads perfectly. |
| `feltpen` | 94 | `'0'` and `'O'` are one bitmap, and so are `'l'` and `'|'`. Also the only one of the five with no space glyph. |
| `futura` | 95 | `'0'`/`'O'` again, and `'5'`/`'S'` -- a pair nobody would think to look for. |
| `trekfont` | 95 | `'('`/`'['` and `')'`/`']'`. |
| `chocolate1` | 95 | `'I'`, `'l'` and `'|'` are all one bitmap: three characters a cell could equally be, not two. |

The other twelve failing fonts showed the same collisions as these, so they are
not here. The full survey is easy to repeat: the fonts are a download away, and
`import_fonts.py` explains the format.

## What they changed

The library used to pick one of the colliding characters and say nothing --
which is the mistake it was built to avoid, one level up. An unreadable cell
was already distinguished from a blank one; a cell that could equally be `'0'`
or `'O'` was not distinguished from one that could only be `'0'`.

So `Cell::alternatives` now carries the other characters a cell could be, and
`Result::ambiguous_cells` counts them, alongside `unmatched_cells`. The text
stays usable, and a caller is told which characters were a choice rather than a
reading.

The distinction that matters: two *sets* disagreeing is an override, which is
how a `VDU 23` redefinition arrives and is not ambiguous at all. Only a set
colliding with *itself* leaves a cell undecidable.

## Re-importing

```
python3 ../import_fonts.py --archives-dirpath <directory of unzipped fonts>
```

Which fonts are converted, and the note recorded at the top of each, live in
`FONTS` in that script.
