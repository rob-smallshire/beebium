# Vector MODE 7: Resolution-Independent Teletext Display

> **Scope note (July 2026).** This document originally described one feature
> spanning three layers: capturing teletext cell data, transporting it, and
> rendering it with a vector font. The capture layer turned out to serve a
> second, independent feature -- copying the screen as text -- which needs no
> rendering work whatsoever.
>
> The shared capture layer and the copy feature are now described in
> `teletext-cell-capture.md`. **This document is the rendering proposal**, and
> it consumes that capture rather than owning it.
>
> The two features also want different transports from the same data source: a
> snapshot RPC for copy, which needs one grid on demand, and a per-frame field
> for rendering, which needs every frame aligned with the pixels it replaces.
> That is a deliberate split, not duplication -- see "Two transports, one
> source" in the companion document, and the revised Open Question 1 below.
>
> Nothing in the analysis below has changed. What has changed is that the
> capture sections describe a foundation this document shares rather than
> introduces, and that rendering is understood to be the larger and more
> optional of the two consumers.

## Motivation

MODE 7 on the BBC Micro is fundamentally a text-based display: 40 columns by 25 rows of 7-bit character codes, rendered into pixels by the SAA5050 teletext character generator. The SAA5050 uses a fixed 5x9 pixel matrix per character, expanded with horizontal and vertical doubling, resulting in a ~480x500 rasterised output.

Because the display is character-based, all the information needed to reproduce it at arbitrary resolution already exists *before* pixelisation: the character code, foreground and background colours, character set, and attribute state at every cell position. If this structured data were streamed to clients alongside (or instead of) the rasterised pixel stream, clients could render MODE 7 using a vector font, giving a resolution-independent "infinite resolution" teletext display.

