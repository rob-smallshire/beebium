# Beebium Video Subsystem

## Overview

The video subsystem produces a stream of `PixelBatch` objects at 2MHz (one per CPU cycle). Each batch contains 8 pixels representing 0.5μs of video output. Clients can consume the raw stream or use the optional `FrameRenderer` to produce a traditional framebuffer.

## Data Flow

```
                         ┌─► VideoULA ─────┐
CRTC 6845 ──► screen ────┤                 ├──► PixelBatch ──► OutputQueue ──► FrameRenderer ──► FrameBuffer
  (timing)    memory     └─► SAA5050 ─────┘      (8 px)       (lock-free)      (optional)       (double-buffered)
                          (Mode 7 only)
```

The CRTC provides timing and addresses. Screen memory is read and passed to either the VideoULA (bitmap modes 0-6) or SAA5050 (teletext Mode 7), selected by the VideoULA's teletext mode bit.

## Components

### Crtc6845

Generates timing signals and memory addresses. Produces `Output` struct with:
- 14-bit screen address
- HSYNC/VSYNC signals
- Display enable flag
- Cursor state

### VideoUla

Converts screen memory bytes to pixels for bitmap modes 0-6. Handles:
- Mode-dependent pixel unpacking (1/2/4/8 bpp)
- Palette lookup (16 logical → 8 physical colors)
- Cursor rendering
- CRTC clock rate selection (1MHz/2MHz)
- Teletext mode detection (delegates to SAA5050)

### SAA5050 (Teletext Character Generator)

Renders Mode 7 teletext display. The SAA5050 is a dedicated chip that converts 7-bit character codes into pixel patterns using an internal character ROM.

**Architecture:**

```
Screen byte ──► byte() ──► Output Buffer ──► emit_pixels() ──► PixelBatch
                 │              (8 slots)          │
                 ▼                                 ▼
           Process control                  Font lookup
           codes & store                    & pixel gen
```

**4-Slot Output Delay Buffer:**

The SAA5050 models a 2μs propagation delay from character input (LOSE signal) to pixel output. This is implemented as an 8-slot circular buffer with read/write indices offset by 4 positions:

- `byte()` writes character data at `write_index`, then advances by 1
- `emit_pixels()` reads from `read_index`, then advances by 1
- Initial state: `write_index=4`, `read_index=0`
- Effect: 4 character delay between input and output

**Character Sets:**

| Charset | Range | Description |
|---------|-------|-------------|
| Alpha | 0x20-0x7F | Standard alphanumeric characters from ROM |
| ContiguousGraphics | 0x20-0x3F | 2×3 sixel blocks, adjacent |
| SeparatedGraphics | 0x20-0x3F | 2×3 sixel blocks with gaps |

**Control Codes (0x00-0x1F after masking to 7-bit):**

Control codes change rendering state but display as spaces:
- 0x01-0x07: Alpha colors (red through white)
- 0x11-0x17: Graphics colors
- 0x08: Flash, 0x09: Steady
- 0x0C: Normal height, 0x0D: Double height
- 0x18: Conceal
- 0x19: Contiguous graphics, 0x1A: Separated graphics
- 0x1C: Black background, 0x1D: New background
- 0x1E: Hold graphics, 0x1F: Release graphics

**Line/Frame Management:**

- `start_of_line()`: Reset per-line state (colors, charset, hold)
- `end_of_line()`: Advance raster counter by 2 (10 font rows → 20 scanlines)
- `vsync()`: Reset raster to 0, increment frame counter

**Font Data:**

96 characters × 10 rows × 6 bits per row, stored in `teletext_font.inl`. Each character is 6 pixels wide, rendered into 8-pixel batches (6 character + 2 spacing).

### PixelBatch

16-byte packet containing:
- 8 pixels (4-bit RGB each)
- Type (Bitmap/Teletext/Nothing)
- Flags (HSYNC/VSYNC/Display)

### OutputQueue

Lock-free SPSC circular buffer. Decouples core from consumers. Default capacity ~256K batches (~1 frame).

### FrameRenderer (optional)

Consumes queue, tracks raster position, writes BGRA32 pixels to framebuffer. Swaps buffers on VSYNC.

### FrameBuffer

Double-buffered with mutex-protected swap. Core writes to front buffer; clients read immutable back buffer. Version counter for change detection.

## Integration

Video output is optional. Call `ModelBHardware::enable_video_output()` to activate. The `tick_peripherals()` method clocks video hardware when enabled, pushing batches to the queue.

### ModelBHardware::tick_video()

The video pipeline is driven by `tick_video()`, called from `tick_peripherals()` at either 1MHz or 2MHz depending on the VideoULA's clock rate setting:

```cpp
void tick_video() {
    // 1. Tick CRTC to get timing and address
    auto crtc_output = crtc.tick();

    // 2. Translate CRTC address to BBC memory address
    uint16_t screen_addr = translate_screen_address(crtc_output.address);
    uint8_t screen_byte = crtc_output.display ? main_ram.read(screen_addr) : 0;

    // 3. Generate pixels (mode-dependent)
    PixelBatch batch;
    if (video_ula.teletext_mode()) {
        // Mode 7: SAA5050 teletext
        handle_teletext_timing(crtc_output);
        saa5050.byte(screen_byte, crtc_output.display ? 1 : 0);
        saa5050.emit_pixels(batch, bbc_colors::PALETTE);
    } else {
        // Modes 0-6: VideoULA bitmap
        video_ula.byte(screen_byte, crtc_output.cursor != 0);
        video_ula.emit_pixels(batch);  // or emit_blank()
    }

    // 4. Set sync flags and push to queue
    batch.set_flags(...);
    video_output->push(batch);
}
```

### Screen Address Translation

The CRTC outputs a 14-bit address. Translation depends on mode:

- **Mode 7**: Screen at 0x7C00-0x7FFF (1KB). Address = `0x7C00 | (crtc_addr & 0x3FF)`
- **Bitmap modes**: Screen base from addressable latch (IC32) bits 4-5:
  - 00: 0x3000, 01: 0x4000, 10: 0x5800, 11: 0x6000

### SAA5050 Timing Integration

The SAA5050 requires specific timing signals derived from CRTC output:

| CRTC Event | SAA5050 Call | Effect |
|------------|--------------|--------|
| VSYNC rising edge | `vsync()` | Reset raster, increment frame counter |
| HSYNC rising edge | `end_of_line()` | Advance raster by 2 |
| Display area start | `start_of_line()` | Reset per-line state |
| Each character | `byte()` + `emit_pixels()` | Feed char, get pixels |

## Current Status

**Implemented:**
- CRTC 6845 timing and address generation
- VideoULA mode detection and palette
- SAA5050 basic alpha character rendering
- Font data for all 96 printable characters
- Output delay buffer (4-slot lag)
- FrameBuffer double-buffering
- Boot screenshot capture showing "BBC Computer 32K"

**Pending (SAA5050 Phase 3-6):**
- Color control codes (foreground/background)
- Graphics characters (sixels)
- Flash/steady animation
- Double-height characters
- Hold graphics mode
- Conceal display
