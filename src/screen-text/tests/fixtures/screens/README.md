# Screens from a real game

Four frames from Waffle, by Chris Bradburne, after the web game at
wafflegame.net. Captured by `../capture_waffle.py`.

These matter because everything else in the corpus was rendered either by the
emulator's own tests or by this library's, and so shares its assumptions.
Waffle was written by somebody else, for a real machine, with no thought for
being read back.

| Screen | What is on it |
|---|---|
| `waffle-title.png` | The title, a byline, and a graphic waffle |
| `waffle-instructions-2.png` | Text flowing around example tiles |
| `waffle-instructions-4.png` | The same, with letters scattered in a diagram |
| `waffle-board.png` | The game: mostly graphics, three lines of status text |
| `loopy-loop.png` | Loopy Loop, by A.S.Shakoor: text over a dense dithered background, and text off the character grid |
| `fruits-wins.png` | Fruits: a win table, text among graphics |
| `fruits-machine.png` | Fruits: the game, with labels at arbitrary pixel positions in the ROM font |
| `rondo-title.png` | Rondo: a drop-shadowed title off the grid, over an ordinary high score table |
| `krazy-controls.png` | Krazy Ape II: text in a flashing colour, under a drop-shadowed title |
| `krazy-loading.png` | Krazy Ape II: MODE 6, white on blue with blanked scanlines between rows |
| `krazy-game.png` | Krazy Ape II: in-game drop-shadowed labels, placed on an imperfect lattice |

# Waffle

MODE 1 -- 320x256 logical pixels, four colours, one display region,
progressive. The text is the standard Acorn font on the character grid at
origin 0,0, which was established by sweeping all 64 grid origins and finding
0,0 matched the most cells.

## What it showed

**Every word read is a real word.** Across all four screens the extracted text
contains no invented characters at all -- the game's own typo, "horzontal",
appears consistently twice and is the game's, not a misreading. Between 32 and
358 cells per screen come back unmatched, which is the graphics, the oversized
tile letters and the title's non-ROM font. All correctly declined rather than
guessed at.

That is the property worth protecting, and it is what the tests here assert:
not merely that the text is found, but that nothing else is.

## What they settled about offset search

These screens were also run through a prototype of the sub-cell offset search
described in `docs/discussion/screen-text-offset-search.md`, which until then
had only ever seen synthetic graphics. Two open questions closed.

**A lone distinctive glyph does count.** The design doc left this open, leaning
towards yes. Waffle's diagrams settle it: they contain genuinely isolated
letters -- `A C O R N` and `M I C R O` spread across example tiles -- which
requiring a run of two or more would discard. Twenty-five of them on one screen
alone.

**Overlapping candidates must be resolved, and it is not optional.** Each
instruction screen produced exactly one off-grid false positive: a lone `p` at
y=238, six pixels above a real line of text at y=240. The ghost came not from
the graphics but from *real text at a near-miss offset*, which the synthetic
screens never produced because they had no text on them. Keeping the longest
run and dropping any run that overlaps one already kept removes both ghosts and
costs none of the genuine isolated letters.

With that rule the four screens yield no off-grid false positives at all.

One honest loss: the title reads `W A F F E`. The spaced-out `L` is dropped,
`L` being HV-convex -- a corner shape, indistinguishable from things graphics
are made of -- and standing alone with nothing to be in registration with.

# Loopy Loop

MODE 2 -- 160x256 logical pixels. Two things no other screen in the corpus
has.

**Text over a background dense enough to defeat the aligned reader.** The
title, byline and key legend sit on a dithered pattern, so most of their cells
hold three or more colours and are declined. Only the panel text, ROM font on
black, is read: 386 of 640 cells come back unmatched. This is the screen that
shows why offset search needs to ask "do the pixels of colour c form a glyph"
rather than "does this cell reduce to a glyph" -- the second question has no
good answer here.

**Text that is genuinely not on the character grid.** The offset-search
prototype found nine runs at baselines of 30, 43, 55, 68 and 239, none of them
a multiple of eight. Every one corresponds to a character actually on the
screen -- `H` and `K` from SHAKOOR, `'92`, `?` and `*` from the key legend,
`D`, `W` and `N` from DOWN -- and three were checked against the ROM font
pixel by pixel and match it exactly. No false positives at all, on the most
hostile background in the corpus.

Recall is another matter. Many characters on those lines are drawn *thicker*
than the ROM glyph, the same shape OR'd with itself shifted a pixel, so they
are not ROM glyphs any more and rightly do not match: `Z` and `L` were checked
and differ. Reading those wants the game's own font supplied.

**A redefined character.** The game prints "to reach next" with a diamond in
place of the `a`, put there by `VDU 23`. It matches nothing and is reported
unmatched, so the line reads `to re ch next` rather than a plausible wrong
word. Exactly the intended behaviour, and a reminder that a screen's font is
not always the ROM's even when it looks like it.

# Fruits

MODE 1 -- 320x256. The screen the offset search was waiting for: text in the
ROM font at arbitrary pixel positions, on black.

