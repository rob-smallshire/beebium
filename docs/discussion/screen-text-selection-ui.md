# Selecting and Copying Screen Text (macOS)

Stream 4 of `screen-text-extraction.md`: the macOS front end's part. How a user
selects text on the emulated display and copies it, built on the
`GetScreenText` and `GetScreenGeometry` calls stream 3 delivered.

This is the interaction design. A build work order will follow; this document
fixes the model and the reasons for it so the work order does not have to
re-argue them.

## Scope

- Selecting a region of the display with the mouse and copying the text in it.
- Copying the whole screen's text with no selection.
- Three interpretations of a selection: aligned rows, aligned rectangle, and
  anywhere (text on and off the character grid).

Out of scope here, each for its own reason:

- **Copy as Image** is a separate, later command -- a single "copy the BBC
  display as pixels", no marquee, because macOS already has good tools for
  grabbing an arbitrary rectangle of any window. Deferred.
- **Reading bitmap-mode text at all** is stream 2. This document assumes
  `GetScreenText` returns text for whatever the machine is showing. It was
  written while that was true only of MODE 7; stream 2 has since landed and the
  UI here needed no change when the bitmap modes began answering, which was the
  property the design was judged on.

## The governing idea: selecting holds the screen

The display is video -- fifty frames a second, and on a game every one of them
different. A copy has to name a single frame, and a selection drawn over moving
pixels is a selection of nothing in particular.

So **the moment a selection gesture begins, the screen is held.** The selection,
the highlighting, and the copy all operate on that one still. The emulator keeps
running underneath; holding is a copy, not a pause, and it ends when the
selection is dismissed.

This earns three things at once:

- Recognising text becomes a static-image problem, which the library already
  solves, rather than a moving-target one.
- The copy is **what you saw**: the frame you began selecting on, not whichever
  frame happened to be up when you pressed the key.
- On the screens people actually copy -- a listing, a menu, an error message --
  the picture is static anyway, so the hold is invisible.

### Holding has to happen on the server -- corrected

The first build read this as a property of the *view* alone: the client stopped
handing frames to its renderer, and the picture stood still. That is half the
job, and the wrong half. The reads behind the still frame went on running
against the **live** screen, so on anything that moved -- a game, a demo,
scrolling text -- the highlights and the copy described a frame the user was not
looking at. The promise the freeze makes is precisely the one it broke.

Nor is it only the pixels. A reading depends on four things that move
independently:

- the pixels,
- the band geometry,
- the teletext grid,
- and the font in RAM, which `VDU 23` can redefine at any moment.

Read at four different instants they describe a screen that never existed. So
they are captured **together**, server-side, by `HoldScreen`, and every later
read names the capture. `GetScreenText` and `GetScreenGeometry` take a `holdId`;
an unknown or expired hold fails `NOT_FOUND`, because quietly falling back to
the live screen would be the very confusion holding exists to remove.

Two consequences worth stating, because both were live options:

- **The machine is not paused.** Pausing would have been far cheaper and just as
  coherent -- it freezes the RAM too -- but it makes a local UI gesture stop a
  *shared* machine: severing a real-time transport mid-transfer, and stopping
  other clients for reasons invisible to them. The gesture must not reach the
  machine.
- **The client displays the captured still**, which `HoldScreen` returns on
  request. Without that there is still a frame or two between the picture the
  view last drew and the frame the server captured. Showing the server's still
  makes them the same frame by construction rather than by luck.

Holding also returns the grid, so it costs no more round trips than fetching the
geometry alone used to -- and the geometry can no longer describe a different
frame from the pixels. Holds expire and are bounded, so a client that dies
cannot leak one; a fresh drag releases what the last one held.

## The gesture: Cmd-drag

**A plain drag is reserved for the machine** -- the future BBC mouse, and
nothing the host does should train the user's hand into a gesture that will one
day mean something to the Beeb. **A `Cmd`-drag selects text.** One rule, and it
is the whole of how selection is invoked.

`Cmd` specifically, and not another modifier, for a concrete reason: `Shift`
and `Control` are *real keys on the BBC keyboard* and are forwarded to the
machine. `Cmd` is not -- the Beeb has no such key -- so it is the one modifier
guaranteed never to mean anything to the emulated machine, which makes it the
safe thing to gate a host gesture on. (`Option` is also Beeb-safe in practice,
but `Cmd` reads as "a host command", which is what this is.)

Once a `Cmd`-drag has begun, the gesture is host-owned, so further modifiers
held during it qualify the *selection* rather than reaching the machine -- which
is what makes `Shift` and `Option` usable below despite being unsafe as the
base. While a host drag is in progress the client must **not** forward the
qualifier modifiers to the emulated keyboard; in practice `Shift` alone does
nothing to the Beeb without an accompanying keypress, so this is tidiness rather
than a live bug, but it should be gated all the same.

