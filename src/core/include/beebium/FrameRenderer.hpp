// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#ifndef BEEBIUM_FRAME_RENDERER_HPP
#define BEEBIUM_FRAME_RENDERER_HPP

#include "PixelBatch.hpp"
#include "OutputQueue.hpp"
#include "FrameBuffer.hpp"
#include <array>
#include <cstdint>

namespace beebium {

// CRTC timing parameters for calculating display offsets.
// These are derived from CRTC registers and Video ULA settings.
struct DisplayTiming {
    uint8_t h_total = 127;           // R0: Horizontal Total (chars - 1)
    uint8_t h_displayed = 80;        // R1: Horizontal Displayed (chars)
    uint8_t h_sync_pos = 98;         // R2: Horizontal Sync Position
    uint8_t sync_width = 0x28;       // R3: H sync (low 4 bits), V sync (high 4 bits)
    uint8_t v_total = 38;            // R4: Vertical Total (rows - 1)
    uint8_t v_total_adjust = 0;      // R5: Vertical Total Adjust (scanlines)
    uint8_t v_displayed = 32;        // R6: Vertical Displayed (rows)
    uint8_t v_sync_pos = 34;         // R7: Vertical Sync Position
    uint8_t interlace_mode = 0;      // R8: Interlace and Skew
    uint8_t max_scanline = 7;        // R9: Max scanline address
    uint8_t pixels_per_char = 8;     // From Video ULA (8 for high freq, 16 for low)
};

// Converts PixelBatch stream to pixel framebuffer.
//
// The FrameRenderer consumes PixelBatches from an OutputQueue,
// converts them to BGRA32 pixels, and writes them to a FrameBuffer.
// It handles sync signals (HSYNC/VSYNC) to track raster position.
//
// Display positioning uses the CRTC display enable signal: Y resets
// to 0 when display enable goes high (first visible scanline), and
// X resets to 0 at the start of each visible line. This provides
// correct positioning based on actual CRTC timing, not hardcoded offsets.
//
// This is an optional convenience component. Clients that want
// raw PixelBatch access (e.g., for CRT shaders) can consume
// the queue directly without using FrameRenderer.
//
class FrameRenderer {
public:
    explicit FrameRenderer(FrameBuffer* frame_buffer)
        : frame_buffer_(frame_buffer)
        , x_(0)
        , y_(0)
        , in_vsync_(false)
        , in_hsync_(false)
        , horizontal_offset_(0)
        , vertical_offset_(0)
        , odd_field_(false)
        , field_offset_(0)
    {
        // Calculate initial offsets from default timing
        update_timing(DisplayTiming{});
    }

    // Update display timing from CRTC registers.
    // Call this when CRTC registers change, or at least once per frame.
    // TODO: Wire up actual CRTC registers for dynamic offset calculation.
    // For now, use empirical fixed offsets that match B2/BeebEm positioning.
    void update_timing(const DisplayTiming& timing) {
        timing_ = timing;

        // Small negative offset to compensate for blanking period before display
        // TODO: Calculate proper offset from CRTC H-sync position register
        horizontal_offset_ = 0;
        vertical_offset_ = 0;
    }

    // Process a batch of PixelBatches from the queue.
    // Returns number of batches consumed.
    // Should be called periodically (e.g., in render thread).
    size_t process(OutputQueue<PixelBatch>& queue, size_t max_units = 1000) {
        auto buffers = queue.get_consumer_buffer();
        if (buffers.empty()) {
            return 0;
        }

        size_t consumed = 0;
        size_t to_consume = std::min(max_units, buffers.total());

        // Process buffer A
        for (size_t i = 0; i < std::min(to_consume, buffers.a.size()); ++i) {
            process_unit(buffers.a[i]);
            ++consumed;
        }

        // Process buffer B (wrap-around portion)
        if (consumed < to_consume && !buffers.b.empty()) {
            for (size_t i = 0; i < std::min(to_consume - consumed, buffers.b.size()); ++i) {
                process_unit(buffers.b[i]);
                ++consumed;
            }
        }

        queue.consume(consumed);
        return consumed;
    }

