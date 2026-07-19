# Reading Text From the Screen

## What this is for

A user selects part of the display and copies it. A script asks what is on the
screen. Neither should have to know how the machine is producing the picture.

This document specifies a single interface for extracting text from the
display, whatever mode the machine is in, and the strategies behind it.

It is **not** about rendering. The vector MODE 7 proposal
(`vector-mode-7.md`) concerns how a client draws teletext, uses a different
transport, and is set aside here entirely. The two share a source of data in
one narrow case and nothing else; conflating them is what produced the design
this document replaces.

## Why the current interfaces are the wrong shape

`GetTeletextScreen` reads the MODE 7 cell grid. In any other mode it returns
`active: false` plus cells describing whatever was last displayed in MODE 7 --
stale data with a flag attached saying not to trust it. That is a disclaimer
rather than a design.

It has two consequences, both bad:

- **The client is made to care which mode the machine is in.** It cannot
  reliably know: the mode can change between the client deciding what to offer
  and the user acting on it.
- **The implementation leaks into the interface.** The macOS client currently
  tells a user that copying works "only in MODE 7, where the display is made of
  characters". Someone who asked to copy the screen is being told about the
  SAA5050. That is not their problem.

The teletext-specific interfaces should be treated as scaffolding. They are
useful while a polymorphic interface is built, and some or all of them should
be expected to disappear behind it.

## What makes this hard

A tidy "the screen is R rows by C columns of characters" model is true only of
MODE 7 and of well-behaved programs in the other modes. The interface has to
survive the rest.

**There may be no text at all.** A game's display is pixels that never came
from a character. The right answer is "nothing", distinguishable from "I could
not read this".

**The screen may not be one mode.** The CRTC can be reprogrammed mid-frame.
Elite runs MODE 4 above and MODE 5 below; Revs does something similar. The
video pipeline already models this as `DisplayRegion` bands of scanlines with
differing pixel widths (`video.proto`, `FrameBuffer.hpp`), and text extraction
must follow the same banded model rather than assume one geometry per frame.

**The mode may be custom.** CRTC registers can produce character rows and
column counts that match no standard mode, or a display that is not a whole
number of character rows tall. Anything that hardcodes 40x25 or 80x32 is
wrong.

**Text need not sit on the character grid.** After `VDU 5`, the MOS writes
characters at the graphics cursor, at arbitrary pixel positions, and they may
overlap each other and existing graphics. A grid-walking extractor will miss
all of it.

**Characters may not be the MOS font, and finding the font is itself work.**
`VDU 23` redefines glyphs into RAM, so matching against the ROM font would fail
on text that is plainly readable on screen -- or worse, match the wrong
character if a redefinition collides with a ROM glyph.

Which memory holds a given character's definition depends on the font
"explode" state (Advanced User Guide 13.1.6). Imploded, the default on a Model
B, characters `&20`-`&7F` come from the OS ROM and only `&80`-`&9F` are in RAM
at `&C00`-`&CFF`. `OSBYTE &14` explodes the allocation in steps, moving further
ranges into RAM from OSHWM upwards:

| X | Characters | Memory |
|---|---|---|
| 0 | `&80`-`&9F` | `&C00`-`&CFF` (imploded default) |
| 1 | `&A0`-`&BF` | OSHWM to OSHWM+`&FF` |
| 2 | `&C0`-`&DF` | OSHWM+`&100` onwards |
| 3 | `&E0`-`&FF` | OSHWM+`&200` onwards |
| 4 | `&20`-`&3F` | OSHWM+`&300` onwards |
| 5 | `&40`-`&5F` | OSHWM+`&400` onwards |
| 6 | `&60`-`&7F` | OSHWM+`&500` onwards |

A Master, or any machine with a second processor active, is fully exploded by
default. So an extractor cannot assume where a glyph lives: it must read the
explode state and OSHWM, and fall back to the ROM for ranges still imploded.

`OSWORD &0A` reads a character's definition and resolves all of this correctly,
but it is a call into the MOS, and the extractor cannot execute guest code to
read the screen. The definitions must be assembled by reading the same memory
the MOS would.

**The screen scrolls.** Hardware scrolling changes the CRTC start address, so
screen memory order and display order differ. Extraction that reads memory
must undo this; extraction that follows the CRTC does not have to.

**MODE 7 is genuinely different.** The display is character codes rendered by
a separate chip, and the characters are recoverable exactly, with attributes,
before any pixels exist. Nothing in the other modes offers that. It is a
special case in the implementation and should stay one -- but behind the
interface, not in front of it.

