# Build Specification: Screen Text Server APIs

A work order for stream 3 of `screen-text-extraction.md`. Read that document
and its "What stream 1 changed" section first; this one says what to build on
the server and does not repeat the why.

> **Amended after delivery (July 2026).** The `ScreenTextSearch` enum below is
> shown with three values (`BOTH`, `ALIGNED`, `OFFSET`). It was reduced to two,
> `ANYWHERE` and `ALIGNED`, once it was clear a caller should choose one search
> up front rather than ask for both and merge -- see "Two searches, chosen up
> front" in `screen-text-extraction.md`. The seam, the clients and the tests
> were updated to match. Read the enum below as historical; the shipped proto
> is the authority.

## What you are building

Two gRPC calls on the emulator, and their Python and TypeScript client
wrappers:

- `GetScreenText` -- read text from the display, whatever the mode, choosing a
  strategy per band of scanlines.
- `GetScreenGeometry` -- report the character grid a client needs to snap a
  drag, before it has anything to send.

You are also building the **strategy seam**: the structure by which
`GetScreenText` dispatches a band to whatever can read it. In this stream only
the teletext strategy exists. Stream 2 will add the bitmap strategy behind the
same seam, and the whole design is judged on that landing **without changing
this API or any client**.

You are **not** reading bitmap-mode text. That is stream 2. A bitmap band here
reports that it could not be read. When stream 2 arrives it starts returning
text through the same call, and nothing you write now changes.

## The property this stream must protect

State it to yourself before every decision: **when stream 2 lands, no client
code changes and no proto changes.** `GetScreenText` starts returning text for
bitmap screens where it returned "nothing readable" before. If any choice here
would force a client edit or a proto edit when the bitmap strategy is added,
that choice is wrong.

That is why the fields for bitmap's uncertainty exist from the start, always
zero for teletext, and why the request already carries a search mode teletext
ignores.

## The proto

Add to `video.proto`. The wire types generate into namespace `beebium`
alongside the core types, so a name that collides with a core type will not
compile -- as happened in stream 1's teletext work, which is why those wire
types are `TeletextScreenCell` and not `TeletextCell`. Check each new name
against the core before settling it, and prefix with `Screen` where in doubt.

```protobuf
service VideoService {
    // ... existing ...
    rpc GetScreenText(GetScreenTextRequest) returns (ScreenText);
    rpc GetScreenGeometry(GetScreenGeometryRequest) returns (ScreenGeometry);
}

message PixelRegion {
    uint32 x = 1;
    uint32 y = 2;
    uint32 width = 3;
    uint32 height = 4;
}

enum ScreenTextSearch {
    // Whole screen and scripts want everything; the default is the most
    // inclusive. A snapped drag sets ALIGNED; free-form sets BOTH.
    SCREEN_TEXT_SEARCH_BOTH = 0;
    SCREEN_TEXT_SEARCH_ALIGNED = 1;
    SCREEN_TEXT_SEARCH_OFFSET = 2;
}

enum ScreenTextLayout {
    SCREEN_TEXT_LAYOUT_ROWS = 0;     // each grid row its own line (default)
    SCREEN_TEXT_LAYOUT_FLOWED = 1;   // rejoin a line that wrapped at the edge
}

message GetScreenTextRequest {
    optional PixelRegion region = 1;   // whole display when unset
    ScreenTextSearch search = 2;
    ScreenTextLayout layout = 3;
}

// A contiguous piece of text and where it was found, so a client can highlight
// exactly what it captured.
message ScreenTextRun {
    string text = 1;
    PixelRegion bounds = 2;

    // The character cell geometry this run was read with, so a selection can
    // snap to it. Zero when the run is not cell-aligned (off-grid text).
    uint32 cell_width = 3;
    uint32 cell_height = 4;
}

message ScreenText {
    // True when at least one band in the requested region has a strategy that
    // could read it. False for a display this build cannot read text from --
    // which today is every non-MODE-7 display, and stops being so when the
    // bitmap strategy lands. Distinct from readable-but-empty: a graphics
    // screen the bitmap strategy read and found no text in is supported=true,
    // runs empty.
    bool supported = 1;

    repeated ScreenTextRun runs = 2;

    // The runs joined into text by `layout`, for a client that wants a string
    // and not structure.
    string text = 3;

    // Cells a strategy tried to read and could not identify at all. Always
    // zero for teletext, whose cells are exact character codes. The bitmap
    // strategy populates it. See "Two kinds of uncertainty" below.
    uint32 unreadable_cells = 4;

    // Cells a strategy read but could not pin to one character, because a font
    // draws two identically. Also always zero for teletext.
    uint32 ambiguous_cells = 5;

    uint64 frame_number = 6;
}

message GetScreenGeometryRequest {}

// The character grid for one band of scanlines, in display pixel coordinates.
message ScreenBandGeometry {
    uint32 top = 1;            // first scanline, inclusive
    uint32 bottom = 2;         // one past the last
    uint32 cell_width = 3;
    uint32 cell_height = 4;
    uint32 column_pitch = 5;   // cell-to-cell step across; cell_width when equal
    uint32 row_pitch = 6;      // and down; cell_height when equal
    uint32 origin_x = 7;       // where the grid starts
    uint32 origin_y = 8;
}

message ScreenGeometry {
    repeated ScreenBandGeometry bands = 1;
    uint64 frame_number = 2;
}
```

