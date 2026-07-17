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

#pragma once

#include <array>
#include <cstdint>
#include "../PixelBatch.hpp"

namespace beebium {

// Video ULA (Uncommitted Logic Array)
//
// The Video ULA handles:
// - Palette mapping (16 logical colors to 8 physical)
// - Display mode control (teletext, character width)
// - Cursor display
// - CRTC clock rate selection
// - Pixel generation from screen memory bytes
//
// Address mapping (offset & 1):
//   0: Control register (write-only)
//   1: Palette register (write-only)
//
// Memory-mapped at 0xFE20-0xFE2F with Mirror<0x01>
//
class VideoUla {
public:
    // Control register bits
    static constexpr uint8_t CTRL_FLASH         = 0x01;  // Bit 0: Flash color select
    static constexpr uint8_t CTRL_TELETEXT      = 0x02;  // Bit 1: Teletext mode
    static constexpr uint8_t CTRL_LINE_WIDTH    = 0x0C;  // Bits 2-3: Line width mode
    static constexpr uint8_t CTRL_FAST_CLOCK    = 0x10;  // Bit 4: 2MHz CRTC clock
    static constexpr uint8_t CTRL_CURSOR_WIDTH  = 0xE0;  // Bits 5-7: Cursor width

    // Video modes determined by line_width and fast_clock:
    // Slow clock modes use the same pixels-per-byte as fast modes;
    // the resolution difference comes from CRTC R1 (40 vs 80 chars/line).
    //
    // fast_clock=1, line_width=3: Mode 0 (640x256, 2 colors, 8 pixels/byte)
    // fast_clock=1, line_width=2: Mode 1 (320x256, 4 colors, 4 pixels/byte)
    // fast_clock=1, line_width=1: Mode 2 (160x256, 16 colors, 2 pixels/byte)
    // fast_clock=0, line_width=3: Mode 3 (80x25 text, 8 pixels/byte)
    // fast_clock=0, line_width=2: Mode 4 (320x256, 2 colors, 8 pixels/byte)
    // fast_clock=0, line_width=1: Mode 5 (160x256, 4 colors, 4 pixels/byte)
    // fast_clock=0, line_width=2: Mode 6 (40x25 text, same as Mode 4)
    // fast_clock=0, line_width=0: AUG Mode 8 (80x256, 16 colors, 10 chars/line)
    // teletext=1: Mode 7 (40x25 teletext)

    uint8_t read(uint16_t) const {
        // Per BeebEm: reads from Video ULA return 0xFE
        return 0xFE;
    }

    void write(uint16_t offset, uint8_t value) {
        if (offset & 1) {
            // Palette register: upper nibble = index, lower nibble = color
            uint8_t index = (value >> 4) & 0x0F;
            // Physical color is XORed with 7 (per hardware design)
            uint8_t physical = (value & 0x0F) ^ 0x07;
            palette_[index] = physical;
            // Update output palette with RGB values
            output_palette_[index] = bbc_colors::PALETTE[physical & 0x07];
        } else {
            // Control register
            control_ = value;
        }
    }

    // Feed a byte from screen memory to the ULA
    // Called when CRTC provides a new memory address
    void byte(uint8_t data, bool cursor_active) {
        work_byte_ = data;

        // Only load cursor pattern on rising edge of cursor_active.
        // This allows cursor to persist across multiple byte() calls,
        // spanning multiple batches for 8 logical pixels in all modes.
        if (cursor_active && !cursor_was_active_) {
            cursor_pattern_ = cursor_width_pattern();
        }
        cursor_was_active_ = cursor_active;
        // Note: cursor_pattern_ is NOT cleared here - it exhausts via shifting
    }

