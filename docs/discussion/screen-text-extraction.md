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

## The interface

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

**Low-resolution modes need decimating first.** In MODE 2 a logical pixel
occupies several physical ones, so a cell must be reduced back to an 8x8
monochrome bitmap before matching, and the character grid is narrower. The
per-band record has to carry enough to do that.

### Text at the graphics cursor

`VDU 5` text is not cell-aligned, so the cell walk misses it. Finding it means
searching for glyph-shaped pixel patterns at arbitrary offsets, which is a
different and much more expensive problem.

Out of scope initially. The interface accommodates it -- such text would appear
as runs whose bounds are not cell-aligned, with `cell_width` and `cell_height`
zero -- so adding it later does not change the contract.

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

## Suggested sequencing

1. Specify and add `GetScreenText` with the teletext strategy only, delegating
   to the existing capture. Behaviour in MODE 7 is unchanged; behaviour
   elsewhere becomes an honest "nothing found" rather than stale cells.
2. Move macOS Copy onto it, and remove the MODE 7 wording from the UI.
3. Drag-to-select against this interface, in pixels.
4. Add the bitmap-mode glyph-matching strategy, ROM font first, soft font
   after.
5. Retire the teletext-specific RPC and the client-side screen scrapers.

`VDU 5` text, if ever, comes last and changes nothing about the contract.