Improve the shape where it is clumsy, but do not remove a field because
teletext does not use it -- those fields are the seam for stream 2, and their
absence is what would force a proto change later.

## GetScreenText

Dispatch per band, collect runs, merge, linearise, return.

1. **Determine the bands** in the requested region. A band is a run of
   scanlines sharing one character geometry. The pixel pipeline already tracks
   scanline bands by pixel width (`FrameRenderer`, `FrameDisplayRegion`); a
   split screen is more than one band, an ordinary screen is one.
2. **For each band, pick a strategy.** The teletext strategy applies when the
   SAA5050 was driving those lines; nothing else applies yet. A band with no
   applicable strategy contributes no runs and marks the result unreadable for
   that region (see `supported`).
3. **The teletext strategy** reads the `TeletextGrid` -- it does not go through
   the screen-text library, and must not. Teletext characters are known
   exactly before pixels exist; sending them through image recognition would be
   converting information to a picture to guess it back. Reuse the existing
   `teletext_text()` and the grid snapshot. Its runs are the grid rows,
   cell-aligned, with teletext cell geometry.
4. **Merge** is concatenation in reading order: bands top to bottom, runs
   within a band by baseline then x. Bands do not overlap and the teletext
   strategy produces no overlapping runs, so there is nothing to dedupe. Stream
   2 must preserve that -- do not leave room for it to introduce overlaps.
5. **Linearise** into `text` by `layout`, exactly as the current
   `GetTeletextScreen` does.

The `search` field is honoured by the bitmap strategy, not teletext -- teletext
is always the grid. Accept it, record it, ignore it for now.

## GetScreenGeometry

Report the character grid per band, in display pixels, so a client can snap a
drag on mouse-down without waiting for a read.

Every mode has a grid, whether or not text is on it: MODE 7 is 40x25, the
bitmap modes are 8-pixel cells on the standard geometry, and MODE 3 and MODE 6
are an 8-scanline cell on a 10-scanline pitch (`row_pitch = 10`, the two spare
lines blanked -- the trap stream 1 documented). Report the geometry the CRTC
implies even for a band the text strategy cannot yet read: snapping is about
where the cells are, reading is about what is in them, and they are separate
questions.

This is the one place stream 3 needs geometry the pixel pipeline does not
already expose. Derive it from the CRTC and mode state. If that turns out to
need the per-band record widening that stream 2 also needs, widen it here and
let stream 2 build on it -- but do not pull glyph-set assembly or colour-depth
tracking forward; those are recognition, which is stream 2.

## Two kinds of uncertainty

Stream 1 established that a bitmap cell can be unreadable (matched no glyph) or
ambiguous (matched more than one, because a font draws two characters the
same). These are different problems for a caller and both surface, as
`unreadable_cells` and `ambiguous_cells`.

For this stream both are always zero, because teletext cells are exact codes.
Define the fields, wire them to zero, and let stream 2 populate them. In `text`,
when stream 2 comes: an unreadable cell contributes a space, preserving column
alignment; an ambiguous cell contributes its best candidate, because it is a
character, only an uncertain one. The alternatives, if a caller wants them, are
a later structured concern, not part of this string.