    // Process a single PixelBatch
    void process_unit(const PixelBatch& batch) {
        uint8_t flags = batch.flags();
        bool vsync = (flags & VIDEO_FLAG_VSYNC) != 0;
        bool hsync = (flags & VIDEO_FLAG_HSYNC) != 0;
        bool display = (flags & VIDEO_FLAG_DISPLAY) != 0;
        bool interlace = (flags & VIDEO_FLAG_INTERLACE) != 0;

        // Track interlace mode from VIDEO_FLAG_INTERLACE
        in_interlace_mode_ = interlace;

        // Handle VSYNC rising edge - finalize frame and swap buffers
        if (vsync && !in_vsync_) {
            // Capture frame scanline count before reset
            max_frame_scanlines_ = std::max(max_frame_scanlines_, frame_scanline_count_);
            frame_scanline_count_ = 0;

            if (in_interlace_mode_) {
                // Latch CRTC's odd_field at VSYNC. The CRTC toggles odd_field at "end of
                // vertical displayed" (mid-frame), so by VSYNC time it reflects the NEXT
                // field that's about to start.
                latched_odd_field_ = (batch.flags() & VIDEO_FLAG_ODD_FIELD) != 0;

                // In interlace mode, a complete frame consists of two fields:
                // - Field 1 (odd_field=true): renders to framebuffer rows 0,2,4...
                // - Field 2 (odd_field=false): renders to framebuffer rows 1,3,5...
                //
                // We must swap buffers AFTER both fields complete, which is at the VSYNC
                // that starts the NEXT frame's field 1. If we swap at field 2's VSYNC,
                // we'd clear field 1's content before field 2 renders.
                //
                // Timing:
                // - VSYNC with odd_field=true: Field 1 about to start, previous Field 2 just ended
                //   → Finish and swap the completed frame, then start new frame
                // - VSYNC with odd_field=false: Field 2 about to start, Field 1 just ended
                //   → Don't swap yet, continue building current frame
                if (latched_odd_field_) {
                    finish_frame();
                }
            } else {
                // Non-interlace mode - finish frame every VSYNC
                finish_frame();
            }
            y_ = 0;  // Reset vertical position for new field
            was_displaying_ = false;  // New field starting
        }
        in_vsync_ = vsync;

        // Handle HSYNC rising edge - new scanline
        if (hsync && !in_hsync_) {
            // Capture line width before reset
            max_line_pixels_ = std::max(max_line_pixels_, line_pixel_count_);
            line_pixel_count_ = 0;
            blanking_count_ = 0;
            ++frame_scanline_count_;  // Count all scanlines including blanking

            ++y_;
            x_ = 0;  // Reset horizontal position for new scanline
            was_displaying_line_ = false;  // New line starting
            // Use capacity (physical allocation) for wrap check, not logical height
            if (y_ >= frame_buffer_->capacity_height()) {
                y_ = 0;  // Wrap around if we exceed buffer
            }
        }
        in_hsync_ = hsync;

        // Count ALL batches for total line width (before early return)
        line_pixel_count_ += 8;

        // Reset Y when first displayed scanline is reached (display enable rising edge)
        // This positions content correctly regardless of CRTC VSYNC timing variations.
        // Capture the pre-reset y_ as the top border (scanlines before display).
        //
        // Use the minimum of current and previous top_border to prevent shimmer from
        // interlace field-length alternation. When the CRTC dummy raster adds one
        // extra blanking scanline on even fields, the distance from VSYNC to display
        // alternates by 1. Taking the minimum stabilizes the value.
        if (display && !was_displaying_) {
            size_t candidate = y_;
            if (prev_top_border_ > 0 && candidate > prev_top_border_) {
                top_border_ = prev_top_border_;  // Use shorter (stable) value
            } else {
                top_border_ = candidate;
            }
            prev_top_border_ = candidate;
            y_ = 0;  // First visible scanline in this field
            was_displaying_ = true;
        }

        // Count blanking batches and return early (don't write pixels during blanking)
        if (!display) {
            ++blanking_count_;
            return;
        }

        // Capture left border when display first goes high on a line
        if (display && !was_displaying_line_) {
            left_border_ = blanking_count_ * 8;  // Convert batches to pixels
            x_ = 0;
            was_displaying_line_ = true;
        }

        // Calculate write position (no offsets needed with display-enable reset)
        int write_x = static_cast<int>(x_);
        int write_y;
        if (in_interlace_mode_) {
            // Interlace: interleave fields on alternating framebuffer lines
            // Use the batch's odd_field directly to determine framebuffer row offset:
            // - odd_field=true (raster 0,2,4...) → offset=0 → write to rows 0,2,4...
            // - odd_field=false (raster 1,3,5...) → offset=1 → write to rows 1,3,5...
            // This directly maps raster values to framebuffer rows regardless of boot timing.
            // Note: odd_field toggles at "end of vertical displayed" (mid-frame), but all
            // visible pixels (display=true) have the correct value for their field.
            bool batch_odd_field = (batch.flags() & VIDEO_FLAG_ODD_FIELD) != 0;
            int field_offset = batch_odd_field ? 0 : 1;
            write_y = static_cast<int>(y_) * 2 + field_offset;
        } else {
            write_y = static_cast<int>(y_);
        }

        // Convert PixelBatch pixels to BGRA32 and write to framebuffer
        // Use pixel_count() for variable-width batches (mode-dependent)
        uint8_t pixel_count = batch.pixel_count();

        // Use capacity for bounds checking (physical allocation size)
        if (write_x >= 0 && write_x + pixel_count <= static_cast<int>(frame_buffer_->capacity_width()) &&
            write_y >= 0 && write_y < static_cast<int>(frame_buffer_->capacity_height())) {
            uint32_t* dest = frame_buffer_->write_ptr(static_cast<size_t>(write_x),
                                                       static_cast<size_t>(write_y));
            for (int i = 0; i < pixel_count; ++i) {
                dest[i] = pixel_to_bgra32(batch.pixels.pixels[i]);
            }

            // Track frame bounds for logical dimensions
            max_x_written_ = std::max(max_x_written_, static_cast<size_t>(write_x + pixel_count));
            max_y_written_ = std::max(max_y_written_, static_cast<size_t>(write_y + 1));
        }

        x_ += pixel_count;  // Advance by actual pixel count

        // Track per-scanline pixel width for split-screen region detection,
        // and alongside it the character geometry those pixels were drawn
        // with. The video path has no use for the geometry, but reading text
        // off the screen cannot recover it from the pixels afterwards.
        if (write_y >= 0 && write_y < static_cast<int>(video_constants::FRAME_HEIGHT)) {
            scanline_pixel_widths_[write_y] = std::max(
                scanline_pixel_widths_[write_y],
                static_cast<uint16_t>(x_));
            scanline_char_scanlines_[write_y] = batch.char_scanlines();
            scanline_is_teletext_[write_y] =
                batch.type() == PixelBatchType::Teletext ? 1 : 0;
        }
    }