`fruits-machine.png` places `GAMBLE` at y=173, `BANK` at 186, `DOUBLE` at 203
and `QUITS` at 216 -- spacings of 13, 17 and 13, none of them a multiple of
eight -- with `MELON METER` at 213 and 223. `COLLECT` above them lands on the
grid at 160, so one panel carries both cases.

The aligned reader finds the fifteen runs on the grid and none of the six off
it, which is correct and is what the second increment is for. The prototype
finds all twenty-one and nothing else: no false positive among 1,106 raw
candidates.

The tests here assert the current behaviour, including that the six off-grid
labels are *absent*. When offset search lands those assertions invert, which
makes them its acceptance target.

The title screen is MODE 7 and interlaced, 640x500 -- teletext, which this
library deliberately never sees, so it is not kept.

# Rondo

MODE 2 -- 160x256. The drop-shadow case, drawn the usual way: the title
rendered twice a pixel apart in two colours, shadow first and text over it.

The `R` of `RONDO` holds three colours. Yellow forms the ROM glyph to the
pixel; red forms the crescent of shadow the text did not cover; black is what
is left of the ground. Neither the red nor the black is any glyph at all.

It sits at y=14, x=61, so it fails the aligned rule twice over -- three colours
and off the grid -- and the aligned reader reads the high score table beneath
it and not the title. The prototype reads `RONDO`, and the whole screen yields
32 runs, every one of them real text.

This is the case the design predicted would work for nothing, and it does. The
test here asserts the title's absence, so it inverts when offset search lands.

The instruction screens before it are MODE 7 and interlaced, and not kept.

# Krazy Ape II

Three screens, each carrying something.

`krazy-loading.png` is **MODE 6 in the wild** -- 320x250, white on blue, with
the two blanked scanlines between character rows. Read with an 8-scanline cell
on a 10-scanline pitch, all 1000 cells match and none is unread. Read as though
the cell were ten scanlines tall, *none* matches: the gap is a third colour and
no cell can be one glyph. That is the pitch design meeting a real screen, and
it behaves exactly as the synthetic corpus said it would.

`krazy-controls.png` prints its last two lines in a **flashing colour**, which
alternates about once a second, so a capture catches one phase. It does not
matter which: nothing here knows what a palette is, and the text reads. The
drop-shadowed title above is three colours and does not.

`krazy-game.png` carries the in-game labels, and turned up the one thing in the
corpus that contradicted a rule. `LIVES=` is drawn at x = 1, 10, 18, 27, 35,
43 -- gaps of 9, 8, 9, 8, 8 -- so a game placing characters by hand does not
always use a perfect lattice. Registration requiring exactly eight split it
into `IV` and `ES` and lost the `L` altogether. Allowing eight give or take one
reads `LIVES`, and the Waffle screens produce identical results either way. The
design was amended accordingly.

# Recapturing

The discs are in `discs/games/`, which is not in the repository, so this cannot
be done from a clean checkout. Only Waffle has a capture script, its navigation
being worth recording. Loopy Loop needs no navigation, only fifteen seconds to
finish drawing. Fruits wants a key at its MODE 7 title, another at the win
table, and a third to reach the machine, each screen taking a few seconds.
Rondo wants four presses of SPACE about a second apart to pass its MODE 7
instructions, then a few seconds to settle. Krazy Ape II runs a long attract
sequence: five instruction screens, each advanced by SPACE, then a MODE 6 load
before the game.

```
cd clients/beebium-python-client
uv run --extra imaging python ../../src/screen-text/tests/fixtures/capture_waffle.py
```

The navigation is boot, `Y` at "Instructions?", then a key through four
instruction screens and into the game. Each screen wants a few seconds to draw;
capture too early and you get it half-finished.

# Thrust

`thrust-hiscore.png` -- the high-score table of Thrust (Superior Software),
288x240, reached by pressing ESCAPE during play. Its text is drawn in the
game's own five-pixel font, blitted from a private glyph table rather than
printed as MOS characters, so the ROM glyph set matches almost none of it and
the table copies as spaces -- the correct "unread, not guessed" behaviour for a
custom-font game. Supplying `../fonts/thrust.glyphs`, transcribed from this very
screen, reads it in full. Both are asserted as a test.

Captured with `../capture_thrust.py`, which also dumps the redefinable-character
RAM to show the font is not there -- evidence the text is not MOS characters.

# Repton 3

`repton3-select.png` -- the MODE 5 splash and menu (PLEASE SELECT / (1) THE
GAME / (2) THE EDITOR), 160x256. Where Thrust hides its font in a private
table, Repton's menu is the opposite: the MOS soft font at &C00, characters
224-249 redefined as a chunky A-Z (the game prints a capital as letter+159). So
the ROM-font "(1)" and "(2)" read, while the redefined words copy as spaces
until `../fonts/repton3.glyphs` -- read straight from that soft-font RAM -- is
supplied, and then the menu reads in full. Both are asserted as a test.

The game's own credits screen ("Written by Matthew Atkinson"), reached by
selecting the game and waiting through the load, uses a different thin font
blitted from a private table, in neither &C00 nor the ROM -- so Repton uses
both mechanisms, a screen each. It is investigated but not committed.

Captured with `../capture_repton3.py`, which dumps the soft font to show it is
there (unlike Thrust's).