## Prior art

The technique is not new, and two implementations are worth reading before
writing any.

### B-Em: `textsave.c`

"Save Screen as Text" does glyph-matching extraction from BBC bitmap modes and
ships today. `textsave_bitmap` (`textsave.c:390`) builds a 64-bit monochrome
bitmap per character cell -- collapsing 2bpp and 4bpp pixels against the OS
background mask -- then linearly scans a ~290-entry `charset[]` table for an
exact match. MODE 7 is a separate path reading screen RAM directly.

Its limitations are the interesting part, because they are exactly the cases
this document sets out to handle:

- **Geometry comes from the OS, not the hardware.** It reads `ram[0x34f]`
  (bytes per char), `ram[0x355]` (mode) and `ram[0x358]` (background mask), and
  indexes hardcoded per-mode row/column tables. That is what the MOS *thinks*
  the screen is, which is wrong for a split screen and meaningless under custom
  CRTC programming.
- **The font is transcribed into the source**, not read from the machine, so
  `VDU 23` redefinitions are invisible. The fragility shows: `textsave.c:447`
  disambiguates one glyph by reading a single ROM byte to work out whether MOS
  3.20 or 3.50 is running.
- **An unmatched cell silently becomes a space**, so "I could not read this"
  and "this was blank" are indistinguishable in the output.
- Whole screen only, to a file rather than the clipboard.

### ZEsarUX: the most complete implementation anywhere

ZEsarUX does this for the ZX Spectrum and Amstrad CPC, both of which are
always-bitmap machines with no text buffer at all -- the same problem as BBC
MODEs 0-6. It has an answer for every hard case listed above:

| Problem | ZEsarUX's approach |
|---|---|
| Matching | Linear scan of 96 glyphs, exact byte equality (`compare_char_tabla_step`) |
| **Inverse video** | Second comparison against `glyph ^ 0xFF`, reported as a flag |
| Soft fonts | Follows the system font pointer (Spectrum CHARS at 23606/7), paging-aware, with a setting to disable |
| Colour | Reduce to 1bpp by thresholding against one colour |
| **Wide-pixel modes** | Decimate N physical pixels per logical pixel back to a 1bpp 8x8 cell, and adapt the grid width per mode |
| **Grid misalignment** | Brute-force all 64 sub-cell (dx, dy) offsets against the rendered framebuffer until a glyph matches |
| Unmatched cells | Configurable: blank, `?`, or 2x2-quadrant ASCII art |

Two of those change this specification and are folded into the sections above:
inverse video, which the BBC produces routinely by swapping foreground and
background colours, and wide-pixel decimation, which MODE 2 requires.

The sub-cell offset search is also the answer to `VDU 5` text, at sixty-four
times the cost of an aligned scan. It stays out of scope here, but it is a
known-workable technique rather than an open research problem.

Worth noting ZEsarUX also has the OS-call-trap approach (hooking `RST 10H`),
and treats the two as complementary: traps for a running transcript, glyph
matching for what is on screen now.

### Everyone else

- **BeebEm** has a screen reader (`VideoGetText`, `Video.cpp:1733`) feeding a
  text view and text-to-speech, hard-limited to MODE 7. In any other mode it
  emits the string "Not in text mode."
- **VICE, DOSBox-X, Altirra, AppleWin** read hardware character buffers, which
  their machines have and the BBC's bitmap modes do not. DOSBox-X has
  drag-selection copy, but only over the VGA text buffer.
- **RetroArch's AI Service** runs general OCR on a screenshot via an external
  cloud endpoint, for game translation. No knowledge of the source font, so
  pixel-exact recovery is impossible by construction.
- **MAME, Fuse**: nothing.

### What is actually new here

Not glyph matching. What no emulator surveyed does is: choose the strategy
**per band of scanlines** from hardware state rather than per frame from OS
workspace; read the font **from the running machine** including redefinitions;
report **what it could not read** instead of silently blanking it; extract an
**arbitrary region** rather than the whole screen; and offer **drag-to-select
over the display**. All three BBC emulators route mouse input into emulation
and none has any selection interaction at all.

## Architecture: a separate library

Text recognition should not live inside the emulator.

The work divides cleanly. Knowing *where the characters are and what font is in
use* requires the running machine: the CRTC and ULA state per band, the palette,
the font explode level, whatever `VDU 23` has redefined. Deciding *which
character a bitmap is* requires none of that -- it is an image problem.