    // Get current raster position (for debugging)
    size_t x() const { return x_; }
    size_t y() const { return y_; }

    // Reset renderer state
    void reset() {
        x_ = 0;
        y_ = 0;
        in_vsync_ = false;
        in_hsync_ = false;
        odd_field_ = false;
        field_offset_ = 0;
        in_interlace_mode_ = false;
        latched_odd_field_ = true;   // Match CRTC reset odd_field=true
        was_displaying_ = false;
        was_displaying_line_ = false;
        max_x_written_ = 0;
        max_y_written_ = 0;
        top_border_ = 0;
        prev_top_border_ = 0;
        left_border_ = 0;
        line_pixel_count_ = 0;
        max_line_pixels_ = 0;
        frame_scanline_count_ = 0;
        max_frame_scanlines_ = 0;
        prev_max_frame_scanlines_ = 0;
        blanking_count_ = 0;
        scanline_pixel_widths_.fill(0);
        scanline_char_scanlines_.fill(0);
        scanline_is_teletext_.fill(0);
    }

    // Get tracked frame dimensions (for debugging/testing)
    size_t max_x_written() const { return max_x_written_; }
    size_t max_y_written() const { return max_y_written_; }

private:
    // Finalize frame at swap: set logical dimensions and metadata
    void finish_frame() {
        // Capture final line width
        max_line_pixels_ = std::max(max_line_pixels_, line_pixel_count_);

        // Use tracked dimensions, but default to capacity if nothing was written
        // (can happen on first frame or if display was never enabled)
        size_t frame_width = max_x_written_ > 0 ? max_x_written_ : frame_buffer_->capacity_width();
        size_t frame_height = max_y_written_ > 0 ? max_y_written_ : frame_buffer_->capacity_height();

        // Set logical dimensions to match actual content
        frame_buffer_->set_dimensions(frame_width, frame_height);

        // Build and store metadata
        FrameMetadata meta;
        meta.width = static_cast<uint32_t>(frame_width);
        meta.height = static_cast<uint32_t>(frame_height);
        meta.frame_number = frame_buffer_->version() + 1;
        meta.interlaced = in_interlace_mode_;

        // BBC Micro always displays at 640 pixels wide (physical CRT width)
        // regardless of logical resolution. Client scales width→display_width.
        // Scale factors: MODE 0=1x, MODE 1=2x, MODE 2=4x, MODE 4=2x, MODE 5=4x
        meta.display_width = 640;
        meta.display_height = static_cast<uint32_t>(frame_height);

        // Calculate borders from tracked values.
        //
        // Guard against inflated tracking variables caused by dropped OutputQueue
        // batches.  When the queue is full the producer silently drops batches,
        // including those carrying HSYNC flags.  Without HSYNC resets,
        // line_pixel_count_ accumulates across many lines, inflating
        // max_line_pixels_ to millions.  The maximum plausible single-line width
        // for any BBC Micro mode is (R0+1) * 2_batches * 8_pixels = 4096.
        static constexpr size_t MAX_PLAUSIBLE_LINE_PIXELS = 4096;

        meta.left_border = static_cast<uint32_t>(left_border_);
        meta.top_border = static_cast<uint32_t>(top_border_);

        // right_border = total_line_width - left_border - displayed_width
        if (max_line_pixels_ <= MAX_PLAUSIBLE_LINE_PIXELS &&
            max_line_pixels_ > left_border_ + frame_width) {
            meta.right_border = static_cast<uint32_t>(max_line_pixels_ - left_border_ - frame_width);
        }

        // bottom_border = total_scanlines - top_border - displayed_height
        // In interlace mode, max_frame_scanlines_ is per-field, frame_height is composited
        //
        // Use the maximum of current and previous frame's scanline count to prevent
        // shimmer from interlace field-length alternation (dummy raster adds 1 scanline
        // on alternating fields). The rolling max stabilizes after 2 frames.
        size_t stable_frame_scanlines = std::max(max_frame_scanlines_, prev_max_frame_scanlines_);
        size_t displayed_scanlines = in_interlace_mode_ ? frame_height / 2 : frame_height;
        if (stable_frame_scanlines > top_border_ + displayed_scanlines) {
            meta.bottom_border = static_cast<uint32_t>(stable_frame_scanlines - top_border_ - displayed_scanlines);
        }

        // Compress per-scanline pixel width and character geometry into
        // contiguous regions. A change to any of the three ends a region: a
        // band is a run of scanlines a single reader can treat alike.
        meta.regions.clear();
        if (frame_height > 0) {
            // Find first non-zero width (skip any leading gap scanlines)
            uint16_t current_width = 0;
            uint8_t current_char_scanlines = 0;
            uint8_t current_is_teletext = 0;
            uint32_t region_start = 0;
            for (uint32_t y = 0; y < frame_height; ++y) {
                uint16_t w = scanline_pixel_widths_[y];
                // Zero-width scanlines inherit the previous region's geometry
                if (w == 0) continue;

                const uint8_t cs = scanline_char_scanlines_[y];
                const uint8_t tt = scanline_is_teletext_[y];

                if (current_width == 0) {
                    // First non-zero width line
                    current_width = w;
                    current_char_scanlines = cs;
                    current_is_teletext = tt;
                    region_start = y;
                } else if (w != current_width
                           || cs != current_char_scanlines
                           || tt != current_is_teletext) {
                    // Geometry changed - close previous region, start new one
                    meta.regions.push_back({region_start, y, current_width,
                                            current_char_scanlines,
                                            current_is_teletext != 0});
                    current_width = w;
                    current_char_scanlines = cs;
                    current_is_teletext = tt;
                    region_start = y;
                }
            }
            // Close final region
            if (current_width > 0) {
                meta.regions.push_back({region_start, static_cast<uint32_t>(frame_height),
                                        current_width, current_char_scanlines,
                                        current_is_teletext != 0});
            }
        }

        // Reset per-scanline tracking for next frame
        const size_t tracked = std::min(frame_height, static_cast<size_t>(video_constants::FRAME_HEIGHT));
        std::fill(scanline_pixel_widths_.begin(),
                  scanline_pixel_widths_.begin() + tracked, uint16_t{0});
        std::fill(scanline_char_scanlines_.begin(),
                  scanline_char_scanlines_.begin() + tracked, uint8_t{0});
        std::fill(scanline_is_teletext_.begin(),
                  scanline_is_teletext_.begin() + tracked, uint8_t{0});

        frame_buffer_->set_metadata(meta);

        // Swap buffers (no reallocation - just pointer swap)
        frame_buffer_->swap();

        // Clear new write buffer to black for next frame
        // (Gap scanlines in MODE 3/6 will remain black)
        frame_buffer_->clear(0x00000000);

        // Reset tracking for next frame
        max_x_written_ = 0;
        max_y_written_ = 0;
        max_line_pixels_ = 0;
        prev_max_frame_scanlines_ = max_frame_scanlines_;
        max_frame_scanlines_ = 0;
    }


