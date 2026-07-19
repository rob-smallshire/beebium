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

# Recapturing

The discs are in `discs/games/`, which is not in the repository, so this cannot
be done from a clean checkout. Only Waffle has a capture script, its navigation
being worth recording. Loopy Loop needs no navigation, only fifteen seconds to
finish drawing. Fruits wants a key at its MODE 7 title, another at the win
table, and a third to reach the machine, each screen taking a few seconds.

```
cd clients/beebium-python-client
uv run --extra imaging python ../../src/screen-text/tests/fixtures/capture_waffle.py
```

The navigation is boot, `Y` at "Instructions?", then a key through four
instruction screens and into the game. Each screen wants a few seconds to draw;
capture too early and you get it half-finished.