So: **a standalone library that turns images into text, given a set of glyphs.**

```
     Beebium server                       screen-text library
  ---------------------                 ----------------------
  per-band geometry                     glyph set (built-in Acorn,
  palette                     ------>    plus any supplied)
  font from RAM/ROM                     image region
  selection rectangle                   selection rectangle
                              <------   semi-structured text
```

The library knows nothing about BBC Micros beyond a built-in Acorn glyph set,
which callers can extend or replace. Beebium reads `VDU 23` redefinitions out of
RAM and passes them in as additional glyphs; another caller passes in something
else entirely.

### Why separate

- **It can be tested on its own**, against image fixtures, with no emulator, no
  ROMs and no timing. Recognition bugs are then reproducible from a PNG rather
  than from a machine state.
- **It has uses outside the emulator.** An offline image-to-text pipeline could
  read on-screen instructions from screenshots -- assembling a catalogue of key
  bindings for BBC games, say -- with no emulator involved at all.
- **It could become its own project**, which is much harder if it is entangled
  with the emulation core.

### Linked or shelled out

Both should work. The library builds as a linkable component and as a CLI
executable over the same interface.

Copying text is a low-frequency, user-initiated action -- once every few
minutes at most, never in a loop -- so the cost of spawning a process is
irrelevant next to the isolation it buys. Shelling out keeps recognition code,
and any future dependency it acquires, out of the emulation process entirely.
Linking stays available for anyone who wants it in-process.

That choice can be made later without changing either side, which is the point
of specifying the interface first.

### The library's interface

Deliberately image-shaped, with no emulator concepts in it.

**In:**

- **An image.** Logical pixels, as the framebuffer holds them. Any sensible
  format for the CLI; the linked form takes a buffer.
- **Bands.** For each horizontal band of the image: the character cell size
  (8x8 for every BBC mode, but not assumed), where the grid starts, and how
  many colours a pixel can take. A single-band call covers the ordinary case.
- **A selection rectangle**, in image pixels. Optional; whole image otherwise.
- **Glyph sets.** Zero or more, each a list of (character code, 8-byte
  bitmap). Built-in Acorn sets are available by name; supplied sets are added
  to or replace them. This is how `VDU 23` redefinitions arrive, and how a
  caller with an entirely different machine's font uses the same tool.
- **Options.** Whether to try inverted glyphs, what to do with cells that
  match nothing, whether to search sub-cell offsets.

**Out: semi-structured text.** Not a bare string -- the caller needs to know
what was uncertain and where things were:

- The text itself, for the common case of wanting to paste it.
- Per run: the characters, the rectangle they occupied, and which glyph set
  matched.
- Per cell where it matters: unmatched, or matched inverted, or matched at a
  sub-cell offset.

A caller that only wants text ignores everything else. A caller building a
catalogue of on-screen instructions from thousands of screenshots wants the
uncertainty, because it decides what to review by hand.

### MODE 7 does not go through it

Teletext characters are known exactly, before pixels exist. Sending them
through image recognition would be converting information into a picture in
order to guess it back. The teletext strategy stays in the emulator and never
calls the library -- the special case the interface hides.

## The interface

This is the emulator-facing interface, used by clients. The library's own
interface is separate and described above.

The client selects **in pixels** and the server returns **text plus where it
found it**. That is the whole idea: a pixel rectangle is something a client can
always produce from a drag without knowing anything about modes, grids, or cell
sizes.

```protobuf
rpc GetScreenText(GetScreenTextRequest) returns (ScreenText);

message GetScreenTextRequest {
    // Region of the display to read, in frame pixel coordinates. Whole display
    // when unset.
    optional PixelRegion region = 1;

    // How to join what is found into `text`.
    ScreenTextLayout layout = 2;
}

message ScreenText {
    // False when no strategy can read this display. Distinct from finding
    // nothing: a graphics-only screen is readable and simply contains no text.
    bool supported = 1;

    // Why, when unsupported. For diagnosis, not for display to a user.
    string unsupported_reason = 2;

    // What was found, in reading order.
    repeated TextRun runs = 3;

    // The runs joined into text according to `layout`.
    string text = 4;

    uint64 frame_number = 5;
}

// A contiguous piece of text found on screen, with where it was found, so a
// client can highlight exactly what it captured.
message TextRun {
    string text = 1;
    PixelRegion bounds = 2;

    // Cell geometry this run was read with, so a client can snap a selection
    // to character boundaries. Zero when the run is not cell-aligned.
    uint32 cell_width = 3;
    uint32 cell_height = 4;

    // Cells whose glyph could not be identified, as a fraction of the run.
    // Zero for MODE 7, where characters are read rather than recognised.
    float unknown_fraction = 5;
}
```