The drag records an **anchor and a focus** -- where it started and where it is
now -- not merely a rectangle. Rows selection needs this: dragging from the
middle of one line to the middle of another, three lines down, selects the tail
of the first line, all of the middle ones, and the head of the last -- a
text-flow region, not a box. Anchor-and-focus also yields a bounding box for the
other two interpretations, so one gesture feeds all three.

## Interpretation is chosen by a held modifier

The drag itself is interpretation-agnostic. Which of the three readings applies
is decided by a modifier held while a selection is active -- during the drag, or
after it:

| Held | Interpretation |
|---|---|
| (none) | **Aligned, rows.** Snaps to the character grid; reading order from anchor to focus, like selecting text in an editor. The common case, the lightest gesture. |
| `Option` | **Aligned, rectangle.** The block of cells the selection covers -- column selection, for a table of figures. `Option` for a rectangle is the system-wide convention (Terminal, Xcode, most editors). |
| `Shift` | **Anywhere.** All the text whose glyphs fall in the selection, on the grid and placed freely with `VDU 5`. There is no "anywhere rows": off-grid text has no grid rows, so this is inherently a rectangle. |

The modifier does one job and does it in two places: it decides **what is
highlighted** and **what a copy captures**. They are never out of step, because
they are the same choice -- the thing you are looking at is the thing a copy
takes.

## The modifier drives the highlight; the modifier-plus-C copies

While a selection is live, the recognised text is **highlighted** on the held
still -- the cells `GetScreenText` reports as matched, drawn over the glyphs they
name. Change the held modifier and the highlight changes to that interpretation.

Copying commits the highlighted interpretation:

- `Cmd`-`C` -- aligned rows (**Copy**)
- `Option`-`Cmd`-`C` -- aligned rectangle (**Copy as Columns**)
- `Shift`-`Cmd`-`C` -- anywhere (**Copy Text from Graphics**)

These are also three items in the Edit menu, so the commands are discoverable
and the menu shows their shortcuts. The three affordances agree: the menu names
them, holding a shortcut's modifier previews it, pressing its key commits it.

**Copy fires on key-down**, the macOS convention for a menu shortcut. It needs
no key-up phase to give the user a look first, because the *modifier* already
did: you hold `Shift`-`Cmd`, see the anywhere highlight, and press `C` on a
selection you have already seen. `C` is the trigger; the modifier is the choice.

Because the copy reads the held screen, it takes exactly what was highlighted.

### With no selection

`Cmd`-`C` with nothing selected copies the **whole screen, aligned**, from the
current live frame. There is no selection to hold and nothing to preview, so
this is an immediate capture and needs no hold at all. It is the everyday "copy what is on the screen",
and the screen is usually static text when someone asks for it.

## Cancelling

**Escape** dismisses the selection, releases the hold, and the display animates
on. So does **a plain click** anywhere -- the natural "give me the machine
back", and a plain click is already bound for the Beeb.

While a selection is live, Escape is **swallowed** -- it ends the selection and
does not reach the machine. This is the same rule the paste feature already
follows: while a host interaction owns the screen, Escape ends *that*, and only
reaches the Beeb when there is nothing host-side to cancel.

## What the client fetches

The held screen makes everything fixed for the selection's lifetime, so:

- On drag start, call **`HoldScreen`** asking for the frame. It returns the hold
  id, the grid, and the captured still. Display the still; the grid maps
  selection pixels to cells -- needed to compute the rows text-flow from anchor
  and focus, and to know where cell boundaries are. This replaces the separate
  `GetScreenGeometry` call an earlier draft made here: the grid arriving with
  the hold is what stops it describing a different frame from the pixels.
- As the selection or the modifier changes, call **`GetScreenText`** with the
  hold id for the region in the current interpretation, and highlight what it
  returns.
- On dismissal, call **`ReleaseScreen`**.

**Highlight the cells, not the runs.** A run's `bounds` spans every cell on its
line, unmatched ones included, so painting it lights up ink the font could not
read as though it had been. Each run carries its `cells`, each with its own
bounds and a `matched` flag; highlight those. Two refinements fall out of using
it:

- A **matched space** is a real character and stays lit *between* real glyphs,
  the way an editor highlights the space in "two words".
- A matched space adrift in a run of unrecognised glyphs is not text anyone
  read. In a game with a custom font every letter is unmatched while the gaps
  between words match, so highlighting every matched cell lit the gaps and left
  the words dark -- the inverse of useful. A space is lit only when it bridges
  real glyphs, so a run whose glyphs are all unreadable highlights nothing and
  the overlay says so honestly.

**A blank row is a row.** The copied text has a line for every row the
selection covers, blank ones included, and nothing is trimmed at either end: if
you dragged over three empty rows above the text, you get three empty lines. The
shape of the screen is part of what was selected, and a boot message whose gaps
were closed up would come back as four solid lines instead of four separated by
blanks.

