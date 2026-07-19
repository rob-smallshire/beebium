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

MODE 1 throughout -- 320x256 logical pixels, four colours, one display region,
progressive. The text is the standard Acorn font on the character grid at
origin 0,0, which was established by sweeping all 64 grid origins and finding
0,0 matched the most cells.

## What they showed

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

## Recapturing

The disc is `discs/games/Disc165-Waffle.ssd`, and `discs/games/` is not in the
repository, so this cannot be done from a clean checkout.

```
cd clients/beebium-python-client
uv run --extra imaging python ../../src/screen-text/tests/fixtures/capture_waffle.py
```

The navigation is boot, `Y` at "Instructions?", then a key through four
instruction screens and into the game. Each screen wants a few seconds to draw;
capture too early and you get it half-finished.
