# Mode 7 / SAA5050 Design Notes

## Overview

This document captures research into Mode 7 teletext rendering architectures, comparing Beebium's current implementation with B2's higher-quality approach, and exploring alternative output modes for external renderers.

## Architectural Options

Mode 7 rendering can be structured in several ways, each suited to different use cases:

| Approach | Output Per Character | Granularity | Use Case |
|----------|---------------------|-------------|----------|
| **Direct Pixels** | 8 RGB pixels per scanline | Scanline | Simple framebuffer, current Beebium |
| **Metadata** (B2) | fg/bg + 12-bit bitmap per scanline | Scanline | Internal TV filtering, CRT shaders |
| **Character Data** | char code + attributes per cell | Character row | External renderers, OTF fonts, accessibility |

Since Mode 7 is easily detected via `video_ula.teletext_mode()`, Beebium could support multiple output paths simultaneously.

## Comparison: Beebium vs B2

| Aspect | Beebium Current | B2 |
|--------|----------------|-----|
| Font width | 6 pixels | 12-16 pixels (6×2 expanded) |
| Font storage | `uint8_t[96][10]` | `uint16_t[2][3][96][20]` |
| Antialiasing | None | Edge-smoothing via GetAARow() |
| Output | Direct pixels | Metadata (fg, bg, bitmap data) |
| Final render | In SAA5050 | In TVOutput with gamma blending |
| Variants | Single | AA and non-AA pre-computed |

## B2's Two-Stage Architecture

### Stage 1: SAA5050 (Emulation)

B2's SAA5050 does **not** render final pixels. Instead, it outputs metadata:

```cpp
void EmitPixels(VideoDataUnitPixels *pixels, const VideoDataPixel *palette) {
    pixels->pixels[0] = palette[output->bg];
    pixels->pixels[0].bits.x = VideoDataType_Teletext;  // Type marker
    pixels->pixels[1] = palette[output->fg];
    pixels->pixels[2].all = output->data0;  // Font bitmap (lower bits)
    pixels->pixels[3].all = output->data1;  // Font bitmap (upper bits)
}
```

This separates emulation (what the chip does) from presentation (how it looks on screen).

### Stage 2: TVOutput (Presentation)

The TVOutput stage:
1. Detects `VideoDataType_Teletext` marker
2. Expands 12-bit font data to output pixels
3. Uses gamma-corrected blend table for smooth transitions
4. Converts 6 input pixels → 8 output pixels with weighted averaging

## Font Pre-processing

### Expanded Font Table

At startup, B2 pre-computes an expanded font table:

```cpp
// teletext_font[aa][charset][char][row]
// aa: 0 = no antialiasing, 1 = antialiased
// charset: Alpha, ContiguousGraphics, SeparatedGraphics
// char: 96 printable characters (0x20-0x7F)
// row: 20 rows (10 font rows × 2 for scanline doubling)
static uint16_t teletext_font[2][3][96][20];
```

### Pixel Doubling (Get16WideRow)

The 6-bit font is expanded to 12 bits (each pixel doubled):

```cpp
static uint16_t Get16WideRow(TeletextCharset charset, uint8_t ch, unsigned y) {
    uint16_t w = 0;
    if (y >= 0 && y < 20) {
        size_t left = 0;
        uint8_t byte = GetTeletextFontByte(charset, ch, y / 2);
        for (size_t i = 0; i < 6; ++i) {
            if (byte & 1 << i) {
                w |= 3 << left;  // Set 2 adjacent bits
            }
            left += 2;
        }
    }
    return w;
}
```

### Antialiasing (GetAARow)

Edge smoothing adds intermediate pixels at diagonal boundaries:

```cpp
static uint16_t GetAARow(TeletextCharset charset, uint8_t ch, unsigned y) {
    if (ShouldAntialias(charset, ch)) {
        uint16_t a = Get16WideRow(charset, ch, y);
        uint16_t b = Get16WideRow(charset, ch, y - 1 + y % 2 * 2);  // Adjacent row

        return a | (a >> 1 & b & ~(b >> 1)) | (a << 1 & b & ~(b << 1));
    } else {
        return Get16WideRow(charset, ch, y);
    }
}
```

The formula adds a pixel where:
- Current row has a pixel (`a`)
- Adjacent row has a pixel (`b`)
- Adjacent row's neighbor doesn't have a pixel (`~(b >> 1)` or `~(b << 1)`)

