# Reading Text That Is Not On The Grid

The second increment of the screen-text library, specified in
`screen-text-library-spec.md` as `Search::IncludeOffset` and deferred there to
a sentence. This is what that sentence turns out to mean.

Nothing here is built. The interface has carried `Search::IncludeOffset` and
`Cell::offset` since the first increment, accepted and honoured by nothing, and
a test asserts as much. This document says what should honour them.

## What it is for

`VDU 5` sends text to the graphics cursor rather than the text cursor. The MOS
draws each glyph at an arbitrary pixel position, in the graphics foreground
colour, over whatever is already there, and advances the cursor eight pixels.
Games use it constantly -- scores, labels, titles -- because it is the only way
to put text where you want it.

A grid walk misses all of it, which is the whole of the problem.

## Why the aligned algorithm does not simply extend

Two things break, and only one of them is the obvious one.

**Position.** There is no grid. A glyph may start at any pixel, so the search
is over all sixty-four sub-cell offsets rather than one. ZEsarUX does exactly
this and it works. That part is arithmetic.

**Colour.** This is the part that is easy to miss. The aligned reader asks
"does this cell reduce to a glyph" -- it takes the two colours in the cell,
tries both ways round, and rejects anything with three or more. That is right
for cell-aligned text, where the VDU drivers paint a background and then a
glyph on it.

`VDU 5` paints no background. It draws the glyph over whatever was there, so an
eight-by-eight window holding one typically contains the glyph's colour plus
however many colours the underlying picture had. The aligned rule would reject
most of it outright.

So the question has to change:

> Not "does this window reduce to a glyph", but "do the pixels of colour *c* in
> this window form a glyph, ignoring everything else" -- asked once per colour
> present in the window.

That is a second matching mode, not a second search position, and it is the
heart of the design.

### One consequence, free of charge

Asking only about colours *present in the window* means the empty bitmap never
arises: if *c* is in the window, at least one pixel is set. So the space glyph
can never match, and the worst false positive imaginable -- a blank window
matching a space at all sixty-four offsets, everywhere on the screen -- does
not need special handling. It cannot happen.

### The other consequence: drop shadows work

A common way to make text readable over a busy background is to draw it twice
in two colours, a pixel or two apart, so it carries its own shadow. The shadow
goes down first and the text over it.

Per-colour matching handles this without knowing anything about it. The pixels
of the text colour form the glyph exactly, because the text was drawn last.
The pixels of the shadow colour form a crescent -- the shadow minus the part
the text covered -- which matches nothing. One reading, the right one.

## The noise problem

A free search over a graphics screen finds glyph-shaped patterns that are not
glyphs. Measured over 232,128 positions of synthetic screens carrying filled
regions, diagonals, discs, dithered patches and four hundred pieces of one- to
three-pixel detail, the naive search produced 1,220 spurious matches.

They were not spread across the alphabet. Every one was `_`, `.` or `-`.

That is the shape of the problem: a handful of glyphs are indistinguishable
from things pictures are made of, and the rest essentially never occur by
accident.

## Distinctiveness

> A glyph is **distinctive** when it is not HV-convex -- when some row or some
> column of its bitmap contains more than one run of set pixels.

Pictures are made of filled regions, straight edges, bars, blobs, discs and
diagonals. Every one of those has unbroken runs in both axes: that is what it
means to be a filled convex-ish shape. A letter needs a concavity or a hole --
a gap in some row or column -- and imagery produces those only by coincidence.

The measure has three properties worth stating.

**It is parameter-free.** No ink threshold, no size cutoff, nothing to tune.
The first attempt at this was "fewer than eight pixels, or a solid rectangle",
which is two magic numbers and a special case, and would not survive a font
whose letters are drawn heavier or lighter than the ROM's.

**It reads only the bitmap**, so it works for a font nobody has seen. Across
the six fonts in the corpus -- the MOS ROM's and five period fonts from
elsewhere -- it classifies between ten and eighteen per cent of glyphs as
simple, and the sets are sensible every time: space, `'`, `,`, `-`, `.`, `_`,
the bars (`l`, `|`, `I`, `1`), the corners (`L`, `T`), the crosses (`+`), and
the diagonals (`/`, `\`). That last pair falls out of the shape alone, which is
as it should be: a diagonal line is one of the commonest things on a screen.

**It predicts the noise.** Of the 1,220 spurious matches measured, every single
one was a simple glyph and none was distinctive. `.` alone accounted for 245 of
them -- a two-by-two blob of colour, which is exactly the failure this has to
prevent.

## Registration

Distinctiveness alone would throw away real text: a line reading `- - -` is
made entirely of simple glyphs.

What rescues them is that text is *regular*. `VDU 5` advances the graphics
cursor by exactly eight pixels per character and does not move it vertically,
so the glyphs of a line share a baseline, a colour, and an eight-pixel lattice.
Three coincidences at once, which noise does not supply.

So candidates are grouped by baseline, colour, and position modulo eight, and
maximal sequences eight pixels apart become runs. A run is credible when it
holds at least one distinctive glyph; the simple glyphs in it come along, being
vouched for by the company they keep.

On the pure-graphics screens this reduced 1,740 raw candidates to 942 runs to
**zero** credible runs. On a screen carrying `VDU 5` text over graphics it
found the text, at its exact off-grid position, and nothing else.

### Runs break at spaces

A space matches nothing, so `SCORE 1000` comes back as two runs rather than
one. Runs sharing a baseline, a colour and a lattice, separated by an exact
multiple of eight pixels, should be rejoined with the spaces written back in.
The information is all there; it is only a matter of not throwing it away.

## What a caller gets

Runs whose bounds are not cell-aligned, with the cell geometry fields zero, as
`screen-text-extraction.md` anticipated. `Cell::offset` set, for the first
time. Everything else -- the codepoints, the two colours, the alternatives when
a font cannot tell two characters apart -- means what it already means.

## Cost

Sixty-four offsets times the number of colours present in each window. That is
why it is opt-in, reached through free-form selection rather than attempted on
every copy, and why the aligned path stays exactly as it is.

## What is not settled

**Does a lone distinctive glyph count?** A single `A` over graphics, in
registration with nothing. The evidence says yes -- no distinctive glyph
matched by chance in 232,128 positions -- but the residual risk sits with the
low-ink distinctive glyphs, `"`, `:` and `=`, which have a gap and so pass the
test while being small enough that dithering might conceivably draw one. If
they misfire in the wild the fix is narrow: require registration for
distinctive glyphs below some ink, rather than changing the definition.

**How much of a run must be distinctive?** One glyph is the current proposal.
A long run of simple glyphs with a single distinctive one at the end is
admitted on thin evidence.

**Overlapping candidates.** The same pixels may match at several offsets or in
several colours. Longer runs should win, but the rule needs writing down and
making deterministic.

## What is needed before building it

Real screens. Every number above comes from synthetic graphics -- my idea of a
busy display, not a real one's. A game's title screen has structure that random
rectangles and discs do not, and the honest position is that the noise
measurements are indicative rather than trustworthy until they have been
repeated against captured frames carrying genuine `VDU 5` text.

That is the next step, and it is deliberately before the implementation rather
than after it.