## Settled policy, so the agent does not have to invent it

- **The layout choice stays**, server-side, defaulting to Rows. Runs carry
  positions so a sophisticated client can re-linearise, but the string exists
  so simple clients need not, and so all clients agree.
- **Reading order** is bands top to bottom, runs within a band by baseline then
  x. Two side-by-side text columns within one band are not orderable, but
  nothing on a BBC produces them; do not solve that.
- **`GetScreenText` returns runs, not a cell grid.** Attribute-rich per-cell
  access, if ever wanted by automation, is a separate deliberate interface, not
  a side effect of a copy call.
- **`supported` is per-request, not per-band.** True if any band in the region
  has a working strategy. A split screen with a MODE 7 band reads that band and
  reports supported even while its bitmap band contributes nothing.

## Clients

Add `get_screen_text` / `getScreenText` and `get_screen_geometry` /
`getScreenGeometry` to the Python and TypeScript clients, in the shape of the
existing `teletext_screen` wrappers. Return the runs and the counts, not only
the string -- a script wanting to know what was uncertain needs them.

Do **not** retire `GetTeletextScreen` or the `teletext_screen` wrappers in this
stream. Copy still uses them until stream 4 moves it to `GetScreenText`.

(Postscript, 2026-07: in the event they were not retired at all. Once copy moved
to `GetScreenText`, the RPC's text role was redundant but its per-cell
attributes had no other home, so it was kept and repositioned as the teletext
attribute API. See `../video-subsystem.md` and `screen-text-extraction.md`.)

## The build trap you will hit

Changing `video.proto` moves the protocol fingerprint, and every binary that
carries it must be rebuilt together or the GUI refuses to connect with a
mismatch:

1. `uv run --project clients/beebium-python-client python scripts/sync_protocol_fingerprint.py`
2. `cmake --build build --target beebium-servers` -- **all four** variants, not
   just model-b.
3. Regenerate stubs: Python `scripts/generate_proto.sh`, TypeScript
   `npm run generate-protos`, Swift `protoc ... video.proto` if you touch the
   Swift side (you should not need to this stream).

Rebuilding only model-b leaves the other three servers stale, and the failure
shows only when someone starts a B+ -- looking like an unrelated bug.

## Verification

End to end through the Python client, the way `GetTeletextScreen` was verified:

- Boot a MODE 7 machine, call `GetScreenText`, assert it reads the boot banner
  -- `supported` true, the text present, `unreadable_cells` and
  `ambiguous_cells` zero.
- Boot a machine in MODE 4, print something, call `GetScreenText`, assert
  `supported` **false** and no runs -- honest "cannot read this yet" rather
  than stale teletext cells. This assertion inverts when stream 2 lands, which
  is the proof the seam works.
- `GetScreenGeometry` in MODE 7 reports one 40x25 band; in MODE 4 reports the
  bitmap grid; in MODE 6 reports `row_pitch = 10`.
- A split-screen program (Elite-like, or a hand-built MODE 4/MODE 5 split)
  reports more than one band from `GetScreenGeometry`.

Unit-test the merge and linearisation off a synthetic set of runs, without a
machine, so the reading-order and layout rules are pinned without booting
anything.

## Conventions

C++20; test first; Catch2 run as built binaries; 7-bit ASCII in source with
non-ASCII as UTF-8 byte escapes; `_filepath`/`_dirpath` suffixes; comments
describe the present; British spelling in prose; proto-change build trap above;
commit frequently; never push; no AI attribution in commit messages.

## Done means

- `GetScreenText` and `GetScreenGeometry` on `VideoService`, with the strategy
  seam and the teletext strategy behind it.
- All four servers and the app rebuilt, fingerprints matching.
- Python and TypeScript wrappers, with tests.
- The MODE 4 `supported == false` assertion in place, written so it inverts
  when the bitmap strategy lands.
- A summary of what you built, any proto shape you changed and why, and a plain
  statement of whether the "no client change when stream 2 lands" property
  holds as far as you can see -- and if you doubt it anywhere, say where.
