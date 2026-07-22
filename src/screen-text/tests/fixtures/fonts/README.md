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

## thrust.glyphs -- a game's own font

`thrust.glyphs` is different in kind from the five above: not a period BBC font
installed by `VDU 23`, but the bespoke five-pixel typeface Thrust (Superior
Software, 1986) blits from its own glyph table. It exists to demonstrate the
supplied-glyph-set path on a real custom-font game: against the ROM set the
high-score screen (`../screens/thrust-hiscore.png`) reads as spaces, because
none of its glyphs is the ROM font; against this set it reads in full. It
covers only the letters, digits and stop that screen uses. See its header for
how it was transcribed, and `../capture_thrust.py` for the capture.

## repton3.glyphs -- a soft font, read from RAM

Where `thrust.glyphs` was transcribed from the screen because the font lived in
a private table, `repton3.glyphs` was read straight from the MOS soft-font area
&C00: Repton 3's menu redefines characters 224-249 as a chunky A-Z, a proper
VDU 23 font, and prints a capital as letter+159. For *this game's menu* the
mapping is therefore exact and needs no guessing -- code 224+n is the nth
letter. It reads the menu of `../screens/repton3-select.png`. See its header,
and `../capture_repton3.py`.

### No offset convention may be assumed

It is tempting to read Repton's +159 as the rule and detect it automatically.
It is not a rule, and doing so would be guessing.

There is folklore that games map letters into the user-defined block by adding
160. Repton falsifies it. That is all one counterexample can do: **falsifying a
universal claim takes a single case, establishing one takes far more.** Repton
tells us +160 is wrong; it tells us nothing about what any other program does.

The corpus already shows the spread. Thrust uses no character codes at all,
blitting a private table. Repton's menu uses an offset. Repton's own credits
screen blits a private table like Thrust. Two games, three behaviours -- and no
reason to think a third game would match any of them.

So: **never infer meaning from a code.** The machine can tell you which code a
cell holds -- the redefinable area at &C00 and its aliases are a hardware fact,
and reading them is sound. It cannot tell you which letter that glyph draws,
because nothing in the machine records it. Only a supplied glyph set carries
that, authored by someone who looked at the screen.

An offset heuristic would produce confident wrong text where the design
otherwise produces honest spaces, which is the one failure mode this library
exists to avoid.

## Recovering a font automatically -- a note for later

Transcribing a font by hand, as `thrust.glyphs` was, is the only way to read a
game that hides its glyphs in a private table. Automating it is a substantial
feature in its own right, not attempted here, but two things are worth recording
for whoever considers it.

**General-purpose OCR does not work on this.** macOS's built-in text
recognition was tried on these screens and performed poorly. The reason is that
its priors do not fit: it expects anti-aliased, proportional, high-resolution
glyphs with a language model behind them, and a BBC character is an eight-by-
eight aliased bitmap -- five rows tall in Thrust's case -- in a bespoke
typeface, sometimes over a dithered background, where a letter is a few dozen
set pixels. Nothing it assumes holds.

**The problem is smaller than "OCR" makes it sound.** The exact matcher already
does the hard half: distinct bitmaps partition a screen with no fuzziness at
all, because the same bitmap is always the same character. A screen holds a few
dozen distinct glyphs, perfectly clustered. What is missing is only the label on
each cluster -- closer to a substitution cipher than to image recognition, with
word boundaries, letter frequency and dictionary shape all available as
evidence.

If it is built, it belongs **outside the reader**: a producer of glyph sets, not
a change to matching. A "learn a font from this image" tool whose output is a
`.glyphs` file a human inspects and corrects keeps the property the rest of this
library rests on -- the matcher never guesses, and anything uncertain is
reviewed before it is trusted rather than presented as read.
