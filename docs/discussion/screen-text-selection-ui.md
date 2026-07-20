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
  `GetScreenText` returns text for whatever the machine is showing; until
  stream 2 lands it returns text only in MODE 7, and the UI here works
  unchanged when bitmap modes start answering.

## The governing idea: selecting freezes a frame

The display is video -- fifty frames a second, and on a game every one of them
different. A copy has to name a single frame, and a selection drawn over moving
pixels is a selection of nothing in particular.

So **the moment a selection gesture begins, the displayed frame is frozen.**
The selection, the highlighting, and the copy all operate on that one still.
The emulator keeps running underneath; the freeze is a property of the view, not
the machine, and it ends when the selection is dismissed.

This earns three things at once:

- Recognising text becomes a static-image problem, which the library already
  solves, rather than a moving-target one.
- The copy is **what you saw**: the frame you began selecting on, not whichever
  frame happened to be up when you pressed the key.
- On the screens people actually copy -- a listing, a menu, an error message --
  the picture is static anyway, so the freeze is invisible.

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

While a selection is live, the recognised text is **highlighted** on the frozen
frame -- the runs `GetScreenText` returns, drawn over the glyphs they name.
Change the held modifier and the highlight changes to that interpretation.

Copying commits the highlighted interpretation:

- `Cmd`-`C` -- aligned rows
- `Option`-`Cmd`-`C` -- aligned rectangle
- `Shift`-`Cmd`-`C` -- anywhere

These are also three items in the Edit menu, so the commands are discoverable
and the menu shows their shortcuts. The three affordances agree: the menu names
them, holding a shortcut's modifier previews it, pressing its key commits it.

**Copy fires on key-down**, the macOS convention for a menu shortcut. It needs
no key-up phase to give the user a look first, because the *modifier* already
did: you hold `Shift`-`Cmd`, see the anywhere highlight, and press `C` on a
selection you have already seen. `C` is the trigger; the modifier is the choice.

Because the copy reads the frozen frame, it takes exactly what was highlighted.

### With no selection

`Cmd`-`C` with nothing selected copies the **whole screen, aligned**, from the
current live frame. There is no selection to freeze and nothing to preview, so
this is an immediate capture. It is the everyday "copy what is on the screen",
and the screen is usually static text when someone asks for it.

## Cancelling

**Escape** dismisses the selection, drops the freeze, and the display animates
on. So does **a plain click** anywhere -- the natural "give me the machine
back", and a plain click is already bound for the Beeb.

While a selection is live, Escape is **swallowed** -- it ends the selection and
does not reach the machine. This is the same rule the paste feature already
follows: while a host interaction owns the screen, Escape ends *that*, and only
reaches the Beeb when there is nothing host-side to cancel.

## What the client fetches, and one wrinkle

The frozen frame makes the geometry fixed for the selection's lifetime, so:

- On freeze (drag start), fetch **`GetScreenGeometry` once** and cache it. It
  maps selection pixels to grid cells -- needed to compute the rows text-flow
  from anchor and focus, and to know where cell boundaries are.
- As the selection or the modifier changes, fetch **`GetScreenText`** for the
  region in the current interpretation, and highlight the runs it returns. Their
  `bounds` are the highlight rectangles; that field exists for exactly this.

The wrinkle is rows: an aligned-rows selection is a text-flow region, not a
rectangle, and `GetScreenText` takes a rectangular region. Two ways to bridge
it, to be settled in the work order, not here: request the bounding rectangle
and trim the first and last rows to the flow client-side (a run's cells are
evenly spaced at a known `cell_width`, so this is arithmetic), or teach
`GetScreenText` a flow selection. The first needs no server change and is the
default assumption; the second is available if the first proves fiddly.

## Composition with Display Style

The selection overlay sits **above** the Display Style system, not inside it.
`display-styles.md` warns against a Cartesian product of options; a selection is
an interaction layer that composes with Standard, Debug, or a future CRT style
rather than being a variant of any of them. The overlay draws in the view's
coordinate space after the style has drawn the frame, and the pixel-to-cell
mapping accounts for whatever geometry the active style produced.

## Phasing

**Version one: highlights.** The freeze, the `Cmd`-drag with anchor and focus,
the modifier-chosen interpretation, the run highlights that update as the
selection and modifier change, the three copy commands, and Escape / click to
cancel. This is a complete, usable copy feature. The highlights *are* the
feedback: you see which glyphs are caught before you commit.

**Version two: preview.** Surfacing the extracted text *as text* -- a floating
panel or the status bar showing what will be copied, with the ambiguous and
unreadable cells the library distinguishes marked as such. Deliberately deferred
until version one has been lived with: the highlight interaction wants using
before its richer companion is designed, so the preview is shaped by experience
rather than guessed. The architecture does not change to add it -- the text and
the per-run detail are already in the `GetScreenText` response -- so nothing in
version one forecloses it.

## Open questions

1. **Adjusting a finished selection.** Version one can treat a new `Cmd`-drag as
   a fresh selection and leave it there. Shift-click-to-extend and drag handles
   are refinements that can wait, unless they prove necessary early.
2. **Whole-screen copy and the freeze.** A whole-screen `Cmd`-`C` captures the
   live frame with no freeze. If that ever feels inconsistent against the
   selection path's frozen copy, a whole-screen copy could freeze-and-flash to
   signal what it took -- but that is polish, not a decision needed now.
3. **The command names.** "Copy", "Copy Columns" and "Copy All Text" are
   placeholders. The rectangle and anywhere commands especially want names that
   read well in a menu and say what they do; worth settling when the menu is
   built and can be seen.
