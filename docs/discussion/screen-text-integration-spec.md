# Build Specification: Bitmap-Mode Text Integration

A work order for stream 2 of `screen-text-extraction.md`: make `GetScreenText`
read text in the bitmap modes (0-6), by putting a bitmap strategy behind the
seam stream 3 built and feeding it from the running machine.

Read `screen-text-extraction.md` first -- its "What stream 1 changed" and
"Strategies behind the interface" sections especially. This document says what
to build and does not repeat the why.

## What you are building

The **bitmap strategy**: the branch of `read_band` that reads a band the
SAA5050 was *not* driving, by recognising glyphs in its pixels. Everything else
in the pipeline exists already.

When this lands, `GetScreenText` starts returning text for MODEs 0-6 through the
same API stream 3 shipped -- **no proto change, no client change**. That is the
property the whole design was arranged around; if any step here seems to need a
proto or client edit, stop and say so, because it means something is wrong.

## The two things that already exist, and how they meet

**The seam** (`src/core/include/beebium/ScreenText.hpp`, namespace
`beebium::screen`): `bands_of(FrameMetadata) -> read_band -> concatenate_bands_readings
-> linearise`. `read_band` dispatches per band; today, only `is_teletext` bands
are read, everything else returns `supported = false`. You add the `else`.

**The library** (`src/screen-text/`, namespace `screentext`): turns an image
plus band descriptors plus glyph sets into text. Its interface:

```cpp
screentext::Result screentext::read(
    const Image& image,                    // one byte per pixel, equal == same colour
    const std::vector<Band>& bands,        // cell geometry in image coordinates
    const std::vector<GlyphSet>& glyph_sets,  // later sets override earlier
    const Options& options);               // { Search::AlignedOnly | OffsetOnly, selection }
```

`Result` carries `runs` (each a row of `Cell`s with codepoints, the two colours,
`alternatives` for ambiguity, and pixel `bounds`), plus `unmatched_cells` and
`ambiguous_cells`. Matching is exact byte comparison; the library never guesses.

**The bitmap strategy is the adapter between them.** For a non-teletext band it:

1. builds a `screentext::Image` from the band's rendered pixels;
2. builds a `screentext::Band` (image coordinates) from the core `beebium::screen::Band`
   (scanline coordinates) -- the geometry carries over directly, cell size,
   pitch and origin;
3. calls `screentext::read` with the glyph set assembled from the machine;
4. maps the `screentext::Result` back to a `beebium::screen::BandReading` --
   runs to `TextRun`s, `unmatched_cells`/`ambiguous_cells` to
   `unreadable_cells`/`ambiguous_cells`, `supported = true`.

## Extending read_band's inputs

`read_band` today takes the teletext snapshot, which is all the teletext
strategy needs. The bitmap strategy needs two more things the signature does not
carry: the **rendered pixels** and the **glyph set**. Bundle them.

Introduce a sources context -- the teletext snapshot, the frame image (or a view
of it), and the assembled glyph set -- and pass it to `read_band` in place of
the bare snapshot. Assemble the image and the glyph set **once per
`GetScreenText` call**, not per band, and hand the same context to every band.
This is a seam change, but a purely internal one: `read_band`'s callers are all
in `beebium::screen` and the service, none of it on the wire.

## The pixels

The framebuffer holds **logical** pixels -- the client does the stretch to
physical -- so a character cell is 8x8 logical pixels in every mode, and no
decimation is needed. Bitmap modes output discrete palette colours with no
blending (the gamma blend is teletext-only), so each pixel is exactly one of the
mode's colours.

The library's `Image` is one byte per pixel where **equal bytes mean the same
colour**. So map the band's rendered pixels to small colour ids: assign each
distinct pixel value an id and fill the `Image`. The library cares only about
equality and reduces each cell to its two colours, so any consistent assignment
works -- you do not need the palette, only "same or not same". A cell of three
or more colours is left unmatched by the library, which is the honest limit on a
busy background.

## The glyph set -- the BBC-specific depth

This is the part with real archaeology in it. Assemble the glyph set the machine
would actually be drawing with, in this order of precedence (later wins):

1. **The ROM font.** Start with the library's built-in Acorn set
   (`builtin_glyph_set`), which is MOS 1.20's font, characters 32-126. Correct
   for the common case. Reading the font from the *actual* running MOS -- for a
   different OS ROM, or a Master -- is a refinement; note it as a known gap
   rather than solving it first.
2. **The soft font.** `VDU 23` redefines glyphs into RAM, and those must be read
   and supplied as an overriding glyph set, or redefined text reads as garbage
   or, worse, as the wrong character.