This creates smooth diagonal transitions.

## Gamma-Corrected Blending

B2 uses a pre-computed blend table for color mixing:

```cpp
// Blend two 4-bit values with gamma correction
// Weights: a gets 1/3, b gets 2/3
double value = pow((a + b + b) / 3.0, 1.0 / gamma);
```

This ensures perceptually correct color blending when expanding pixels.

## Key Source Files in B2

| File | Purpose |
|------|---------|
| `src/beeb/src/teletext.cpp` | SAA5050 emulation, font preprocessing |
| `src/beeb/src/teletext_font.inl` | Raw 6×10 font data |
| `src/beeb/include/beeb/teletext.h` | SAA5050 class definition |
| `src/beeb/src/TVOutput.cpp` | Gamma blending, pixel expansion |

## Implementation Status

Beebium has implemented B2-quality Mode 7 rendering:

### Completed Features

1. **Pre-computed Antialiased Font** (`Saa5050.hpp`)
   - Font storage: `uint16_t TELETEXT_EXPANDED_FONT[2][3][96][20]`
   - `get_doubled_row()` - expands 6 bits to 12 bits (horizontal pixel doubling)
   - `get_aa_row()` - B2's edge smoothing algorithm
   - Both AA and non-AA variants pre-computed at startup via `TeletextFontInit`

2. **Gamma-Corrected Blending**
   - `TELETEXT_BLEND_TABLE[16][16]` pre-computed with γ=2.2
   - `emit_pixels()` expands 6→8 pixels using weighted blend pattern
   - Pattern: `p0, blend(p0,p1), blend(p2,p1), p2, p3, blend(p3,p4), blend(p5,p4), p5`

3. **Complete Control Code Support**
   - Alpha/graphics colors (0x01-0x07, 0x11-0x17)
   - Flash/steady (0x08, 0x09)
   - Double height (0x0C, 0x0D)
   - Contiguous/separated graphics (0x19, 0x1A)
   - Black/new background (0x1C, 0x1D)
   - Hold/release graphics (0x1E, 0x1F)
   - Conceal display (0x18)

### Architectural Notes

Unlike B2's two-stage approach (SAA5050 outputs metadata, TVOutput renders), Beebium renders final pixels directly in `emit_pixels()`. This is simpler and sufficient since Beebium's FrameRenderer doesn't apply additional CRT filtering. The quality is equivalent because:

- Same antialiasing algorithm as B2
- Same gamma-corrected blend table
- Same 6→8 pixel expansion pattern

If CRT shader effects are added in the future, the existing `PixelBatchType::Teletext` marker enables detecting Mode 7 content for special handling.

---

## Interlace Implementation

Mode 7 uses CRTC interlace mode, which requires coordinated handling across the CRTC, VideoRenderer, and FrameRenderer. This section documents the implementation and the subtle timing issues involved.

### Why Mode 7 Uses Interlace

Mode 7 sets CRTC register R8 = 0x03, enabling "interlace sync and video" mode. In this mode:

- The display outputs **50 fields per second** (PAL) instead of 25 frames per second
- Each field contains half the scanlines: odd fields show rasters 0, 2, 4..., even fields show 1, 3, 5...
- When both fields are displayed together on a CRT, they interleave to produce full vertical resolution

This affects cursor timing because the CRTC's `field_count_` (used for cursor blink) increments per field, not per frame.

### CRTC Interlace Handling

The `Crtc6845` class implements interlace with these key changes:

#### 1. Detect Interlace Mode

```cpp
bool interlace_sync_and_video() const {
    return (registers_[R8_INTERLACE] & 0x03) == 0x03;
}
```

R8 bits 0-1 encode the interlace mode: 00/10 = normal, 01 = interlace sync only, 11 = interlace sync and video.

#### 2. Increment Raster by 2

```cpp
if (interlace_sync_and_video()) {
    raster_ += 2;
} else {
    ++raster_;
}
```

Each field skips every other scanline. The odd field displays rasters 0, 2, 4, 6, 8; the even field displays 1, 3, 5, 7, 9.

#### 3. Field-Based Raster Start

```cpp
// Track field parity - toggles at end of vertical displayed area
if (row_ == registers_[R6_VDISPLAYED] && v_display_) {
    odd_field_ = !odd_field_;
}

// Start raster based on field
void end_of_frame() {
    raster_ = (interlace_sync_and_video() && !odd_field_) ? 1 : 0;
}
```