    // Emit pixels for one 2MHz cycle into a PixelBatch
    // This produces 8 pixels regardless of mode (pixel clock varies)
    void emit_pixels(PixelBatch& batch) {
        if (teletext_mode()) {
            // Mode 7 handled separately by SAA5050
            batch.set_type(PixelBatchType::Teletext);
            return;
        }

        batch.set_type(PixelBatchType::Bitmap);

        // Determine pixels per byte based on mode
        // line_width: 0=10 chars, 1=20 chars, 2=40 chars, 3=80 chars
        uint8_t lw = line_width_mode();

        if (fast_clock()) {
            // High-res modes (Modes 0, 1, 2) - 2MHz CRTC clock
            // Each byte produces 8 pixels at different color depths
            switch (lw) {
                case 3: emit_8_pixels_1bpp(batch); break;  // Mode 0: 8 pixels/byte, 1bpp
                case 2: emit_4_pixels_2bpp(batch); break;  // Mode 1: 4 pixels/byte, 2bpp
                case 1: emit_2_pixels_4bpp(batch); break;  // Mode 2: 2 pixels/byte, 4bpp
                default: emit_blank(batch); break;
            }
        } else {
            // Slow clock modes (1MHz CRTC) use same pixels-per-byte as fast modes.
            // The resolution difference comes from CRTC R1 (40 vs 80 chars/line).
            switch (lw) {
                case 3: emit_8_pixels_1bpp(batch); break;  // Mode 3 text: 8 pixels/byte
                case 2: emit_8_pixels_1bpp(batch); break;  // Mode 4: 8 pixels/byte (same as Mode 0)
                case 1: emit_4_pixels_2bpp(batch); break;  // Mode 5: 4 pixels/byte (same as Mode 1)
                case 0: emit_2_pixels_4bpp(batch); break;  // AUG Mode 8: 2 pixels/byte (16 colors)
                default: emit_blank(batch); break;
            }
        }

        // Apply cursor (XOR with white if cursor active)
        // Only XOR valid pixels based on current mode's pixel count
        if (cursor_pattern_ & 1) {
            uint8_t count = pixels_per_batch();
            for (int i = 0; i < count; ++i) {
                batch.pixels.pixels[i].value ^= 0x0FFF;  // XOR RGB
            }
        }
        // Shift by 1 bit per batch. Cursor pattern spans multiple batches
        // via edge detection in byte(), giving 8 logical pixels in all modes.
        cursor_pattern_ >>= 1;
    }

    // Emit blank pixels (during blanking or gap scanlines)
    // Pixel count matches current mode for consistent line width tracking.
    // Cursor is still applied - it should be visible on gap scanlines.
    void emit_blank(PixelBatch& batch) {
        batch.set_type(PixelBatchType::Nothing);
        batch.clear();
        batch.set_pixel_count(pixels_per_batch());

        // Apply cursor XOR even on blank scanlines (e.g., gap scanlines in MODE 3/6)
        if (cursor_pattern_ & 1) {
            uint8_t count = pixels_per_batch();
            for (int i = 0; i < count; ++i) {
                batch.pixels.pixels[i].value ^= 0x0FFF;  // XOR RGB
            }
        }
        cursor_pattern_ >>= 1;
    }

    // Return the number of logical pixels per batch for current mode
    uint8_t pixels_per_batch() const {
        uint8_t lw = line_width_mode();
        if (fast_clock()) {
            // Fast modes: 8, 4, 2, or 8 pixels per batch
            switch (lw) {
                case 3: return 8;  // Mode 0: 8 pixels/byte
                case 2: return 4;  // Mode 1: 4 pixels/byte
                case 1: return 2;  // Mode 2: 2 pixels/byte
                default: return 8; // Mode 3: 8 pixels/byte (text)
            }
        } else {
            // Slow clock modes: same pixels-per-batch as equivalent fast modes
            switch (lw) {
                case 3: return 8;  // Mode 3 text: 8 pixels/batch
                case 2: return 8;  // Mode 4: 8 pixels/batch (same as Mode 0)
                case 1: return 4;  // Mode 5: 4 pixels/batch (same as Mode 1)
                case 0: return 2;  // AUG Mode 8: 2 pixels/batch (16 colors)
                default: return 8;
            }
        }
    }

    // Control register queries
    bool flash_select() const { return (control_ & CTRL_FLASH) != 0; }
    bool teletext_mode() const { return (control_ & CTRL_TELETEXT) != 0; }
    bool fast_clock() const { return (control_ & CTRL_FAST_CLOCK) != 0; }

    // Line width mode (0-3)
    // 0 = 10 chars, 1 = 20 chars, 2 = 40 chars, 3 = 80 chars
    uint8_t line_width_mode() const {
        return (control_ >> 2) & 0x03;
    }

    uint8_t cursor_width_bits() const { return (control_ >> 5) & 0x07; }

    // Palette access (returns physical color 0-7)
    uint8_t palette(uint8_t index) const {
        return (index < 16) ? palette_[index] : 0;
    }

    // Output palette (returns VideoDataPixel with RGB)
    VideoDataPixel output_palette(uint8_t index) const {
        return (index < 16) ? output_palette_[index] : VideoDataPixel{};
    }

    uint8_t control() const { return control_; }