    // Convert a 4-bit-per-channel VideoDataPixel to BGRA32
    static uint32_t pixel_to_bgra32(VideoDataPixel pixel) {
        // VideoDataPixel: bits 0-3 blue, 4-7 green, 8-11 red
        // BGRA32: bits 0-7 blue, 8-15 green, 16-23 red, 24-31 alpha
        auto b = static_cast<uint8_t>((pixel.bits.b << 4) | pixel.bits.b);  // 4-bit to 8-bit
        auto g = static_cast<uint8_t>((pixel.bits.g << 4) | pixel.bits.g);
        auto r = static_cast<uint8_t>((pixel.bits.r << 4) | pixel.bits.r);
        return (0xFF << 24) | (r << 16) | (g << 8) | b;
    }

    FrameBuffer* frame_buffer_;
    size_t x_;  // Current horizontal pixel position
    size_t y_;  // Current scanline
    bool in_vsync_;
    bool in_hsync_;
    DisplayTiming timing_;
    int horizontal_offset_;  // Pixels from HSYNC end to display start
    int vertical_offset_;    // Scanlines from VSYNC end to display start
    bool odd_field_;         // For interlace: alternates each frame
    int field_offset_;       // 0 or 1 for interlace field positioning
    bool in_interlace_mode_ = false;   // True when interlace mode detected
    bool latched_odd_field_ = true;    // Latched at VSYNC; init matches CRTC reset odd_field=true
    bool was_displaying_ = false;      // Frame-level: have we seen display=true this frame?
    bool was_displaying_line_ = false; // Line-level: have we seen display=true this line?

