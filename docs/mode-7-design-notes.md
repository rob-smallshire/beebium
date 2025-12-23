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

## Implementation Path for Beebium

If enhanced rendering quality is desired in the future:

### Phase 1: Pre-computed Antialiased Font
1. Change font storage from `uint8_t[96][10]` to `uint16_t[2][3][96][20]`
2. Implement `Get16WideRow()` - expand 6 bits to 12 bits
3. Implement `GetAARow()` - edge smoothing algorithm
4. Pre-compute AA and non-AA variants at startup

### Phase 2: Metadata Output
1. Add teletext type marker to PixelBatch
2. Change `emit_pixels()` to output metadata:
   - pixel[0] = background + type marker
   - pixel[1] = foreground
   - pixel[2-3] = font bitmap data

### Phase 3: TV Output Filter
1. Create `TeletextRenderer` class or enhance `FrameRenderer`
2. Detect teletext pixel batches by type marker
3. Implement gamma-corrected blend table
4. Expand font data to final pixels with blending

## Key Takeaways

1. **Architectural separation** - B2 separates emulation (SAA5050 outputs metadata) from presentation (TVOutput does gamma blending)

2. **Pre-computed antialiasing** - Edge smoothing calculated once at startup, stored in expanded font table

3. **Gamma-corrected blending** - Proper perceptual color mixing when expanding pixels to screen resolution

4. **Memory vs computation trade-off** - Pre-computed font table is ~15KB but eliminates per-frame antialiasing calculations

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