Odd fields start at raster 0; even fields start at raster 1.

#### 4. End-of-Row Detection

```cpp
if (interlace_sync_and_video()) {
    at_max_raster = (raster_ >> 1) == ((registers_[R9_MAX_SCANLINE] & 0x1F) >> 1);
} else {
    at_max_raster = (raster_ == (registers_[R9_MAX_SCANLINE] & 0x1F));
}
```

The halved comparison ensures correct end-of-row detection when raster increments by 2.

### VideoRenderer Flag Propagation

The `VideoRenderer` propagates interlace state to the `FrameRenderer` via the `VIDEO_FLAG_INTERLACE` flag:

```cpp
if (crtc_output.interlace && crtc_output.odd_field) flags |= VIDEO_FLAG_INTERLACE;
```

This flag is set during odd field display periods only. The FrameRenderer uses this to detect interlace mode and track field parity.

### FrameRenderer Field Interleaving

The `FrameRenderer` composites both fields into a single framebuffer:

#### 1. Detect Interlace Mode

```cpp
if (interlace_odd) {
    in_interlace_mode_ = true;
}
```

Once VIDEO_FLAG_INTERLACE is seen, the renderer enters interlace mode.

#### 2. Swap Every Other VSYNC

```cpp
if (in_interlace_mode_) {
    interlace_field_count_++;
    if ((interlace_field_count_ & 1) == 0) {
        frame_buffer_->swap();  // Swap after completing both fields
    }
} else {
    frame_buffer_->swap();  // Non-interlace: swap every VSYNC
}
```

In interlace mode, a complete frame requires two fields, so we swap every other VSYNC.

#### 3. Interleave Y Positions

```cpp
if (in_interlace_mode_) {
    // First field → even lines (0, 2, 4...)
    // Second field → odd lines (1, 3, 5...)
    int field_offset = (interlace_field_count_ & 1) ? 0 : 1;
    write_y = static_cast<int>(y_) * 2 + field_offset;
    write_y += vertical_offset_ * 2;  // Scale offset for interlace
} else {
    write_y = static_cast<int>(y_) + vertical_offset_;
}
```

Each field's scanlines are spread across alternating framebuffer lines. The first field (odd rasters from CRTC) writes to even framebuffer lines; the second field (even rasters) writes to odd framebuffer lines.

### Timing Subtlety: When odd_field_ Toggles

A critical implementation detail: the CRTC's `odd_field_` flag toggles at the **end of the vertical displayed area** (when `row_ == R6_VDISPLAYED`), which is **before VSYNC**. This means:

- During VSYNC at the end of an odd field, `odd_field_` has already toggled to `false`
- During VSYNC at the end of an even field, `odd_field_` has already toggled to `true`

The FrameRenderer cannot rely on VIDEO_FLAG_INTERLACE at VSYNC time to determine which field just ended. Instead, it counts VSYNCs and uses the count's parity to determine field interleaving.

### Effects on Cursor Display

#### Cursor Blink Rate

The cursor blink rate is derived from `field_count_`, which increments at the end of each field. The counter always increments at 50 Hz (PAL field rate): in interlace mode it increments once per field, while in non-interlace mode it increments by 2 per frame to compensate. This ensures consistent cursor blink timing across all modes.

#### Cursor Thickness

The cursor appears on specific raster lines within a character row (determined by R10/R11). With proper field interleaving:

- Odd field shows cursor on even rasters (e.g., raster 18)
- Even field shows cursor on odd rasters (e.g., raster 19)
- Combined: cursor appears on two adjacent framebuffer lines = 2 pixels thick

Without field interleaving, both fields overwrite the same framebuffer lines, resulting in a 1-pixel cursor.

### Summary

| Component | Responsibility |
|-----------|---------------|
| **Crtc6845** | Raster increment by 2, field-based start, end-of-row detection |
| **Saa5050** | Its own line count (DEW, LOSE), RA0 as CRS: glyph line 2 x line + CRS |
| **VideoRenderer** | Propagate VIDEO_FLAG_INTERLACE during odd field; drive the SAA5050's pins |
| **FrameRenderer** | Count fields, swap every 2nd VSYNC, interleave Y positions |

