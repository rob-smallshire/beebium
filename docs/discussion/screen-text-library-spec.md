# Build Specification: Screen Text Library

A work order for stream 1 of `screen-text-extraction.md`. Read that document
first for why this exists and how it fits; this one says what to build.

## What it is

A library that turns images into text, given sets of glyphs.

It knows nothing about BBC Micros beyond a built-in Acorn glyph set that
callers may extend or replace. It does not know what an emulator is, what a
CRTC is, or which screen mode produced its input. Given a picture and some
glyphs, it says what characters it can see and where.

## Hard constraints

**1. No dependency on the emulator.** The library must not include, link
against, or otherwise reference `beebium_core`, the service layer, or anything
else in this repository outside its own directory. Dependencies point one way:
Beebium may use the library, never the reverse. A single `#include
<beebium/...>` for convenience would forfeit the ability to extract this as a
separate project later, which is the point of building it this way.

If the library appears to need something from the core, that is a signal the
interface is wrong. Raise it rather than reaching across.

**2. C++20, matching the project's core.** No new third-party dependencies
without asking. Image decoding for the CLI is the one place a dependency might
be justified; propose before adding.

**3. Deterministic.** The same image and glyph set always produce the same
output. No heuristics that vary by build, platform, or iteration order.

## Where it lives

```
src/screen-text/
    include/screentext/...     public headers
    src/...                    implementation
    tools/                     the CLI
    tests/                     unit tests and fixtures
    CMakeLists.txt
```

Two build targets: a static library and a CLI executable over the same
interface. Naming is deliberately generic -- the namespace is `screentext`, not
`beebium::screentext` -- because this may become a separate project. Do not
name it after Beebium or the BBC.

## The interface

Sketch, not gospel. Improve it where it is clumsy, but keep the shape: images
and glyphs in, structured text out, no emulator concepts anywhere.

```cpp
namespace screentext {

// An 8x8 monochrome glyph. Row-major, one byte per row, MSB leftmost --
// the same layout the BBC uses, and the same a font ROM dump has.
struct Glyph {
    std::array<uint8_t, 8> rows{};
    char32_t codepoint = 0;      // what this glyph means as text
};

// A named collection of glyphs. Later sets take precedence over earlier ones
// when both match, so a caller can override individual characters.
struct GlyphSet {
    std::string name;
    std::vector<Glyph> glyphs;
};

// Built-in sets, by name. At minimum "acorn-mos-1.20".
std::vector<std::string> builtin_glyph_set_names();
const GlyphSet& builtin_glyph_set(std::string_view name);

// A greyscale or paletted image. The caller has already reduced whatever it
// had to one byte per pixel; what the values mean is decided by `background`.
struct Image {
    size_t width = 0;
    size_t height = 0;
    std::vector<uint8_t> pixels;   // width * height, row-major
};

// A horizontal band of the image sharing one character geometry.
struct Band {
    size_t top = 0;              // first row of the image in this band
    size_t bottom = 0;           // one past the last
    size_t cell_width = 8;
    size_t cell_height = 8;
    size_t origin_x = 0;         // where the character grid starts
    size_t origin_y = 0;
    uint8_t background = 0;      // pixel value meaning "not part of a glyph"
};

struct Rect { size_t x = 0, y = 0, width = 0, height = 0; };

enum class Search {
    AlignedOnly,     // character cells only. Fast, exact, well structured.
    IncludeOffset,   // also search sub-cell offsets for text off the grid.
};

struct Options {
    Search search = Search::AlignedOnly;
    bool match_inverted = true;   // also compare against the complement
    std::optional<Rect> selection;  // whole image when unset
};

// One cell that was matched, or could not be.
struct Cell {
    Rect bounds;
    char32_t codepoint = 0;      // 0 when unmatched
    bool inverted = false;
    bool offset = false;         // matched away from the character grid
    std::string glyph_set;       // which set matched
};

// A contiguous run of cells on one line.
struct Run {
    std::string text;            // UTF-8
    Rect bounds;
    std::vector<Cell> cells;
};

struct Result {
    std::vector<Run> runs;
    size_t unmatched_cells = 0;
    size_t total_cells = 0;
};

Result read(const Image& image,
            const std::vector<Band>& bands,
            const std::vector<GlyphSet>& glyph_sets,
            const Options& options = {});

} // namespace screentext
```

