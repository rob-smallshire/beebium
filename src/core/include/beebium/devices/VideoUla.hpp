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
    // fast_clock=1, line_width=3: Mode 0 (640x256, 2 colors, 8 pixels/byte)
    // fast_clock=1, line_width=2: Mode 1 (320x256, 4 colors, 4 pixels/byte)
    // fast_clock=1, line_width=1: Mode 2 (160x256, 16 colors, 2 pixels/byte)
    // fast_clock=0, line_width=3: Mode 3 (80x25 text)
    // fast_clock=0, line_width=2: Mode 4 (320x256, 2 colors)
    // fast_clock=0, line_width=1: Mode 5 (160x256, 4 colors)
    // fast_clock=0, line_width=0: Mode 6 (40x25 text)
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
        cursor_pattern_ = cursor_active ? cursor_width_pattern() : 0;
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
                case 3: emit_8bpp(batch); break;  // Mode 0: 8 pixels/byte, 1bpp
                case 2: emit_4bpp(batch); break;  // Mode 1: 4 pixels/byte, 2bpp
                case 1: emit_2bpp(batch); break;  // Mode 2: 2 pixels/byte, 4bpp
                default: emit_blank(batch); break;
            }
        } else {
            // Low-res modes (Modes 3, 4, 5, 6) - 1MHz CRTC clock
            // Two bytes produce 8 pixels total (4 pixels per byte)
            switch (lw) {
                case 3: emit_8bpp_slow(batch); break;  // Mode 3: text
                case 2: emit_4bpp_slow(batch); break;  // Mode 4: 4 pixels/byte, 1bpp
                case 1: emit_2bpp_slow(batch); break;  // Mode 5: 2 pixels/byte, 2bpp
                default: emit_blank(batch); break;     // Mode 6: text
            }
        }

        // Apply cursor (XOR with white if cursor active)
        if (cursor_pattern_ & 1) {
            for (int i = 0; i < 8; ++i) {
                batch.pixels.pixels[i].value ^= 0x0FFF;  // XOR RGB
            }
        }
        cursor_pattern_ >>= fast_clock() ? 2 : 1;
    }

    // Emit blank pixels (during blanking period)
    void emit_blank(PixelBatch& batch) {
        batch.set_type(PixelBatchType::Nothing);
        batch.clear();
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

    void reset() {
        control_ = 0;
        palette_.fill(0);
        output_palette_.fill(VideoDataPixel{});
        work_byte_ = 0;
        cursor_pattern_ = 0;
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

    // Cursor width pattern based on control register bits 5-7
    uint8_t cursor_width_pattern() const {
        switch (cursor_width_bits()) {
            case 0: return 0x00;  // No cursor
            case 1: return 0x01;  // 1 byte wide
            case 2: return 0x03;  // 2 bytes wide
            case 3: return 0x0F;  // 4 bytes wide
            default: return 0xFF; // Full width
        }
    }

    // Mode 0: 8 pixels/byte, 1bpp (each pixel uses 1 bit)
    void emit_8bpp(PixelBatch& batch) {
        for (int i = 0; i < 8; ++i) {
            uint8_t idx = shift_pixel();
            batch.pixels.pixels[i] = get_pixel(idx);
        }
    }

    // Mode 1: 4 pixels/byte, 2bpp (each pixel uses 2 bits)
    void emit_4bpp(PixelBatch& batch) {
        for (int i = 0; i < 4; ++i) {
            uint8_t idx = shift_pixel();
            VideoDataPixel p = get_pixel(idx);
            // Double each pixel horizontally
            batch.pixels.pixels[i * 2] = p;
            batch.pixels.pixels[i * 2 + 1] = p;
        }
    }

    // Mode 2: 2 pixels/byte, 4bpp (each pixel uses 4 bits)
    void emit_2bpp(PixelBatch& batch) {
        for (int i = 0; i < 2; ++i) {
            uint8_t idx = shift_pixel();
            VideoDataPixel p = get_pixel(idx);
            // Quadruple each pixel horizontally
            batch.pixels.pixels[i * 4] = p;
            batch.pixels.pixels[i * 4 + 1] = p;
            batch.pixels.pixels[i * 4 + 2] = p;
            batch.pixels.pixels[i * 4 + 3] = p;
        }
    }

    // Slow modes (1MHz CRTC) - each byte produces 4 output pixels
    // but we need 8 pixels per batch, so we double everything

    void emit_8bpp_slow(PixelBatch& batch) {
        // 4 pixels from byte, doubled to 8
        for (int i = 0; i < 4; ++i) {
            uint8_t idx = shift_pixel();
            VideoDataPixel p = get_pixel(idx);
            batch.pixels.pixels[i * 2] = p;
            batch.pixels.pixels[i * 2 + 1] = p;
        }
    }

    void emit_4bpp_slow(PixelBatch& batch) {
        // 2 pixels from byte, quadrupled to 8
        for (int i = 0; i < 2; ++i) {
            uint8_t idx = shift_pixel();
            VideoDataPixel p = get_pixel(idx);
            batch.pixels.pixels[i * 4] = p;
            batch.pixels.pixels[i * 4 + 1] = p;
            batch.pixels.pixels[i * 4 + 2] = p;
            batch.pixels.pixels[i * 4 + 3] = p;
        }
    }

    void emit_2bpp_slow(PixelBatch& batch) {
        // 1 pixel from byte, replicated to 8
        uint8_t idx = shift_pixel();
        VideoDataPixel p = get_pixel(idx);
        batch.fill(p);
    }

    uint8_t control_ = 0;
    std::array<uint8_t, 16> palette_{};           // Logical to physical mapping
    std::array<VideoDataPixel, 16> output_palette_{};  // Pre-computed RGB values
    uint8_t work_byte_ = 0;                       // Current screen byte being shifted
    uint8_t cursor_pattern_ = 0;                  // Cursor display pattern
};

} // namespace beebium