The implementation correctly handles the BBC Micro's interlace mode, producing:
- 50 Hz field rate for correct cursor blink timing
- Proper field interleaving for 2-pixel cursor thickness
- Full-height text (not half-height from field overwriting)

### Mode 7 Without Interlace

The SAA5050 does not take its glyph line from the CRTC's row address. It counts
a character's ten lines itself -- cleared by the trailing edge of DEW (VSYNC),
advanced by the trailing edge of LOSE (DISPTMG) -- and sees only RA0, on CRS,
which picks a font line's rounded partner. In the standard mode the two agree:
RA runs 0,2,..18 or 1,3,..19 and the glyph line 2 x line + CRS is the same
number. A program that turns the interlace off for teletext by programming
the 6845 directly (R8=&92, R9=9; Ant Attack does it for its title page --
MOS 1.20 keeps MODE 7 interlaced even after `*TV ,1`) gets RA 0..9, and the
chip still draws whole glyphs, ten lines to a row, with CRS alternating line by
line. Taking the row address as the glyph line instead drew the top half of
every character and never ended a row. See `video-subsystem.md` (SAA5050 Timing
Integration) and `tests/test_saa5050_line_counter.cpp`.

---

## Character-Based Output Mode

### Motivation

Both the direct pixel and B2 metadata approaches output **scanline-level** data - they process each of the 19-20 scanlines within a character row separately. This is necessary for cycle-accurate emulation and CRT shader effects, but is inefficient for external renderers that don't need per-scanline data.

A **character-based output mode** would emit high-level character data once per character cell, allowing external renderers to:

- Render text using arbitrary fonts (OTF, TTF, vector)
- Scale to any resolution without pixelation
- Support accessibility tools (screen readers)
- Extract text content for search/indexing
- Apply custom styling (web-based frontends)

### Character Data Structure

```cpp
// Output per character cell (not per scanline)
struct TeletextCharacter {
    uint8_t code;               // Raw character (0x20-0x7F after masking bit 7)
    uint8_t foreground;         // Color index 0-7
    uint8_t background;         // Color index 0-7
    TeletextCharset charset;    // Alpha, ContiguousGraphics, SeparatedGraphics

    // Display state
    bool double_height_top;     // Top half of double-height character
    bool double_height_bottom;  // Bottom half of double-height character
    bool flash;                 // Character should flash
    bool concealed;             // Character is concealed (show as background)
    bool held_graphics;         // This is a held graphics character

    // Position (for sparse output)
    uint8_t column;             // 0-39
    uint8_t row;                // 0-24
};

// Output per character row (40 characters)
struct TeletextRow {
    TeletextCharacter characters[40];
    uint8_t row_number;         // 0-24
    bool any_double_height;     // Hint: row contains double-height chars
};

// Output per frame
struct TeletextFrame {
    TeletextRow rows[25];
    uint32_t frame_number;
    bool flash_phase;           // Current flash on/off state
};
```

### Output Timing Options

| Timing | Output Point | Latency | Buffer Size |
|--------|-------------|---------|-------------|
| **Per-character** | After each byte() call | Minimal | 1 char |
| **Per-row** | At end_of_line() | 1 row | 40 chars |
| **Per-frame** | At vsync() | 1 frame | 1000 chars |

Per-frame output is most practical for external renderers - they typically redraw the entire display anyway. Per-row output enables progressive rendering.

### Implementation Approach

#### Option A: Parallel Output Path

Add character output alongside existing pixel output:

```cpp
void Saa5050::byte(uint8_t data, int de) {
    // Existing pixel pipeline...
    process_control_codes(data);
    m_delay_buffer[m_write_index] = ...;

    // Character output (if enabled)
    if (m_character_output_enabled && de) {
        TeletextCharacter ch = {
            .code = data & 0x7F,
            .foreground = m_fg,
            .background = m_bg,
            .charset = m_charset,
            .double_height_top = m_double_height && !m_double_height_bottom_row,
            .double_height_bottom = m_double_height && m_double_height_bottom_row,
            .flash = m_flash,
            .concealed = m_concealed,
            .column = m_column
        };
        m_row_buffer[m_column++] = ch;
    }
}

void Saa5050::end_of_line() {
    // Emit row buffer to character output queue
    if (m_character_output_enabled) {
        m_character_output->push(m_row_buffer);
    }
}
```

#### Option B: Frame Capture at Higher Level

