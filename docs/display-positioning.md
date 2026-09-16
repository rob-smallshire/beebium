# Display Positioning

This document describes the display positioning implementation in Beebium.

## Current Implementation

### Display-Enable Based Positioning

Rather than using fixed offsets derived from CRTC timing registers, Beebium uses the CRTC's **display enable** signal to determine where content appears in the framebuffer:

**Vertical positioning:**
- When display enable first goes high after VSYNC, reset Y to 0
- This marks the start of the visible display area
- Scanlines before display enable become the top border

**Horizontal positioning:**
- Accumulate blanking width in 16MHz pixel clocks from HSYNC until display enable
  goes high (each batch spans 8 clocks at the 2MHz character clock, 16 at 1MHz)
- When display enable goes high, reset X to 0 and record the left border width
- Blanking after display enable ends contributes to the right border

All four borders and the line total are measured in 16MHz pixel clocks, the same
uniform grid as `display_width`. This is why a 1MHz mode's borders are twice a
2MHz mode's for the same blanking interval, and why the right border is computed
against the physical `display_width` rather than the logical pixel width.

**Files:**
- `src/core/include/beebium/FrameRenderer.hpp`

**Key implementation:**
```cpp
// Reset Y when first displayed scanline is reached
if (display && !was_displaying_) {
    top_border_ = y_;  // Scanlines from VSYNC to first display
    y_ = 0;            // First visible scanline
    was_displaying_ = true;
}

// Capture left border when display first goes high on a line
if (display && !was_displaying_line_) {
    left_border_ = blanking_clocks_;  // Already summed in 16MHz clocks
    x_ = 0;
    was_displaying_line_ = true;
}
```

### Border Tracking

The FrameRenderer tracks all four borders, the horizontal pair in 16MHz pixel
clocks:

| Border | Calculation |
|--------|-------------|
| `left_border` | Sum of blanking batches' clocks before display enable on each line |
| `right_border` | Total line clocks − left border − physical `display_width` |
| `top_border` | Scanlines from VSYNC to first display enable |
| `bottom_border` | Total frame scanlines − top border − displayed height |

These border dimensions are included in `FrameMetadata` and sent to clients via the gRPC `Frame` message. See [video-subsystem.md](video-subsystem.md#logical-pixel-output-and-client-scaling) for details on client-side handling.

This approach:
- Automatically adapts to different screen modes
- Works with custom CRTC timings (games like Elite, Exile)
- Provides accurate border dimensions for CRT-style rendering
- Handles Mode 7's different timing (1 MHz character clock, SAA5050 pixel expansion)

## Comparative Analysis

### B2's Approach: TV Timing State Machine
B2 models actual CRT TV timing with explicit constants:
```cpp
static const int HORIZONTAL_RETRACE_CYCLES = 8;   // 4 µs
static const int BACK_PORCH_CYCLES = 16;          // 8 µs
static const int SCAN_OUT_CYCLES = 104;           // 52 µs
```

The state machine transitions through scanout, retrace, and back porch phases. Display position emerges from accurate timing emulation.

### BeebEm's Approach: CRTC Register Calculation
BeebEm calculates position from CRTC registers:
```cpp
int InitialOffset = 0 - (((CRTC_HorizontalTotal + 1) / 2) -
                        (HSyncModifier == 8 ? 40 : 20));

int HStart = InitialOffset +
             (CRTC_HorizontalTotal + 1 -
              (CRTC_HorizontalSyncPos + (CRTC_SyncWidth & 0x0f))) +
             ((HSyncModifier == 8) ? 2 : 1);
```

For standard Mode 7 timing, this produces HStart ≈ 0.

### B-Em's Approach: Fixed Offset with Sync Adjustment
B-Em uses a fixed 128-pixel left margin, adjusted by sync width:
```c
scrx = 128 - ((crtc[3] & 15) * 4);  // High frequency mode
```

### Beebium's Approach: Display-Enable Positioning
Uses the CRTC's display enable signal directly rather than calculating offsets from timing registers. This naturally adapts to any CRTC configuration since display enable already encodes when visible content starts and ends.

## Advantages of Display-Enable Positioning

1. **Automatic adaptation**: Works with any CRTC register values without offset calculation
2. **Accurate borders**: Blanking periods are measured directly, not inferred from registers
3. **Mode independence**: Handles Mode 7's different character clock automatically
4. **Custom timing support**: Games that reprogram the CRTC work without special cases

## Future Directions

### 1. Border Color Configuration
~~Currently borders are rendered in debug colors (red/green/blue/yellow).~~ The macOS client now renders configurable border colors, with distinct colors for each border edge. Options for future work:
- Black borders (authentic CRT appearance) - the current default
- User-configurable border color via preferences
- Per-border color from CRTC/ULA state (if BBC had border color control)

### 2. Overscan/Cropping Options
Add user-configurable display options:
- **Full frame**: Show entire frame including borders (current behavior)
- **Visible area only**: Crop to displayed content (like most emulators)
- **TV safe area**: Crop to typical CRT overscan margins
- **Custom**: User-defined cropping percentages

### 3. CRT Shader Integration
Border dimensions enable authentic CRT rendering:
- Apply phosphor glow/bloom effects within visible area
- Proper handling of blanking in scanline effects
- Accurate aspect ratio including borders for PAL timing

## References

- B2: `/Users/rjs/Code/b2/src/beeb/src/TVOutput.cpp`
- BeebEm: `/Users/rjs/Code/beebem-mac/Src/Video.cpp` (AdjustVideo function)
- B-Em: `/Users/rjs/Code/b-em/src/video.c`