    // Zeroing here is a convention, not a model of the hardware: the real
    // Video ULA has no reset input. Its 28 pins are power, A0, chip select,
    // the data bus, the 16MHz-in/8-4-2-1MHz-out divider, RGB in and out, and
    // CURSOR/DISEN/INVERT/CRTC CLK -- there is nowhere for a reset to arrive,
    // and the service manual routes notRS only to the CPU and the expansion
    // connectors. So on real hardware &FE20 holds whatever its latches settled
    // to at power-on (no manual gives it a reset value) and BREAK cannot
    // disturb it, since BREAK asserts the same notRS as power-on.
    //
    // Two consequences. The state we come up in is invented, and bit 4 decides
    // whether the 6845 is clocked at 1 or 2MHz, which is why emulators disagree
    // about reset-state CRTC timing and why none of them is right: see
    // oracle/CYCLE_DIFFERENCE_INVESTIGATION.md and issue #58. And clearing this
    // on reset diverges from the hardware, which would hold the value. Both are
    // unobservable -- the MOS writes the register during reset before anything
    // can see it -- so this stays as it is rather than modelling indeterminacy
    // for no gain.
    void reset() {
        control_ = 0;
        palette_.fill(0);
        output_palette_.fill(VideoDataPixel{});
        work_byte_ = 0;
        cursor_pattern_ = 0;
        cursor_was_active_ = false;
    }

private:
    // Extract logical color index from work byte using BBC bit interleaving
    // Bits 7,5,3,1 form the 4-bit color index
    uint8_t shift_pixel() {
        uint8_t index = ((work_byte_ >> 4) & 8) |
                        ((work_byte_ >> 3) & 4) |
                        ((work_byte_ >> 2) & 2) |
                        ((work_byte_ >> 1) & 1);
        work_byte_ <<= 1;
        work_byte_ |= 1;
        return index;
    }

    // Get VideoDataPixel for a logical color index
    VideoDataPixel get_pixel(uint8_t logical_index) {
        return output_palette_[logical_index & 0x0F];
    }

    // Cursor width pattern for 8 logical pixels in all modes.
    // The MOS sets cursor_width_bits for constant physical size on screen,
    // but we override this to produce constant logical pixel width.
    //
    // The cursor persists across multiple byte() calls via cursor_was_active_
    // edge detection, allowing the pattern to span multiple batches.
    uint8_t cursor_width_pattern() const {
        if (cursor_width_bits() == 0) return 0x00;  // No cursor

        // Always produce 8 logical pixels (1 text character width)
        // MODE 0: 1 batch, MODE 1: 2 batches, MODE 2: 4 batches
        uint8_t batches = 8 / pixels_per_batch();
        return static_cast<uint8_t>((1u << batches) - 1);
    }

    // Mode 0 (fast): 8 pixels per byte, 1 bit per pixel
    // Output: 8 pixels per batch (640 pixels/line with R1=80)
    void emit_8_pixels_1bpp(PixelBatch& batch) {
        for (int i = 0; i < 8; ++i) {
            uint8_t idx = shift_pixel();
            batch.pixels.pixels[i] = get_pixel(idx);
        }
        batch.set_pixel_count(8);
    }

    // Mode 1 (fast): 4 pixels per byte, 2 bits per pixel
    // Output: 4 pixels per batch (320 pixels/line with R1=80)
    void emit_4_pixels_2bpp(PixelBatch& batch) {
        for (int i = 0; i < 4; ++i) {
            uint8_t idx = shift_pixel();
            batch.pixels.pixels[i] = get_pixel(idx);
        }
        batch.set_pixel_count(4);
    }

    // Mode 2 (fast): 2 pixels per byte, 4 bits per pixel
    // Output: 2 pixels per batch (160 pixels/line with R1=80)
    void emit_2_pixels_4bpp(PixelBatch& batch) {
        for (int i = 0; i < 2; ++i) {
            uint8_t idx = shift_pixel();
            batch.pixels.pixels[i] = get_pixel(idx);
        }
        batch.set_pixel_count(2);
    }

    uint8_t control_ = 0;
    std::array<uint8_t, 16> palette_{};           // Logical to physical mapping
    std::array<VideoDataPixel, 16> output_palette_{};  // Pre-computed RGB values
    uint8_t work_byte_ = 0;                       // Current screen byte being shifted
    uint8_t cursor_pattern_ = 0;                  // Cursor display pattern
    bool cursor_was_active_ = false;              // Track cursor_active for edge detection
};

} // namespace beebium