That has to be built from the **row range**, not from the runs. Taking a line
per run drops any row that had nothing readable on it -- and the two strategies
disagree about how they report such a row, which walking the rows makes moot:
the teletext reader emits an empty run for it, the glyph recogniser emits no run
at all.

Blank rows stay out of the *highlight*, though. There is nothing on them to
light up, and the range layer already shows they are selected.

The wrinkle is rows: an aligned-rows selection is a text-flow region, not a
rectangle, and `GetScreenText` takes a rectangular region. Settled as the first
of the two options: request the bounding rectangle and trim the first and last
rows to the flow client-side. A run's cells are evenly spaced at a known
`cell_width` and its `text` is one character per cell, so which column a
character sits in is arithmetic -- and the cells travel with their characters
through the trim, so the ragged rows respect readability too. Teaching
`GetScreenText` a flow region was not needed.

## Composition with Display Style

The selection overlay sits **above** the Display Style system, not inside it.
`display-styles.md` warns against a Cartesian product of options; a selection is
an interaction layer that composes with Standard, Debug, or a future CRT style
rather than being a variant of any of them. The overlay draws in the view's
coordinate space after the style has drawn the frame, and the pixel-to-cell
mapping accounts for whatever geometry the active style produced.

## Phasing

**Version one: highlights. Built.** The hold, the `Cmd`-drag with anchor and
focus, the modifier-chosen interpretation, the run highlights that update as the
selection and modifier change, the three copy commands, and Escape / click to
cancel. This is a complete, usable copy feature. The highlights *are* the
feedback: you see which glyphs are caught before you commit.

Version one grew one thing this document did not anticipate, from using it:
**three layers of feedback, not one.** Highlighting only the recognised text
left aligned-rows selection looking unfamiliar, because every editor draws the
*range* -- the reading-order region, out to the screen edges -- and not merely
the words inside it. So the overlay draws, back to front:

1. the **range**: the reading-order flow for rows, the snapped block for a
   rectangle, and nothing for anywhere, which has no grid to snap to;
2. the **text**: the matched cells, which is what a copy will take;
3. the **marquee**: the literal dragged rectangle.

Two intensities of one tint separate the first two, because *what is selected*
and *what will copy* genuinely differ -- trailing blanks and unreadable cells sit
in the range and never reach the clipboard. The marquee is drawn only in the
anywhere mode: rows and rectangle trace themselves with their snapped range, and
the raw drag rectangle over the top was clutter. Each mode therefore has its own
silhouette -- notched flow, clean block, or bare outline -- which is what tells
you which one you are in.

**Version two: preview -- dropped.** The plan was to surface the extracted text
*as text*, in a panel showing what would be copied with the unreadable cells
marked. It was deferred until version one had been lived with, on the grounds
that the preview should be shaped by experience rather than guessed. Living with
it answered the question the other way: the highlights turned out to be feedback
enough, especially once they showed only the cells actually read, and a panel
would add furniture for something the overlay already says. Not deferred --
unnecessary. Nothing forecloses it if that judgement ever changes: the text and
the per-cell detail are already in the `GetScreenText` response.

## Settled since

**The command names**, which this document left as placeholders. They are now
**Copy** (`Cmd`-`C`), **Copy as Columns** (`Option`-`Cmd`-`C`) and **Copy Text
from Graphics** (`Shift`-`Cmd`-`C`), sitting together directly beneath the
standard Copy -- SwiftUI can only place a group after the whole pasteboard
group, so an AppKit reorder pulls them up, as it already did for Paste at Full
Speed.

"Copy Text from Graphics" was chosen over "Copy Graphics as Text" because the
`as` collides with "Copy *as* Columns" while meaning something different, and
because the phrase usefully signals the least certain of the three -- it reads
text out of pixels. Each variant carries a help string, which is where the
nuance a three-word menu item cannot hold actually lives.

**Whole-screen copy needs no hold.** A `Cmd`-`C` with nothing selected is a
single atomic read with no preview to be inconsistent with, so it reads the live
screen and stays as it was.

**An unreadable cell copies as a space.** It holds its column so the text stays
aligned with the screen, and the highlight already says it was not read, so the
copy does not have to refuse as well. Making a wholly unreadable selection copy
nothing was considered and judged not worth the special case.

## Open questions

1. **Adjusting a finished selection.** Version one treats a new `Cmd`-drag as a
   fresh selection, releasing whatever the last one held. Shift-click-to-extend
   and drag handles are refinements that can wait, unless they prove necessary
   early.
2. **Reading a game's own font.** A game that draws with glyphs of its own
   copies as spaces, because nothing in the machine records which letter a
   redefined glyph draws -- only a supplied glyph set carries that. Whether
   Beebium should ship or produce such sets is open; the reader itself will not
   guess. See `screen-text-extraction.md` and the corpus notes under
   `src/screen-text/tests/fixtures/fonts/`.

