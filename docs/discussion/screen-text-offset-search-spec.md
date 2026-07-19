# Build Specification: Off-Grid Text

A work order for the second increment of the screen-text library. Read
`screen-text-offset-search.md` first -- it is the design, it is validated, and
this document does not repeat it. This one says what to build and what to
settle on the way.

The prototype behind that document is adopted. Productise it.

## What to build

`Search::IncludeOffset`, honoured. `Cell::offset`, set. The interface has
carried both since the first increment with a test asserting nothing honours
them; that test inverts.

The design is not up for renegotiation: per-colour matching, HV-convexity as
the distinctiveness criterion, grouping by baseline and colour, runs at eight
pixels give or take one, longest-run-wins for overlaps. Every one of those was
measured against real screens. Change them only if productising produces
evidence against them, and say so loudly if it does.

## Acceptance is already written

The corpus holds three screens whose off-grid labels are currently asserted
**absent**, because the aligned reader cannot see them:

- `fruits-machine.png` -- six off-grid labels at irregular vertical spacings,
  on a panel where other labels happen to land on the grid
- `rondo-title.png` -- drop-shadowed title, three colours per cell, off-grid
- `krazy-game.png` -- `LIVES=` on an imperfect lattice, gaps of 9, 8, 9, 8, 8

Those expectations invert. That is the acceptance target, and it is unusually
clean: the tests exist, they pass today for the wrong reason, and they must
pass tomorrow for the right one.

The negative sets matter as much. The Waffle screens and the synthetic
graphics screens must continue to yield **zero** off-grid false positives.
A change that finds more text and also finds ghosts is a regression.

## Settle these while building

**Time it.** Nobody has measured the off-grid pass on a whole screen, and that
number decides policy elsewhere: whether a whole-screen copy runs the off-grid
pass by default, or whether it stays reached only through free-form selection.
Report milliseconds for a full screen at each colour depth, on ordinary
hardware. This is the single most useful thing the work can produce beyond the
feature itself.

**Rejoin runs across spaces.** The design notes that `SCORE 1000` comes back as
two runs because a space matches nothing, and that runs sharing a baseline, a
colour and a lattice separated by an exact multiple of eight should be rejoined
with the spaces written back. Do that; the information is there.

**Close the hostile-background gap, if it can be closed cheaply.** The design
names one untested combination: off-grid text over a dense background. Loopy
Loop has the background but draws in a thickened non-ROM font, so it does not
match -- but the library takes supplied glyph sets. Extracting that font and
supplying it turns an untestable screen into exactly the missing fixture. Try
it. If the font resists extraction, say so and leave the gap named rather than
quietly unfilled.

**How much of a run must be distinctive** stays at one glyph unless the corpus
forces otherwise. If nothing forces it, leave it and record that nothing did.

## What not to do

- Do not add tuning parameters. The distinctiveness criterion is parameter-free
  and that is why it is expected to survive fonts nobody has seen. If something
  seems to need a threshold, that is a finding worth reporting, not a knob
  worth adding.
- Do not make the aligned path pay for this. It stays exactly as it is: same
  speed, same results, same tests.
- Do not let a lower-confidence result be reported as if it were certain. The
  existing distinction between unmatched and ambiguous cells applies here too.

## Conventions

As before: test first, Catch2, 7-bit ASCII in source with non-ASCII written as
UTF-8 byte escapes, C++20, British spelling in prose, no dependency on anything
in Beebium outside the library's own directory, commit frequently, never push,
no AI attribution in commit messages.

## Done means

- The three inverted expectations pass, and the negative sets still yield zero.
- A timing for a whole-screen off-grid pass, reported.
- Runs rejoined across spaces.
- The hostile-background fixture added, or its absence explained.
- A summary of what was built, anything the corpus forced you to change, and
  anything you could not verify.