    // Track frame dimensions from actual pixel writes
    size_t max_x_written_ = 0;
    size_t max_y_written_ = 0;

    // Track border dimensions (blanking before active area)
    size_t top_border_ = 0;          // Scanlines from VSYNC to first display
    size_t prev_top_border_ = 0;     // Previous field's top border (for stabilization)
    size_t left_border_ = 0;         // Pixels from HSYNC to first display

    // Track total line/frame dimensions (including blanking)
    size_t line_pixel_count_ = 0;      // All batches this line (reset at HSYNC)
    size_t max_line_pixels_ = 0;       // Maximum line width seen this frame
    size_t frame_scanline_count_ = 0;  // All scanlines this frame (reset at VSYNC)
    size_t max_frame_scanlines_ = 0;        // Maximum frame height seen this frame
    size_t prev_max_frame_scanlines_ = 0;   // Previous frame's max (for rolling max)

    // Blanking tracking for left border calculation
    size_t blanking_count_ = 0;        // Blanking batches since HSYNC

    // Per-scanline pixel width tracking for split-screen region detection
    std::array<uint16_t, video_constants::FRAME_HEIGHT> scanline_pixel_widths_{};

    // Per-scanline character geometry, compressed into the same regions. A
    // region breaks on a change to any of the three, so a band is a run of
    // scanlines sharing one character geometry as well as one pixel width.
    std::array<uint8_t, video_constants::FRAME_HEIGHT> scanline_char_scanlines_{};
    std::array<uint8_t, video_constants::FRAME_HEIGHT> scanline_is_teletext_{};
};

} // namespace beebium

#endif // BEEBIUM_FRAME_RENDERER_HPP