The client never learns the mode. It gets back what was read and the geometry
that was used, which is enough to draw a selection that snaps sensibly without
knowing why the cells are that size.

### Why not a grid in the request

An earlier sketch had the client select in character cells, with the server
reporting the grid dimensions. It does not survive the cases above: there is no
single grid on a split screen, no grid at all for `VDU 5` text, and no
guarantee a custom mode has a sensible one. Pixels are the only coordinate
system every case shares.

## Strategies behind the interface

The server chooses per scanline band, using the CRTC and Video ULA state that
was in effect when those lines were rendered.

### Teletext (SAA5050 active)

Read the character codes. Exact, attribute-aware, and already implemented as
`TeletextGrid`. No recognition is involved and `unknown_fraction` is always
zero.

### Bitmap modes, cell-aligned text

**Verified before this was specified.** A character cell on a real MODE 4
screen matches its font glyph byte for byte, and the MOS 1.20 font sits at
`&C000` with eight bytes per character from character 32 -- `(c - 32) * 8`
predicts every glyph checked. So the matching is exact, and nothing in this
design needs approximate comparison.

Delegated to the library described above; what follows is what the emulator
must supply and what the library then does.

Beebium supplies: the image band in logical pixels, the cell geometry and grid
origin from the CRTC, the palette so foreground can be told from background,
and the glyph set assembled from the OS ROM plus whatever `VDU 23` has
redefined at the current explode level.

Not OCR. Text written through the VDU drivers uses font glyphs unmodified at
cell-aligned positions, so a cell either matches a known glyph exactly or does
not match at all:

1. Build a lookup from glyph bitmap to character, from the font actually in
   use -- the soft font in RAM where `VDU 23` has redefined characters, the ROM
   font otherwise.
2. For each character cell in the band, hash its pixels and look it up.
3. A hit yields a character. A miss yields an unknown cell, counted in
   `unknown_fraction`, never a guess.

This degrades honestly. A cell a program has drawn over does not match, and is
reported as unknown rather than as a plausible wrong character.

Colour complicates the hash: the same glyph in different colours produces
different pixels. The bitmap must be reduced to a foreground/background mask
before hashing, which needs the palette in effect for that band.

**Inverse video must be matched too.** The BBC routinely prints text with
foreground and background swapped, which inverts the cell relative to the
glyph. Following ZEsarUX, compare against both the glyph and its complement,
and record which matched -- a run of inverse text is worth knowing about even
if it copies as ordinary characters.

**Decimation is not needed here**, although ZEsarUX requires it. Beebium's
framebuffer holds *logical* pixels and the client stretches them to physical
ones -- that is what `DisplayRegion.pixel_width` records, 320 for Elite's upper
band and 160 for its lower. So a matcher never sees a stretched pixel.

That leaves a strong invariant: **in logical pixel space a BBC character cell
is 8x8 in every mode.** MODE 0 is 640 pixels over 80 columns, MODE 2 is 160
over 20, MODE 5 is 160 over 20 -- eight pixels per character throughout. The
glyph to match is always an 8x8 bitmap, and only the cell's colour depth and
the grid width vary.

A glyph straddling a band boundary is not matched. Text written across the seam
of a split screen is rare enough not to pay for.

### Text at the graphics cursor

`VDU 5` text is not cell-aligned, so the cell walk misses it. Finding it means
searching for glyph-shaped pixel patterns at arbitrary offsets: ZEsarUX brute
-forces all sixty-four sub-cell positions, which works and costs what it
sounds like it costs.

**In scope from the outset, but opt-in.** It is reached through free-form
selection (see below) rather than being attempted on every copy, so the cost
falls only on callers who ask for it. Such text appears as runs whose bounds
are not cell-aligned, with the cell geometry fields zero.

### Nothing readable

A band with no matching glyphs produces no runs. `supported` stays true: the
display was read and contains no text.

## What capture must record

Split screens mean mode information cannot be per-frame. To know how to read a
band, the extractor needs, for each band of scanlines: the character cell
geometry, the pixels-per-byte from the Video ULA, the palette, and whether the
SAA5050 was driving those lines.

