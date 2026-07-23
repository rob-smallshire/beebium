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
- Raster counter (scanline within character row)
- Interlace and field state

**Interlace dummy raster:**

When interlace sync is enabled (R8 bit 0 = 1), the CRTC inserts an extra "dummy raster" scanline on alternating fields. This produces 312/313 scanline field alternation (averaging 312.5 = exactly 50Hz PAL). Without the dummy raster, both fields would be 312 scanlines (50.08Hz), causing timer-based split-screen effects like Revs' palette switching to drift visibly.

The dummy raster is a non-displayed scanline (`v_display=false`) inserted between the end of vertical adjust (or end of vertical total when R5=0) and the frame restart. It is triggered by the `in_dummy_raster_` state when `is_interlace_sync() && do_even_frame_logic_` is true. This matches B2's `in_dummy_raster` mechanism (`crtc.cpp:364-366`).

The half-scanline VSYNC offset (from `is_vsync_point()`) and the dummy raster combine so that VSYNC-to-VSYNC periods are constant at 20,000 character clocks (312.5 scanlines), even though individual fields alternate between 312 and 313 scanlines.

**Edge cases tested (from beebjit):**
- VSYNC width 0 = 16 scanlines (per Hitachi 6845 datasheet)
- R6=0 quirk: first scanline still displays before v_display clears
- R6 > R7: VSYNC fires before display ends (Caesar's Travels)
- R6 > R4: field state freezes (R6 never hit)
- Small R0 values and frame boundary recovery
- Mid-scanline R6/R7 register changes
- 1MHz/2MHz clock speed switching
- Interlace dummy raster produces constant VSYNC period with R8=1 and R8=3
- Non-interlace (R8=0) produces constant frame length with no dummy raster

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

**Naming: "teletext" vs "Mode 7".** These are not synonyms, and the codebase
currently uses the first where it often means the second. See
[Naming: teletext and Mode 7](#naming-teletext-and-mode-7).

### PixelBatch

16-byte packet containing:
- 8 pixels (4-bit RGB each)
- Type (Bitmap/Teletext/Nothing)
- Flags (HSYNC/VSYNC/DISPLAY/INTERLACE/ODD_FIELD) stored across two 4-bit fields

### OutputQueue

Lock-free SPSC circular buffer. Decouples core from consumers. Default capacity ~256K batches (~1 frame).

### FrameRenderer (optional)

Consumes queue, tracks raster position, writes BGRA32 pixels to framebuffer. Swaps buffers on VSYNC.

**Key features:**

- **Display-enable positioning**: Resets Y to 0 when display enable first goes high (start of visible area), resets X at line start. This positions content correctly regardless of CRTC sync timing variations.

- **Border tracking**: Counts all pixel batches (including blanking) to calculate four border dimensions:
  - `left_border`: Blanking pixels before display enable on each line
  - `right_border`: Total line width minus left border minus displayed width
  - `top_border`: Scanlines from VSYNC to first display enable
  - `bottom_border`: Total frame height minus top border minus displayed height

- **Border stabilization for interlace field alternation**: The interlace dummy raster adds one extra blanking scanline on alternating fields, which would cause `top_border` and `bottom_border` to oscillate by 1 each frame, shifting the display vertically in the client. Two stabilization mechanisms prevent this:
  - `top_border` uses the minimum of the current and previous field's value (`prev_top_border_`), absorbing the extra blanking scanline from the dummy raster.
  - `bottom_border` uses a rolling maximum of `max_frame_scanlines_` across two consecutive frames (`prev_max_frame_scanlines_`), so the longer field sets the stable value. Both stabilizations settle within 2 frames and track mode changes correctly.

- **Interlace support**: Detects interlace mode via `VIDEO_FLAG_INTERLACE`, composites both fields into a single framebuffer (even field → even lines, odd field → odd lines), swaps buffers every other VSYNC.

- **Dynamic dimensions**: Tracks maximum X/Y written to determine actual frame dimensions. Sets logical width/height in `FrameMetadata` at swap time.

- **Split-screen region tracking**: Tracks per-scanline pixel width to detect mid-frame mode changes. In `finish_frame()`, compresses the per-scanline data into contiguous `FrameDisplayRegion` entries (runs of scanlines sharing the same logical pixel width). Zero-width gap scanlines inherit the previous region's width to avoid spurious boundaries. Most frames produce a single region; split-screen games like Elite produce two or more.

### FrameBuffer

Double-buffered with mutex-protected swap. Core writes to front buffer; clients read immutable back buffer. Version counter for change detection.

## Integration

Video output is optional. Call `ModelBHardware::enable_video_output()` to activate.

### Clock Rate

The CRTC character clock rate depends on the current display mode:
- **Mode 7 (teletext)**: 1 MHz character clock. The CRTC ticks on falling edges only (every other 2 MHz cycle).
- **Modes 0-6 (bitmap)**: 2 MHz character clock. The CRTC ticks on both rising and falling edges (every 2 MHz cycle).

The Video ULA's control register determines the rate (`fast_clock` bit). `Machine::step()` reads this via `VideoBinding::clock_rate()` and ticks the video binding accordingly. Getting this wrong halves the VSYNC frequency in bitmap modes (25 Hz instead of 50 Hz).

### Video Pipeline

The video pipeline is driven by `VideoBinding::tick_falling()`, called from `Machine::step()` at the appropriate rate:

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

### Mode 7 Screen Memory Layout

Mode 7 is a 40×25 character teletext display:

| Property | Value |
|----------|-------|
| Screen start | $7C00 |
| Screen end | $7FFF |
| Size | 1000 bytes (1024 allocated) |
| Characters per row | 40 |
| Rows | 25 |
| Bytes per character | 1 |

Memory is laid out sequentially:

```
$7C00: Row 0, columns 0-39
$7C28: Row 1, columns 0-39
$7C50: Row 2, columns 0-39
...
$7FC8: Row 24, columns 0-39
```

Row offset = row_number × 40 (decimal) = row_number × $28 (hex)

### MOS VDU Variables

Key MOS variables for screen handling:

| Address | Name | Description |
|---------|------|-------------|
| $0350-$0351 | vduScreenTopAddress | Start of screen memory |
| $034E-$034F | vduCurrentTextCell | Current cursor position (character offset) |
| $0355 | screenMode | Current display mode (0-7) |

### Character Output Path

When OSWRCH ($FFEE) is called:
1. JMP through WRCHV ($020E) → default handler at $E0A4
2. VDU handler processes character
3. For printable characters in Mode 7:
   - Calculate screen address from cursor position
   - Write character byte directly to screen memory
   - Increment cursor position
4. The actual write happens at $CFE6 in the MOS

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
- CRTC 6845 timing and address generation (including interlace mode)
- Interlace dummy raster for correct 312/313 field alternation (R8 bit 0)
- VideoULA mode detection and palette
- SAA5050 teletext character generator (complete):
  - All 96 printable characters with pre-computed antialiased font
  - Color control codes (foreground/background)
  - Graphics characters (contiguous and separated sixels)
  - Flash/steady animation
  - Double-height characters
  - Hold graphics mode
  - Conceal display
  - Gamma-corrected 6→8 pixel blending (B2-quality rendering)
- FrameBuffer double-buffering with metadata
- FrameRenderer with display-enable positioning and border tracking
- Border stabilization for interlace field alternation (top and bottom)
- Dynamic frame dimensions (adapts to mode changes)
- Full border calculation (left, right, top, bottom)
- Interlace field compositing for Mode 7
- Logical pixel output with client-side scaling metadata
- Split-screen region tracking for mid-frame mode changes (e.g., Elite, Revs)

## Logical Pixel Output and Client Scaling

### Overview

The BBC Micro displays all screen modes at the same physical CRT size, but different modes have different logical resolutions:

| Mode | Logical Width | Display Width | Horizontal Scale |
|------|--------------|---------------|------------------|
| MODE 0 | 640 | 640 | 1× |
| MODE 1 | 320 | 640 | 2× |
| MODE 2 | 160 | 640 | 4× |
| MODE 3 | 640 | 640 | 1× |
| MODE 4 | 320 | 640 | 2× |
| MODE 5 | 160 | 640 | 4× |
| MODE 6 | 640 | 640 | 1× |
| MODE 7 | 480* | 480 | 1× |

*Mode 7 uses the SAA5050 teletext generator with 6→8 pixel expansion, producing 480 output pixels.

### Design Philosophy

Rather than pre-scaling pixels in the core (which would require interpolation decisions), Beebium outputs **logical pixels** and provides **display dimension metadata**. This approach:

1. **Preserves pixel fidelity**: Golden master tests can compare logical pixels directly
2. **Enables client choice**: Clients can use nearest-neighbor, bilinear, or CRT shader scaling
3. **Simplifies the core**: No scaling logic needed in VideoULA or FrameRenderer
4. **Supports flexible output**: Same frame data works for tests, framebuffers, and video streams

### PixelBatch Variable Width

The `PixelBatch` struct supports variable pixel counts per batch:

```cpp
struct PixelBatch {
    PixelData pixels;           // 8 pixel slots
    PixelBatchType type;        // Bitmap, Teletext, or Nothing
    uint8_t flags;              // HSYNC, VSYNC, Display

    // Variable pixel count (1-8), stored in pixels[2].bits.x
    void set_pixel_count(uint8_t count);
    uint8_t pixel_count() const;
};
```

The VideoULA emits different pixel counts based on mode:

| Bits per Pixel | Pixels per Batch | Modes |
|----------------|------------------|-------|
| 1 bpp | 8 pixels | MODE 0 |
| 2 bpp | 4 pixels | MODE 1 |
| 4 bpp | 2 pixels | MODE 2 |

Note: Text modes (MODE 3, 6) and slow clock modes (MODE 4, 5) have different pixel/batch counts but are less commonly used for graphics.

### FrameMetadata Display Dimensions

The `FrameMetadata` struct includes target display dimensions and per-region geometry:

```cpp
struct FrameDisplayRegion {
    uint32_t start_line = 0;    // First scanline (inclusive, 0-based)
    uint32_t end_line = 0;      // Last scanline (exclusive)
    uint32_t pixel_width = 0;   // Logical pixel width for scanlines in this region
};

struct FrameMetadata {
    // ... existing fields ...

    // Target display resolution for client scaling
    uint32_t display_width = 640;   // Target width (always 640)
    uint32_t display_height = ...;  // Set by FrameRenderer to frame_height
    bool interlaced = false;        // True for MODE 7 and custom interlace

    // Display regions for split-screen modes
    // Always populated with at least one region
    std::vector<FrameDisplayRegion> regions;
};
```

The `FrameRenderer` sets these in `finish_frame()`. It compresses per-scanline pixel widths into contiguous regions:

```cpp
void finish_frame() {
    meta.display_width = 640;
    meta.display_height = static_cast<uint32_t>(frame_height);

    // Compress per-scanline pixel widths into regions
    // e.g., Elite: [{0, 192, 320}, {192, 248, 160}]
    meta.regions = compress_scanline_widths(frame_height);
    // ... swap buffers ...
}
```

### gRPC Frame Message

The `video.proto` Frame message includes display dimensions and per-region geometry:

```protobuf
message DisplayRegion {
    uint32 start_line = 1;      // First scanline (inclusive, 0-based)
    uint32 end_line = 2;        // Last scanline (exclusive)
    uint32 pixel_width = 3;     // Logical pixel width for this region
}

message Frame {
    uint64 frame_number = 1;
    uint32 width = 3;           // Logical width (max across all regions)
    uint32 height = 4;          // Logical height (scanlines)
    bytes pixels = 5;           // BGRA32 at logical resolution

    // Border dimensions
    uint32 left_border = 7;
    uint32 right_border = 8;
    uint32 top_border = 9;
    uint32 bottom_border = 10;

    // Target display dimensions for scaling
    uint32 display_width = 11;  // Target width (typically 640)
    uint32 display_height = 12; // Target height (typically 256)

    // Per-region display geometry for split-screen modes
    repeated DisplayRegion regions = 13;
}
```

### Client-Side Scaling

Clients receive frames at logical resolution and scale to display dimensions:

```
Core Output          gRPC Transport       Client Rendering
───────────          ──────────────       ────────────────
MODE 1: 320×256  ──► Frame {              Scale 320→640 (2×)
                     width: 320           Line-double if
                     height: 256          field_order=PROGRESSIVE
                     display_width: 640
                     display_height: 256
                     field_order: PROGRESSIVE
                     regions: [{0, 256, 320}]
                    }
```

The `field_order` field tells clients whether to apply line-doubling:
- `PROGRESSIVE`: Non-interlaced mode, apply ×2 effective height
- `EVEN_FIRST`/`ODD_FIRST`: Interlaced mode, use height as-is

TODO: Why not have display_height always be the final height after line-doubling? If we did that,
clients would know that line-doubling is necessary just by comparing height vs display_height. It
would be much clearer to API consumers what the final output size should be.

### Split-Screen Mode Scaling

Games like Elite and Revs reprogram the CRTC mid-frame to switch video mode partway down the screen. This produces different logical pixel widths for different horizontal bands of the display. The `regions` field in the Frame message describes these bands.

**Example: Elite (MODE 4 upper / MODE 5 lower)**

```
Core Output              gRPC Transport              Client Rendering
───────────              ──────────────              ────────────────
Upper: 320×192 (MODE 4)  Frame {                     Upper: 320→640 (2×)
Lower: 160×56  (MODE 5)    width: 320                Lower: 160→640 (4×)
                            height: 248
                            display_width: 640
                            regions: [
                              {0, 192, 320},   // Upper: 320 px/line
                              {192, 248, 160}  // Lower: 160 px/line
                            ]
                          }
```

The frame texture is `width` pixels wide (the maximum across all regions). Narrower regions occupy only the left portion of the texture — the remaining pixels are black. The client shader uses the per-region `pixel_width` to scale each band independently:

```metal
// For each fragment, find its region and compute texture U accordingly
uint regionPixelWidth = uniforms.regions[regionIndex].pixelWidth;
texU = (displayX / displayWidth) * float(regionPixelWidth) / textureWidth;
```

This stretches the 160-pixel dashboard to fill the same 640 display pixels as the 320-pixel space view, matching the BBC Micro's physical CRT behaviour.

For uniform-mode frames (most games), there is a single region spanning all scanlines and the scaling is equivalent to the simple `textureSize → displaySize` mapping.

#### Metal Shader Example (macOS Client)

The macOS client uses a Metal shader with aspect ratio correction, line-doubling, and per-region scaling:

```metal
struct RegionUniforms {
    uint startLine;
    uint endLine;
    uint pixelWidth;
    uint padding;
};

struct Uniforms {
    float2 drawableSize;      // Window size
    float2 textureSize;       // Logical texture dimensions (e.g., 320×256)
    float2 displaySize;       // Target display dimensions (e.g., 640×256)
    float2 totalSize;         // Display size + borders
    float2 borderOffset;      // Left and top border offsets
    float parScale;           // Pixel Aspect Ratio (0.96 for BBC)
    uint interlaced;          // 1 = interlaced, 0 = progressive
    // ... border colors
    uint regionCount;         // Number of display regions
    RegionUniforms regions[8];
};
```

The vertex shader applies PAR correction and line-doubling to calculate the correct aspect ratio. The fragment shader uses per-region scaling for the horizontal axis: it maps the fragment's Y coordinate to a texture scanline, looks up the matching region, and uses that region's `pixelWidth` to compute the texture U coordinate. For uniform-mode frames (single region), this is equivalent to simple linear scaling.

### Testing with Logical Pixels

Golden master tests benefit from logical pixel output:

```cpp
TEST_CASE("MODE 1 test card") {
    machine.memory().set_startup_screen_mode(1);
    machine.reset();
    // ... run and render ...

    // Frame is 320×256 - direct pixel comparison
    // No scaling artifacts to account for
    REQUIRE(frame.width() == 320);
    compare_golden_master("mode1_testcard.ppm", frame);
}
```

Test images are stored at logical resolution, making visual inspection and comparison straightforward.

## Display Geometry, Aspect Ratio, and Line-Doubling

### Physical BBC Micro Display

The BBC Micro uses a standard PAL CRT display with fixed timing constraints:

- **Horizontal line time**: ~64μs total (~52μs active)
- **Vertical frame time**: 20ms (50Hz refresh)
- **Display aspect ratio**: 4:3

All screen modes display at the same physical CRT size, but vary in:
- Number of pixels per line (controlled by CRTC R1 and pixel clock)
- Number of scanlines per frame (controlled by CRTC R4, R6)
- Whether interlaced (controlled by CRTC R8)

### Interlace vs Non-Interlace

| Mode Type | CRTC R8 | Scanlines/Field | Field Alternation | Frame Swap |
|-----------|---------|-----------------|-------------------|------------|
| MODE 0-6 (bitmap) | 0x00 | 312 | None (progressive) | Every VSYNC |
| Custom (e.g., Revs) | 0x01 | 312/313 | Interlace sync timing only | Every VSYNC |
| MODE 7 (teletext) | 0x03 | 312/313 | Interlace sync + video | Every 2nd VSYNC |

**Non-interlaced modes (MODE 0-6, R8=0x00):**
- Single field per frame with sequential scanlines
- 32 character rows × 8 scanlines = 256 displayed, 312 total per frame
- VSYNC occurs once per frame; buffer swap on every VSYNC
- No dummy raster; constant frame length

**Interlace sync only (R8=0x01):**
- Used by custom screen modes like Revs for precise frame timing
- Display is progressive (no raster doubling or field interleaving)
- Dummy raster adds one extra scanline on alternating fields: 312/313
- VSYNC has half-scanline offset on even fields (from `is_vsync_point()`)
- The offset and dummy raster combine to produce constant 20,000 character clock VSYNC-to-VSYNC periods (312.5 scanlines average = exactly 50Hz)
- Timer-based split-screen effects rely on this exact frame period
- Buffer swap on every VSYNC (same as non-interlace)

**Interlace sync and video (MODE 7, R8=0x03):**
- Two fields per frame: even lines in field 1, odd lines in field 2
- 25 character rows × 10 scanlines = 250 displayed lines per field
- Dummy raster produces 312/313 scanline field alternation (same as R8=1)
- Raster counter increments by 2 and comparison is halved (raster doubling)
- VSYNC occurs twice per frame; buffer swap on every other VSYNC
- 250 × 2 = 500 total scanlines composited into framebuffer

### Line-Doubling

On a physical CRT, each scanline has the same vertical height regardless of mode. This means:

- **Interlaced MODE 7**: 500 scanlines fill the full vertical space
- **Non-interlaced bitmap modes**: 256 scanlines are "line-doubled" by the CRT electron beam

Line-doubling means each scanline is displayed twice, effectively making 256 scanlines occupy the same vertical space as 512 interlaced lines. This is not pixel stretching—it's how CRTs physically work.

**Effective heights for aspect ratio calculation:**

| Mode | Actual Lines | Line-Doubled | Effective Height |
|------|--------------|--------------|------------------|
| MODE 7 (interlaced) | 500 | No | 500 |
| MODE 0-6 (non-interlaced) | 256 | Yes (×2) | 512 |
| Custom (e.g., Revs 208 lines) | 208 | Yes (×2) | 416 |

### Pixel Aspect Ratio (PAR)

BBC Micro pixels are not square. The Pixel Aspect Ratio (PAR) describes how wide a pixel is relative to its height.

**PAR = 0.96** (from B2 emulator reference)

This means BBC pixels are slightly narrower than they are tall. The PAR accounts for:
- The difference between pixel clock timing and CRT horizontal scan rate
- Standard PAL display geometry

### Aspect Ratio Calculation

The displayed aspect ratio combines PAR with line-doubling:

```
contentWidth = totalWidth × PAR
effectiveHeight = totalHeight × (interlaced ? 1 : 2)
aspectRatio = contentWidth / effectiveHeight
```

**Examples:**

| Mode | Width | Height | PAR | Effective Height | Aspect Ratio |
|------|-------|--------|-----|------------------|--------------|
| MODE 7 | 640* | 500 | 0.96 | 500 | 1.23 |
| MODE 0 | 640 | 256 | 0.96 | 512 | 1.20 |
| MODE 1 | 320→640 | 256 | 0.96 | 512 | 1.20 |
| Revs | 640 | 208 | 0.96 | 416 | 1.48 (letterbox) |

*MODE 7 outputs 480 pixels but is padded/scaled to match border calculations.

The ~2.4% difference between MODE 7 (1.23) and bitmap modes (1.20) is physically correct—MODE 7 has 12 fewer effective scanlines (500 vs 512).

### Custom CRTC Modes

Games can reprogram the CRTC for custom display modes:

- **Revs**: 208 scanlines for letterbox effect (416 effective, aspect 1.48)
- **Boffin**: 720 pixels wide (aspect 1.35 with 512 effective height)
- **Elite**: Split-screen mode with MODE 4 upper (320 px) and MODE 5 lower (160 px), using VIA timer to switch video mode at the dashboard boundary

The line-doubling approach correctly handles these custom modes:
- Fewer scanlines → taller aspect ratio (letterbox)
- More pixels → wider aspect ratio
- No forced 4:3—the actual CRTC timing determines geometry
- Split-screen modes → per-region horizontal scaling in the client shader

### Frame Metadata

The server sends frame metadata that clients use for correct display:

```protobuf
message Frame {
    uint32 width = 3;           // Logical pixel width (max across all regions)
    uint32 height = 4;          // Scanline count
    uint32 display_width = 11;  // Target width (640 for horizontal scaling)
    uint32 display_height = 12; // Target height (same as height)
    FieldOrder field_order = 6; // PROGRESSIVE or EVEN_FIRST/ODD_FIRST
    repeated DisplayRegion regions = 13;  // Per-region pixel widths
}
```

**Client interpretation:**
1. If `field_order == PROGRESSIVE`: Apply line-doubling (×2 effective height)
2. If `field_order != PROGRESSIVE`: Use height as-is (already interlaced)
3. Apply PAR (0.96) to width
4. Calculate aspect ratio for letterbox/pillarbox fitting
5. Use `regions` for per-band horizontal scaling (split-screen modes)

### Interlace State Tracking

The CRTC supports three interlace modes via R8:

| R8 Value | Mode | CRTC Behaviour | Renderer Behaviour |
|----------|------|----------------|--------------------|
| 0x00 | Non-interlace | Constant 312 scanline frames | Progressive, swap every VSYNC |
| 0x01 | Interlace sync | Dummy raster, half-scanline VSYNC offset | Progressive, swap every VSYNC |
| 0x03 | Interlace sync + video | Dummy raster, VSYNC offset, raster doubling | Field compositing, swap every 2 VSYNCs |

The `VIDEO_FLAG_INTERLACE` flag in PixelBatch reflects `interlace_sync_and_video()` (R8 bits 0+1 = 3), which is only true for Mode 7. The renderer uses this to decide between progressive rendering and field compositing:

```cpp
in_interlace_mode_ = interlace;  // Updated each frame, not sticky
```

With R8=1 (interlace sync only, as used by Revs), the display is progressive but the CRTC produces alternating 312/313 scanline fields via the dummy raster. The renderer treats this as progressive (frame swap every VSYNC) but uses border stabilization to prevent the alternating field lengths from causing display shimmer.

This correctly handles:
- MODE 7 boot → MODE 0 switch (interlace turns off)
- MODE 7 → Revs custom mode (R8=1, interlace sync without video)
- `*TV` command changing interlace settings
- Direct CRTC programming via VDU 23

The interlace flag is passed through to clients via `field_order` in the Frame message.

## Naming: teletext and Mode 7

**Teletext** is the broadcast data service -- pages carried in the vertical
blanking interval of a television signal. **Mode 7** is the BBC screen mode
that borrows the SAA5050, teletext's character generator, to draw a 40x25
character display out of whatever the machine put in screen memory. Most Mode 7
screens have never been near a broadcast in their lives.

Beebium may one day emulate the **Acorn Teletext Adapter**: a 1 MHz bus
peripheral with a TV tuner that receives real teletext pages and hands them to
the computer. That is teletext in the proper sense, and it needs the word.

So:

| Concept | Name it | Why |
|---|---|---|
| The SAA5050 and its character ROM | **teletext** | It genuinely is a teletext character generator, and its repertoire genuinely is the teletext character set. An adapter feeding the same chip does not make these names wrong -- it makes them correctly shared. |
| The character set / repertoire choice | **teletext** | Same reason: `TeletextCharacters`, `TeletextCharacterSet` describe the chip's repertoire, not the screen. |
| The screen, its grid, its capture | **Mode 7** | A display concern. An adapter would feed *into* it and does not own it. This is where a peripheral service would collide head-on. |
| Anything a user reads | **Mode 7** | Users of a BBC micro know the mode by its number. The Edit menu says `Mode 7 Copies As` for exactly this reason. |

By that rule, the one thing that clearly names the *screen* rather than the chip
is the `is_teletext` flag on a screen-text `Band`, which marks a band of the
Mode 7 display.

**`GetTeletextScreen` and its cells are correctly named, as of the 2026-07
decision to keep them.** They were going to be retired -- `GetScreenText`
supersedes them for text and reads every mode -- but the retirement was
conditional on nothing needing the attribute-rich teletext cells, and something
does: they are the only exposure of the SAA5050's per-cell state (colour,
character set, concealment, flash, double height, control-code). So the RPC
stays, repositioned as the teletext *attribute* API, and that role is
teletext in the rule's own sense -- it carries chip state, not a rendering of
the screen. `TeletextGrid`, `TeletextScreenCell` and the `teletext_screen()`
wrappers name that same chip state and keep the word honestly. What moved to
`GetScreenText` was the *text*, which is the Mode 7 concern; what stayed
teletext is the chip.

So the sweeping rename this section once anticipated mostly evaporated: there is
no retirement to trigger it, and most of what looked misnamed turned out to name
the chip after all. The live tension that remains is small and local -- chiefly
whether the `is_teletext` band flag ought to read `mode7` -- and can be settled
if and when the Acorn Teletext Adapter arrives and the two senses of the word
sit side by side in one codebase.

Recorded here rather than in the discussion document that raised it
(`discussion/teletext-repertoire-choice.md`), because a naming rule is only
worth writing down where the next person to break it will be reading.

# Ideas

- a CRT shader in the client, like this: https://blog.gingerbeardman.com/2026/01/04/webgl-crt-shader/
- visual testing with disc images: Revs.ssd, Thrust.ssd, EliteD.ssd