Capture character data from screen memory directly in `ModelBHardware`:

```cpp
void ModelBHardware::capture_teletext_frame(TeletextFrame& frame) {
    if (!video_ula.teletext_mode()) return;

    uint16_t addr = 0x7C00;  // Mode 7 screen start
    for (int row = 0; row < 25; ++row) {
        // Read 40 bytes from screen memory
        for (int col = 0; col < 40; ++col) {
            uint8_t byte = main_ram.read(addr++);
            // Process control codes to determine attributes...
            frame.rows[row].characters[col] = process_byte(byte, ...);
        }
    }
}
```

This approach is simpler but doesn't capture the exact state of the SAA5050 (held graphics, etc.). It's essentially re-implementing control code parsing.

#### Option C: Hybrid - SAA5050 State Snapshot

Let SAA5050 process normally but expose its state for external capture:

```cpp
// In SAA5050
struct CharacterState {
    uint8_t fg, bg;
    TeletextCharset charset;
    bool double_height, flash, concealed, hold_graphics;
    uint8_t held_char;
};

const CharacterState& Saa5050::current_state() const { return m_state; }
uint8_t Saa5050::last_character() const { return m_last_char; }
```

External code can then sample this state after each `byte()` call to build character data.

### Use Cases

#### 1. High-Resolution OTF Rendering

Frontend loads a teletext-style OTF font (e.g., Galax, Unscii, or Bedstead) and renders at 4K:

```
Core (SAA5050) → Character Output → Frontend → OTF Renderer → 4K Display
```

Benefits:
- Crisp text at any resolution
- No pixel scaling artifacts
- User-selectable fonts

#### 2. Web-Based Frontend

Character data sent via WebSocket to browser:

```javascript
// Receive TeletextFrame as JSON
ws.onmessage = (event) => {
    const frame = JSON.parse(event.data);
    renderTeletextFrame(frame);  // CSS Grid + styled spans
};
```

#### 3. Accessibility

Screen reader integration:

```
SAA5050 → Character Output → Text Extraction → Screen Reader API
```

#### 4. Teletext Page Archival

Save displayed pages as structured data:

```json
{
    "page": 100,
    "rows": [
        {"text": "CEEFAX 100  News Headlines", "colors": [7,7,7,...]},
        ...
    ]
}
```

### Graphics Characters

Teletext graphics (sixel) characters (0x20-0x3F and 0x60-0x7F with bit 5 clear) pose a challenge for character-based output:

| Approach | Handling |
|----------|----------|
| **Unicode Block Elements** | Map to U+2580-U+259F (▀▄█▌▐░▒▓) - imperfect coverage |
| **Custom Font Glyphs** | OTF font includes all 64 sixel patterns in PUA |
| **Bitmap Fallback** | Render graphics chars as mini-bitmaps |
| **SVG Paths** | Generate vector sixel patterns |

For full fidelity, a custom font with sixel glyphs is recommended.

### Integration with Existing Architecture

The character output mode would be orthogonal to existing pixel output:

```
                    ┌─────────────────┐
                    │   Screen RAM    │
                    │   (0x7C00)      │
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
                    │    SAA5050      │
                    │  (processes     │
                    │   control       │
                    │   codes)        │
                    └───┬─────────┬───┘
                        │         │
           ┌────────────▼──┐  ┌───▼────────────┐
           │ Pixel Output  │  │ Character      │
           │ (PixelBatch)  │  │ Output         │
           │               │  │ (TeletextRow)  │
           └───────┬───────┘  └───────┬────────┘
                   │                  │
           ┌───────▼───────┐  ┌───────▼────────┐
           │ FrameRenderer │  │ External       │
           │ (framebuffer) │  │ Renderer       │
           └───────────────┘  │ (OTF/Web/etc)  │
                              └────────────────┘
```

### Considerations

1. **Flash timing** - External renderers need to know the current flash phase (on/off) or handle timing themselves

2. **Double-height state** - Must track which rows are top/bottom halves; state persists across rows

3. **Control code visibility** - Control codes (0x80-0x9F) are displayed as spaces but affect state; character output should reflect the visible result, not the raw bytes

4. **Held graphics** - When hold graphics is active, control codes display the last graphics character; this state must be captured

5. **Conceal/Reveal** - Concealed text should be flagged but the actual character code preserved (for "reveal" functionality)