The pixel pipeline already does exactly this shape of bookkeeping for a
related reason. `FrameRenderer` records a pixel width per scanline in
`scanline_pixel_widths_`, then compresses equal runs into the
`FrameDisplayRegion` bands the `Frame` message carries
(`FrameRenderer.hpp:355-382`). Extraction needs the same per-scanline record
with more in it -- character cell geometry, pixels per byte, the palette, and
whether the SAA5050 was driving -- and the same compression into bands.

So the mechanism exists and the work is to widen what it records, not to
invent it. Whether the extra fields ride on `FrameDisplayRegion` or a parallel
structure is open: the video path has no use for them, and `Frame` is streamed
fifty times a second to every client, so a parallel record read only on demand
is probably better.

## Selection

Selection is not only a visual affordance: **the mode the user selects in
determines which search the server runs.** That coupling is what keeps the
expensive search off the common path.

### Snapped (the default)

The dragged rectangle snaps to character cells, using the cell geometry the
CRTC implies for the band under the pointer. Only cell-aligned glyphs are
searched, which is the fast, exact, well-structured case, and the one that
serves reading a BASIC listing or an error message.

Within snapped selection there are two shapes, as in a text editor:

- **Rows** -- the selection follows reading order. The first row runs from the
  start point to the right edge, whole rows follow, and the last runs from the
  left edge to the end point. What ordinary text selection does.
- **Rectangle** -- the same column range is taken from every row, as in a
  block or column selection. What a table of figures wants.

These decide *which cells are selected*, and are distinct from how the selected
cells are then joined into text (see layout, above). A rectangle selection
naturally joins as separate lines; a row selection naturally flows. The two
concepts are kept apart because they can be combined independently, even if the
UI only ever offers sensible pairings.

### Free-form

The rectangle is not snapped, and the search additionally looks for text that
is not on the character grid -- `VDU 5` output, or anything in an offline image
that was never grid-aligned.

This is slower, by up to the sixty-four sub-cell alignments a thorough search
implies, and yields less structure: without a grid there are no rows and no
columns, only runs at positions. Rectangle-versus-rows does not apply.

Free-form is therefore **opt-in**, not because unaligned support is an
afterthought -- it is specified from the outset and the library implements it
early -- but because a user dragging over a BASIC listing should not pay for a
search they do not need.

### What this forces: geometry before the drag ends

Snapping has to happen *during* the drag, so the client needs the cell geometry
before it has anything to send. Returning it with the extracted text, as the
response does, is too late.

So a client needs to ask for the current grid geometry -- cell size, grid
origin, and the bands they apply to -- when a drag begins. One call on
mouse-down is ample; this does not want to ride on every frame.

Note what this does and does not leak. The client learns that cells are a
certain size at a certain origin, which is precisely what it needs to draw a
snapping overlay. It still learns nothing about *which mode* produced that
geometry, and needs no per-mode knowledge to use it.

### Deferred

Which modifier keys select rectangle versus rows, and how free-form is
reached -- a modifier, a menu item, a preference -- is UI design and is not
settled here. The requirement is that all three are reachable and that snapped
rows is the default.

## The client's part

- Convert a drag to frame pixel coordinates. The renderer already does this
  mapping in reverse to draw, including the Display Style geometry.
- Send the rectangle; receive text and runs.
- Optionally snap the visible selection using the returned cell geometry, after
  the fact rather than before.

Selection state belongs above the Display Style system, not inside it --
`display-styles.md` warns against a Cartesian product of options, and a
selection overlay composes with any style.

## What happens to the teletext interfaces

`GetTeletextScreen`, `TeletextGrid` and `teletext_text()` were built for
whole-screen copy in MODE 7. Under this design:

- The **capture** remains, as the teletext strategy's implementation.
- The **RPC** is scaffolding. Once `GetScreenText` exists, copy should use it,
  and `GetTeletextScreen` should be expected to go, unless something else needs
  attribute-rich teletext cells for reasons of its own.
- `teletext_text()` becomes an internal detail of the teletext strategy rather
  than a public conversion.

The client wrappers (`Video.teletext_screen()`, `video.teletextScreen()`)
should be treated the same way: useful now, not a commitment.

## Open questions

1. **Does `text` need a layout choice at all, given runs are returned?** A
   client that wants to join runs its own way already has what it needs. The
   server-side join exists so simple clients do not reimplement it, and so all
   clients agree -- but Rows versus Flowed may be better expressed as a
   property of a run (did this line reach the edge?) than as a request option.