[Bedstead](https://bjh21.me.uk/bedstead/) is a vector (OpenType) font family derived directly from the SAA5050's character ROM. It covers the full SAA5050 repertoire: Latin alphanumerics, mosaic (sixel) graphics in both contiguous and separated forms, and characters from related teletext chips. It is public domain.

## Current Pipeline

The existing MODE 7 data flow is:

```
Screen Memory ($7C00-$7FFF, 1KB)
    |
    | CRTC address → translate_screen_address()
    v
7-bit character code
    |
    | VideoRenderer::render_teletext()
    v
Saa5050::byte()             <-- All attribute state is resolved here
    |
    | Control code processing, font lookup
    v
Output Buffer (8-slot circular, 12-bit expanded font rows)
    |
    | Saa5050::emit_pixels()
    v
PixelBatch (8 pixels x 4-bit RGBA)
    |
    | OutputQueue (lock-free SPSC)
    v
FrameRenderer → FrameBuffer → VideoService gRPC → Client
```

Each character passes through `Saa5050::byte()` (Saa5050.hpp:243), where the full attribute state is resolved: foreground/background colour, character set, double height, conceal, flash, hold graphics, and cursor. This is the ideal interception point.

## Proposed Architecture

A parallel output path captures structured cell data without disturbing the pixel pipeline:

```
Screen Memory ($7C00-$7FFF)
    |
    v
Saa5050::byte()  ──────────────>  TeletextGrid (40x25 cell buffer)
    |                                    |
    | (existing pixel path,              | (new structured path,
    |  unchanged)                        |  ~8KB per frame)
    v                                    v
PixelBatch → FrameRenderer         TeletextService gRPC
    |                                    |
    v                                    v
VideoService gRPC                  Client renders with
    |                              Bedstead font at any
    v                              resolution
Rasterised display
(traditional pixel output)
```

The two paths are independent. Clients can subscribe to either or both.

## Data Model

### TeletextCell

At each cell position, the following state is fully resolved within `Saa5050::byte()`:

| Field | Source | Notes |
|-------|--------|-------|
| Character code | `value & 0x7F` | 7-bit SAA5050 code |
| Foreground colour | `m_fg` (0-7) | Set by alpha/graphics colour codes |
| Background colour | `m_bg` (0-7) | Set by black background / new background codes |
| Character set | `m_charset` | Alpha, ContiguousGraphics, or SeparatedGraphics |
| Double height | `m_raster_shift` | Top half vs bottom half tracked via `m_raster_offset` |
| Concealed | `m_conceal` | Set by conceal code, cleared by colour codes |
| Flashing | `!m_text_visible` | Derived from 64-frame counter (visible frames 16-63) |
| Hold graphics | `m_hold`, `m_held_char` | Last graphics character repeated at control codes |
| Cursor | `cursor` parameter | From CRTC cursor state |

For control codes (0x00-0x1F), the cell records the control code itself plus the resolved display character (space, or held graphics character).

### TeletextGrid

A simple 40x25 array, double-buffered to match the pixel framebuffer:

```cpp
struct TeletextCell {
    uint8_t character;         // 7-bit SAA5050 character code
    uint8_t fg : 3;            // Foreground colour index (0-7)
    uint8_t bg : 3;            // Background colour index (0-7)
    uint8_t charset : 2;       // TeletextCharset enum
    bool double_height_top;    // Top half of a double-height pair
    bool double_height_bottom; // Bottom half of a double-height pair
    bool concealed;
    bool flashing;
    bool cursor;
    bool is_control_code;      // True if this cell holds a control code
};

struct TeletextGrid {
    TeletextCell cells[25][40];
    uint32_t frame_number;
    bool active;               // True when display is in MODE 7
};
```

At ~6 bytes per cell, one frame is 6000 bytes. With double-buffering, the total memory overhead is 12KB.

### Unicode Mapping

The client needs a mapping from (charset, character_code) to Unicode codepoint for Bedstead rendering:

- **Alpha characters** (0x20-0x7F): Mostly ASCII-identity. A few characters differ from ASCII in the SAA5050's character set (e.g. `#` at 0x23 is `£` in the UK teletext character set).
- **Contiguous mosaic graphics** (0x20-0x3F with bit 5 set): Map to Unicode Symbols for Legacy Computing block (U+1FB00-U+1FB3B). These are the 2x3 sixel block characters. Bedstead includes glyphs for all 64 combinations.
- **Separated mosaic graphics**: Same sixel patterns but with visible gaps between blocks. Bedstead provides separated variants.

The mapping table is small (96 entries per charset, 3 charsets = 288 entries) and static.

## gRPC Service

A new `TeletextService` alongside the existing `VideoService`:

```protobuf
syntax = "proto3";

package beebium;

service TeletextService {
    // Stream teletext frames as structured cell data
    rpc SubscribeTeletextFrames(SubscribeTeletextFramesRequest)
        returns (stream TeletextFrame);
}

message SubscribeTeletextFramesRequest {}

message TeletextCell {
    uint32 character = 1;          // 7-bit SAA5050 character code
    uint32 fg_color = 2;           // Colour index (0-7)
    uint32 bg_color = 3;           // Colour index (0-7)
    CharacterSet charset = 4;      // Which character set is active
    bool double_height_top = 5;    // Top half of double-height pair
    bool double_height_bottom = 6; // Bottom half of double-height pair
    bool concealed = 7;            // Hidden by conceal control code
    bool flashing = 8;             // Flash attribute active
    bool cursor = 9;               // CRTC cursor at this position
    bool is_control_code = 10;     // Cell contains a control code
}

enum CharacterSet {
    ALPHA = 0;
    CONTIGUOUS_GRAPHICS = 1;
    SEPARATED_GRAPHICS = 2;
}

message TeletextFrame {
    repeated TeletextCell cells = 1;  // 1000 cells, row-major order
    uint32 columns = 2;               // Always 40
    uint32 rows = 3;                  // Always 25
    uint64 frame_number = 4;
    uint32 flash_phase = 5;           // 0-63, for client-side flash animation
}
```

### Bandwidth

A TeletextFrame is approximately 8KB per frame at 25 fps (interlaced, so ~12.5 unique frames per second after deduplication) = ~100KB/s. Compare this to the rasterised pixel stream: 480 x 500 x 4 bytes x 25 fps = ~24MB/s. The structured stream is roughly 240x smaller.

### Sending Colour Values vs Indices

The proto above sends colour *indices* (0-7) rather than RGB values. This lets the client choose its own palette. The standard BBC teletext palette is:

| Index | Colour | RGB |
|-------|--------|-----|
| 0 | Black | #000000 |
| 1 | Red | #FF0000 |
| 2 | Green | #00FF00 |
| 3 | Yellow | #FFFF00 |
| 4 | Blue | #0000FF |
| 5 | Magenta | #FF00FF |
| 6 | Cyan | #00FFFF |
| 7 | White | #FFFFFF |

Clients are free to adjust these (e.g. for CRT colour temperature, accessibility, or aesthetic preference).

## Core Changes

### Interception Point

The interception is in `Saa5050::byte()`, after control code processing but before the output buffer write. Roughly 10-15 lines of new code:

```cpp
void byte(uint8_t value, uint8_t dispen, bool cursor = false) {
    value &= 0x7F;

    // ... existing control code / character processing ...

    // === NEW: Capture cell data for vector output ===
    if (m_teletext_grid && dispen) {
        auto& cell = m_teletext_grid->cell(m_row, m_column);
        cell.character = value;
        cell.fg = m_fg;
        cell.bg = m_bg;
        cell.charset = m_charset;
        cell.double_height_top = (m_raster_shift == 1 && m_raster_offset == 0);
        cell.double_height_bottom = (m_raster_shift == 1 && m_raster_offset == 20);
        cell.concealed = m_conceal;
        cell.flashing = !m_text_visible;
        cell.cursor = cursor;
        cell.is_control_code = (value < 32);
    }

    // ... existing output buffer write (unchanged) ...
}
```

The `m_teletext_grid` pointer is null when vector output is not enabled, so the cost when disabled is a single null-pointer check per character -- negligible.

### Row/Column Tracking

The SAA5050 does not currently track row and column position explicitly. The column is tracked by `VideoRenderer::teletext_column_`, and the row can be derived from CRTC state. These would need to be made available to the SAA5050, either by passing them as parameters to `byte()` or by adding row/column state to the SAA5050 itself.

### Double-Height Handling

Double-height characters span two character rows. The SAA5050 renders the top half on the first row (with `m_raster_offset == 0`) and the bottom half on the next row (with `m_raster_offset == 20`). The TeletextGrid should only capture cell data on the *first scanline* of each character row (when `m_raster == 0`) to avoid writing 20 times per cell. This is a simple guard:

```cpp
if (m_teletext_grid && dispen && m_raster == 0) {
    // ... capture cell ...
}
```

For double-height, the grid records `double_height_top` on the first row and `double_height_bottom` on the second. The client renders the character at 2x vertical scale and clips to the appropriate half.

### Frame Lifecycle

The TeletextGrid swaps at VSYNC, synchronised with the pixel framebuffer. In `Saa5050::vsync()`:

```cpp
void vsync() {
    // ... existing frame counter logic ...

    if (m_teletext_grid) {
        m_teletext_grid->swap();
        m_row = 0;
    }
}
```

## Client Rendering

### macOS (SwiftUI + Core Text)

The macOS client would add an alternative rendering path for MODE 7:

1. Subscribe to `TeletextService.SubscribeTeletextFrames`
2. When a TeletextFrame arrives, build an `NSAttributedString` (or a grid of `Text` views) using Bedstead.otf
3. Set font size, foreground colour, and background colour per cell
4. For double-height cells, render at 2x font size and clip to top/bottom half
5. Animate flash client-side using `flash_phase` or an independent timer

The vector view could be:
- A toggle alongside the pixel view (user switches between "CRT" and "vector" modes)
- An overlay with opacity control
- The default for MODE 7, with automatic fallback to pixels for modes 0-6

### Font Size and Layout

Bedstead is designed on a 5x9 grid. At any font size, a 40x25 grid of Bedstead characters will have the correct proportions. The client simply sizes the font to fill the available display area:

```
font_size = min(view_width / 40, view_height / 25) * scale_factor
```

On a Retina display, this gives crisp teletext at any window size.

### Web Client (HTML/CSS)

A hypothetical web client could render the teletext grid as a `<pre>` element or CSS grid with per-cell `<span>` elements, using Bedstead as a web font. This is particularly natural since Bedstead is available as an OpenType font that browsers can use directly.

## Edge Cases and Complications

### Hold Graphics

When hold graphics is active and a control code is encountered, the last graphics character is displayed instead of a space. The interception point in `byte()` sees the *resolved* display state: `m_hold` is set, `m_last_graphics_data` holds the pixel pattern, and the original character code is still available. The TeletextCell records the original control code in `character` and sets `is_control_code = true`. The client uses the held character for display.

**Complication**: The held character is stored as pixel data (`m_last_graphics_data`), not as a character code. To support vector rendering of held graphics, the SAA5050 would need to additionally store the *character code* of the last graphics character. This is a one-byte addition to the SAA5050 state.

### Smooth Scrolling

The BBC Micro does not support sub-character scrolling in MODE 7. Hardware scrolling changes the CRTC start address in 40-byte increments (whole rows). The TeletextGrid is always cell-aligned, so scrolling "just works" -- the grid contents change, and the client re-renders.

### Mid-Frame Mode Switching

If the display mode changes from MODE 7 to a bitmap mode mid-frame (or vice versa), the TeletextGrid may contain partial data. The `active` flag indicates whether MODE 7 was active for the frame. Clients should fall back to the pixel stream if `active` is false.

### Conceal and Reveal

Broadcast teletext has a "reveal" button that shows concealed text. The BBC Micro's MODE 7 does not have a hardware reveal mechanism, but Beebium could offer one: the TeletextCell includes the `concealed` flag and the underlying character code. A client-side "reveal" toggle would simply ignore the flag and display the character. This is a feature advantage over the pixel stream, where concealed characters are indistinguishable from the background.

### Flash Animation

The SAA5050 uses a 64-frame counter: characters with flash enabled are visible on frames 16-63 and hidden on frames 0-15. The `flash_phase` field in TeletextFrame provides the current counter value so clients can synchronise if desired. Alternatively, clients can animate flash independently at whatever rate looks good -- there is no requirement to match the SAA5050's specific timing for a vector rendering.

## Scope of Changes

Rows marked *shared* belong to the capture layer in
`teletext-cell-capture.md` and are needed by copy-as-text too, so they are not
costs attributable to rendering alone. Rendering's own cost is the Unicode
mapping and the client rasteriser.

| Component | Estimated Change |
|-----------|-----------------|
| `Saa5050.hpp` | *shared* -- ~30 lines: cell capture, held character code, grid pointer |
| `VideoRenderer.hpp` | *shared* -- ~10 lines: row tracking, grid lifecycle |
| New `TeletextGrid.hpp` | *shared* -- ~80 lines: double-buffered cell array |
| New `teletext.proto` | ~40 lines: service and message definitions |
| New `TeletextService.cpp` | ~60 lines: gRPC stream implementation |
| Unicode mapping table | ~50 lines: static lookup table |
| macOS client | ~200 lines: Bedstead rendering view |
| **Total** | ~470 lines |

The core emulation changes are minimal (~40 lines). The pixel rendering path is completely unaffected.

## Open Questions

1. **Should the TeletextService be a separate gRPC service or an extension of VideoService?** A separate service is cleaner (single responsibility), but extending VideoService with a `SubscribeTeletextFrames` RPC would avoid adding another service to the discovery and connection machinery.

2. **Should the grid capture every frame or deduplicate?** MODE 7 is interlaced (two fields per frame), but the character data is identical for both fields. The grid could capture once per frame rather than once per field. A sequence number or dirty flag would let clients skip redundant frames.

3. **Should control codes be sent as separate cell types or inline?** The current design uses `is_control_code` as a flag. An alternative is a union type (either a displayable character or a control code with its effect), but this adds protobuf complexity for little gain.

4. **Bedstead font variants**: Bedstead comes in six width variants (standard, extended, semi-condensed, condensed, extra-condensed, ultra-condensed). Which should the client use? Standard width matches the SAA5050's proportions most closely, but extended might look better at large sizes.

5. **Alpha character set national variants**: The SAA5050 supports multiple national character sets (e.g. UK maps 0x23 to `£` rather than `#`). The initial implementation could hardcode the UK variant; a future extension could add a national character set field to TeletextFrame.

## Summary

This proposal is feasible with modest effort. The key insight is that `Saa5050::byte()` already resolves all the attribute state needed for vector rendering -- the information is present, it just is not captured. Adding a parallel capture path alongside the existing pixel pipeline requires no changes to timing, pixel rendering, or the existing gRPC video service. The structured teletext stream is 240x smaller than the pixel stream, and clients gain resolution independence, accessibility features (reveal, custom palettes), and rendering flexibility.

## Composition with the Display Style architecture (May 2026)

The macOS client is mid-way through introducing a `DisplayStyle` abstraction
(see `clients/macos/Beebium/Beebium/Display/`). Each style governs how a
frame is *presented* -- which Metal pipeline runs, whether four debug borders
are drawn, how the active pixel area is fitted into the drawable, and which
SwiftUI options panel appears under the Video sidebar. Initial styles are
**Standard** (active area only, letterbox/pillarbox fill) and **Debug**
(today's coloured border behaviour); a future **CRT** style is planned.

Vector MODE 7 sits orthogonally to that abstraction. Display styles answer
"how is the frame presented?", whereas vector MODE 7 answers "where do the
content pixels come from?". Rather than introducing a parallel set of
"vector-MODE 7" Display Styles (which would force a Cartesian product of
options), the cleaner shape is a single global toggle that interacts with
whichever style happens to be active.

### Where the toggle lives

`VideoSettings` (introduced alongside the `DisplayStyle` protocol) is the
per-window owner of cross-cutting display preferences. The natural addition
is:

```swift
@MainActor
final class VideoSettings: ObservableObject {
    @Published var activeStyleID: String       // Standard / Debug / CRT
    @Published var pixelShape: PixelShape       // Authentic / Crisp
    @Published var windowBackground: Color
    @Published var mode7VectorEnabled: Bool     // <-- new
    // ...
}
```

A sidebar checkbox under the Video pane toggles it. Per-machine cache (keyed
by provenance UUID, planned for the same effort) picks it up automatically
because it is just another field on `VideoSettings`.

### Renderer dispatch

The renderer becomes responsible for selecting the content path per frame.
The active Display Style's pipeline keeps consuming a normal `MTLTexture`;
what changes is *what is uploaded into that texture*:

```
For each incoming Frame:
  if videoSettings.mode7VectorEnabled
     and frame.teletext_data.active
     and activeStyle accepts vector replacement:
      rasterise teletext cells via Bedstead/Core Text into the frame texture
  else:
      upload frame.pixels into the frame texture
  encode active DisplayStyle's uniforms; draw
```

Because the rasterisation lands in the same texture, the Display Style's
existing geometry math -- aspect-fit, line-doubling skip for interlaced
modes, per-region scaling for split-screen, the four debug borders if Debug
is active -- continues to work without modification. Debug + vector MODE 7
gives crisp text inside the coloured CRTC overscan borders. Standard +
vector MODE 7 gives crisp text fitted edge-to-edge with the configured
window background as letterbox.

### A note on style consent

A future CRT shader will probably want to opt out of vector replacement (the
analog phosphor look depends on the SAA5050 raster output). The cleanest way
to express that is a small protocol addition when CRT lands -- e.g. a
`var supportsVectorContentReplacement: Bool { get }` defaulting to `true` --
rather than special-casing CRT in the renderer. Standard and Debug both
return `true`. Not needed for the first cut.

### Reconsidering Open Question 1 in light of the per-window subscription model

The original Open Question above asks whether vector MODE 7 should ride on a
separate `TeletextService` or extend `VideoService`.

Note first that the question is narrower than it looks, because it is only
about the *rendering* transport. Copy-as-text reads the same grid through a
snapshot RPC (`teletext-cell-capture.md`), and that decision is independent:
the answer below does not constrain it, and it does not constrain the answer
below. The framing "one data source, two transports" replaces the original
assumption that one stream had to serve every consumer.

For rendering specifically, with the Display Style architecture in place, the
case for **extending `VideoService`** has strengthened:

- Each emulator window already owns one `VideoClient` and one
  `SubscribeFrames` stream. Adding a parallel `TeletextService` subscription
  doubles the per-window stream count, the connection-tracker bookkeeping,
  and the lifecycle wiring (disconnect coordination, error handling) for no
  obvious gain.
- The two streams must be kept frame-aligned. Bundling cell data into the
  existing `Frame` message via an optional sub-message makes alignment
  implicit (`frame_number` is shared; both arrive together in the same
  proto). A separate service would have to reconstruct alignment from
  sequence numbers.
- 8 KB per frame is trivial next to the existing pixel payload. Even when a
  vector-aware client is the only consumer, the bandwidth saving from
  splitting the streams is small.
- A client that does not care about teletext data (every current client) can
  ignore the optional field with zero cost; proto3 default semantics handle
  that cleanly.

Concretely, this means adding to `video.proto`:

```protobuf
message Frame {
  // ... existing fields ...
  optional TeletextFrameData teletext_data = 14;  // populated when MODE 7 active
}

message TeletextFrameData {
  bool active = 1;                       // MODE 7 was active for this frame
  uint32 columns = 2;                    // Always 40
  uint32 rows = 3;                       // Always 25
  uint32 flash_phase = 4;
  repeated TeletextCell cells = 5;       // 1000 entries, row-major
}
```

The `TeletextCell` and `CharacterSet` definitions from the original
proposal carry over unchanged. The per-RPC `SubscribeTeletextFrames` and
`SubscribeTeletextFramesRequest` types become unnecessary.

### PAR (Pixels: Authentic / Crisp) still applies

The "Pixels" toggle on `VideoSettings` controls Pixel Aspect Ratio
(0.96 = Authentic, 1.0 = Crisp), which determines how wide a BBC pixel is
relative to its height. Vector text inherits the same concern: at PAR 0.96
each character cell is the proportions a real BBC produced; at PAR 1.0 each
cell is square, giving cleaner integer scaling on modern displays. The
toggle stays meaningful in vector mode and applies to the rasterised text
texture exactly as it does to bitmap-mode pixel textures.

### What this means for sequencing the work

The current Display Style work (Standard / Debug + sidebar picker + Pixels
toggle + window background + per-machine cache) does not block, conflict
with, or constrain the vector MODE 7 effort. The interception in
`Saa5050::byte()`, the `TeletextGrid` data structure, and the `Frame`
message extension can be implemented after the Display Style work lands,
with the only client-side change being:

1. A new `mode7VectorEnabled` field on `VideoSettings` and a sidebar
   checkbox.
2. A Bedstead-based rasteriser invoked by the renderer when the toggle is
   on and `Frame.teletext_data.active` is set.
3. (Optional, when CRT lands) the `supportsVectorContentReplacement`
   protocol property.

Everything else in the original proposal -- the emulation changes, the
Unicode mapping table, the rendering and font sizing logic -- carries
through unchanged.