## How matching works

Validated against a real machine before this spec was written: a character cell
on a BBC screen matches its font glyph **byte for byte**. There is no fuzzy
comparison anywhere in this design, and there should not be.

1. For each cell in a band, reduce its pixels to 8 bits per row: a pixel equal
   to `background` is 0, anything else is 1.
2. Look the 8-byte pattern up in a hash map built from the glyph sets. B-Em and
   ZEsarUX both scan linearly; a map is a strict improvement and costs nothing.
3. If `match_inverted`, also look up the complement, and mark the cell
   `inverted` when that is what hit. The BBC produces inverse text routinely by
   swapping foreground and background, so this is not an edge case.
4. No hit means unmatched. **Never guess.** An unmatched cell is reported as
   such and contributes a space to `text`, so column alignment survives -- but
   the cell records that it was unmatched, so a caller can tell "unreadable"
   from "blank". Conflating those is the specific mistake B-Em's `textsave.c`
   makes.

A cell straddling a band boundary is not matched.

## The CLI

Same capability, for use without linking, and for offline pipelines over
directories of screenshots.

```
screentext read <image> [--selection X,Y,W,H] [--glyphs FILE]...
                        [--search aligned|offset] [--no-inverted]
                        [--format text|json]
```

`--format text` prints the extracted text and nothing else, so it composes in a
shell pipeline. `--format json` emits the full structure -- runs, cells,
positions, unmatched counts -- for a caller that needs the uncertainty.

Exit status: 0 when it ran, whatever it found. Finding no text is not a
failure. Reserve non-zero for being unable to read the image or the glyphs.

## Built-in glyph sets

Generate the Acorn set as a committed source file from a MOS ROM, with a script
under `tools/`. The library must not read a ROM at runtime, and must not depend
on one being present to build.

The MOS 1.20 font is at ROM offset `0x0000` -- address `&C000` -- with eight
bytes per character starting at character 32. Verified: `(c - 32) * 8` predicts
the offset of every glyph checked.

Record in the generated file which ROM it came from, and keep the generator
script so it can be repeated for another MOS version.

## Testing

Fixtures, not an emulator. This is the main practical reason the library is
separate, so lean on it hard.

- **Synthetic images built in the tests**: compose an image by stamping known
  glyphs at known positions, then assert the text comes back. No files needed,
  and every case is constructed exactly.
- **A few real screenshots**, captured from the emulator and committed, so the
  library is exercised against genuine output as well as its own idea of it.
- Cover at least: plain text; inverse text; a cell that matches nothing;
  mixed matched and unmatched; a selection rectangle clipping mid-run;
  multiple bands with different geometry; an empty image; a glyph set that
  overrides a built-in character.
- The CLI is tested through its own interface, including that `--format text`
  emits nothing but text.

Aim for tests that would fail if the matching became fuzzy.

## Scope

**In, for the first increment:**

- The interface, the built-in Acorn set, supplied sets, aligned exact matching
  with inversion, the CLI, and the tests above.

**In, for a second increment, but design for it now:**

- `Search::IncludeOffset` -- sub-cell offset search for text that is not on the
  character grid, which on a BBC comes from `VDU 5`. ZEsarUX brute-forces all
  64 (dx, dy) positions per cell and that is known to work. It is slower by
  roughly that factor, which is why it is opt-in rather than default.

The interface must accommodate this from the start: `Cell::offset` and
`Search::IncludeOffset` exist in increment one even though nothing sets or
honours them yet.

**Out of scope entirely:**

- Anything that reads emulator state, memory, or ROMs at runtime.
- Proportional or anti-aliased text, and any form of approximate matching.
- Layout reconstruction beyond runs -- no paragraph detection, no reading-order
  inference across bands.

## Acceptance

- Builds as both a library and a CLI, on macOS and Linux.
- No reference to anything outside `src/screen-text/`.
- Tests pass and cover the cases listed.
- Given a screenshot of a MODE 4 BASIC listing, the CLI prints that listing.
