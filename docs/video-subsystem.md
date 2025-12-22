# Beebium Video Subsystem

## Overview

The video subsystem produces a stream of `PixelBatch` objects at 2MHz (one per CPU cycle). Each batch contains 8 pixels representing 0.5μs of video output. Clients can consume the raw stream or use the optional `FrameRenderer` to produce a traditional framebuffer.

## Data Flow

```
CRTC 6845 ──► VideoULA ──► PixelBatch ──► OutputQueue ──► FrameRenderer ──► FrameBuffer
  (timing)    (pixels)      (8 px)      (lock-free)      (optional)       (double-buffered)
```

## Components

### Crtc6845

Generates timing signals and memory addresses. Produces `Output` struct with:
- 14-bit screen address
- HSYNC/VSYNC signals
- Display enable flag
- Cursor state

### VideoUla

Converts screen memory bytes to pixels. Handles:
- Mode-dependent pixel unpacking (1/2/4/8 bpp)
- Palette lookup (16 logical → 8 physical colors)
- Cursor rendering
- CRTC clock rate selection (1MHz/2MHz)

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