Where a redefinition lives depends on the font **explode** state (Advanced User
Guide 13.1.6, reproduced in `screen-text-extraction.md`): imploded (the Model B
default) puts only 128-159 in RAM at `&C00`-`&CFF`; `OSBYTE &14` moves further
ranges to OSHWM upwards; a Master or a second-processor machine is fully
exploded. You cannot call `OSWORD &0A` to resolve this -- that is guest code an
extractor cannot run -- so read the same memory the MOS would: the explode state
and OSHWM from OS workspace, then the definition bytes. **Pin the workspace
locations down for the MOS versions Beebium ships and cite them**; a program
that redefines a glyph part-way down the screen defeats a single per-frame font
table, which is a known limitation to record, not to solve now.

Because matching is exact and the library never guesses, a redefined character
you have *not* supplied is declined -- reported unread -- rather than mistaken
for a ROM glyph. So an incomplete soft-font read degrades honestly.

## Reading guest memory

The soft font means the screen-text path now needs read access to guest RAM,
which `VideoService` does not have today (it has the framebuffer and the
teletext grid). Wire that access through -- the machine's memory is reachable
the way the debugger service reaches it. Read-only, and only at
`GetScreenText` time.

## Composing ANYWHERE

The server's two searches map onto the library's disjoint passes:

- `beebium::screen::Search::Aligned` -> one `screentext::read` with
  `Search::AlignedOnly`.
- `beebium::screen::Search::Anywhere` -> `AlignedOnly` **and** `OffsetOnly`, the
  two `Result`s concatenated: runs appended, counts summed. The library builds
  the passes to be disjoint (the off-grid pass finds grid text, uses it to
  suppress ghosts, then drops it), so this is concatenation with nothing to
  reconcile, and `Anywhere` is a strict superset of `Aligned`.

Keep `concatenate_bands_readings` a pure concatenation. The composition of the
two passes happens inside the strategy, for one band, and produces one
`BandReading`.

## Acceptance -- a test that already exists, written to invert

`clients/beebium-python-client/tests/test_screen_text.py` has
`test_mode_4_reports_that_it_cannot_be_read`, which today asserts a MODE 4
display returns `supported = false` and no runs, with a comment saying it is
written to invert. **Rewrite it** to assert MODE 4 now reads its text -- that
flip is the definition of done. The comment tells you how.

Then cover the modes properly, by booting real machines and asserting on what
comes back -- the integration counterpart to the library's image fixtures:

- Each of MODEs 0-6 prints known text and `GetScreenText` returns it.
- A `VDU 23`-redefined character reads correctly once its soft-font glyph is
  supplied, and is declined (not guessed) when it is not.
- `VDU 5` text off the grid is read under `ANYWHERE` and not under `ALIGNED`.
- A split screen reads each band with its own geometry.
- The uncertainty counts are populated: a partly-drawn-over cell is unreadable,
  a font collision is ambiguous.
- MODE 7 still reads exactly as before -- the teletext strategy is untouched.

## Constraints

- **No proto change, no client change.** The whole point. If you reach for one,
  the design is telling you something -- report it.
- The teletext strategy and its tests stay exactly as they are.
- Do not weaken the library's "never guess": an unread cell is a space plus a
  record that it was unread, never a plausible wrong character.
- The library must not gain a dependency on the emulator. The adapter lives on
  the emulator side and calls into the library, never the reverse.

## Conventions

C++20; test first; Catch2 run as built binaries; 7-bit ASCII in source with
non-ASCII as UTF-8 byte escapes; `_filepath`/`_dirpath` suffixes; comments
describe the present; British spelling in prose; commit frequently; never push;
no AI attribution in commit messages. Targeted test runs, not the whole suite.

Note: this stream changes no `.proto`, so there is no fingerprint sync and no
four-server rebuild -- unless you find you must widen the per-band record beyond
what stream 3 already exposes, which would touch the core but still not the
wire.

## Done means

- The MODE 4 assertion inverted and passing; MODEs 0-6 read real text booted
  from real machines.
- Soft-font redefinitions read and honoured; unsupplied ones declined, not
  guessed.
- `ANYWHERE` a strict superset of `ALIGNED`, composed from the library's two
  passes.
- MODE 7 unchanged; no proto or client edit.
- A summary of what you built, where you had to read OS workspace and for which
  MOS versions, what you left as a named gap (the ROM-font-from-machine
  refinement, mid-screen redefinition), and anything you could not verify.