2. **What is a "reading order" across bands?** Top to bottom is obvious for
   stacked bands. Two side-by-side text areas within a band are not obviously
   orderable, though nothing on a BBC produces those today.
3. **Should unknown cells occupy their column in `text`?** A space preserves
   alignment, which is why control codes copy as spaces in MODE 7. A
   placeholder would be more honest about what was not read. Alignment probably
   wins, with `unknown_fraction` carrying the honesty.
4. **Where is the explode state held?** The allocation table above says which
   memory a character's definition occupies for a given explode level, but the
   level itself and OSHWM have to be read from OS workspace, and those
   locations need pinning down per MOS version. A program that redefines a
   glyph part-way down the screen also defeats a single font table for the
   frame -- the same code could legitimately mean two different characters in
   the top and bottom halves.
5. **Does a caller ever want the cells rather than the text?** Drag-select as
   specified does not: it sends pixels and receives text. Attribute-aware cell
   access may still be wanted by automation, and if so it should be a separate
   deliberate interface rather than a side effect of copy.

## Work partition

Four streams. Two can start immediately and in parallel; the other two follow.

```
  (1) Library  ────────────────┐
      independent              │
                               ├──> (2) Integration
  (3) Server APIs  ────────────┘         bitmap modes light up
      teletext strategy only
            │
            └──> (4) macOS GUI
                     selection + copy
```

### 1. The library

Turns images into text given glyph sets. Depends on nothing else in this
repository. Specified as a work order in `screen-text-library-spec.md`.

Lives here for now, as its own component with its own build targets -- a
linkable library and a CLI over the same interface. Naming can wait until there
is a reason to extract it.

**One constraint matters more than the rest: the library must not depend on the
emulator core.** Dependencies point one way, Beebium to library, never back. A
single `#include <beebium/...>` for convenience would quietly make the
extraction this design is arranged around impossible.

Order: interface and Acorn glyph set, then exact matching of aligned cells,
then inverted matching, then supplied glyph sets, then sub-cell offset search.

Tested against image fixtures -- images in, expected text out -- with no
emulator, no ROMs and no timing. A recognition bug is then reproducible from a
file, which is the main practical reason for the split.

### 2. Integration with the servers

Where bitmap modes start working. Needs the library, and needs emulator-side
work that has nothing to do with recognition:

- Widen the per-scanline record to carry what a band needs: cell geometry,
  colour depth, palette, whether the SAA5050 was driving.
- Assemble the glyph set from the OS ROM plus `VDU 23` redefinitions at the
  current explode level, which means reading OSHWM and the explode state.
- Call the library, in-process or by spawning it.

Tested by booting real machines, running programs that print in MODEs 0-6, and
asserting on the text that comes back -- the integration counterpart to the
library's fixtures.

### 3. Server APIs

`GetScreenText`, and the geometry call a client needs on mouse-down to snap a
drag.

**Startable now**, with the teletext strategy only, delegating to the capture
already built. That alone is an improvement: MODE 7 behaviour is unchanged, and
every other mode stops returning stale cells with a disclaimer attached and
starts returning an honest "nothing found".

The design succeeds or fails on one property, worth stating as a test of it:
**when stream 2 lands, no client changes.** `GetScreenText` starts returning
text where it previously returned nothing -- no new API, no version
negotiation, no client-side branching on mode. If that turns out not to hold,
the interface is wrong, and it is better to discover that now with one strategy
than later with two.

Also in this stream: retiring `GetTeletextScreen` and its client wrappers once
copy no longer uses them, and the Python and TypeScript screen scrapers once
the API supersedes them.

### 4. macOS front end

Drag-to-select over the display, in the three modes: snapped rows (the
default), snapped rectangle, and free-form. Copy moves onto `GetScreenText`,
and the MODE 7 wording comes out of the UI.

The selection overlay composes with any Display Style rather than being one --
`display-styles.md` warns against a Cartesian product of options.

Follows stream 3, though the overlay and its geometry can be built against a
stub. Selection state, coordinate mapping and snapping are testable without a
window; the drag itself needs eyeball verification, as the Display Style work
found.

### What is deliberately not partitioned

`VDU 5` support is not a fifth stream. It is the last item of stream 1 and the
free-form path of stream 4, and it arrives when both are ready rather than
being scheduled as a project of its own.
