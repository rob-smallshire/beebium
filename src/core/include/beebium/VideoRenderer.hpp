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

#include "AddressableLatch.hpp"
#include "OutputQueue.hpp"
#include "PixelBatch.hpp"
#include "Saa5050.hpp"
#include "devices/Crtc6845.hpp"
#include "devices/Ram.hpp"
#include "devices/VideoUla.hpp"
#include <optional>

namespace beebium {

// VideoRenderer handles pixel generation from CRTC output.
//
// This class is responsible for:
// - Translating CRTC addresses to BBC screen memory addresses
// - Generating pixels from screen memory using VideoUla or SAA5050
// - Managing teletext line state
//
// Separated from ModelBHardware to achieve single responsibility:
// - ModelBHardware: Device ownership and memory map
// - VideoRenderer: Pixel generation and screen addressing
//
// The Hardware template parameter must provide:
// - peek_video(uint16_t addr) const -> uint8_t: Read video memory
// - crtc: Crtc6845 reference for character row geometry
// - addressable_latch: AddressableLatch reference for screen base
// - video_ula: VideoUla reference for mode info and pixel generation
// - saa5050: Saa5050 reference for teletext rendering
// - video_output: optional OutputQueue<PixelBatch> for output
//
template<typename Hardware>
class VideoRenderer {
public:
    explicit VideoRenderer(Hardware& hardware)
        : hardware_(hardware)
    {}

    // Render video output from CRTC state
    void render(const Crtc6845::Output& crtc_output) {
        // Read screen memory byte at CRTC address using peek_video() for shadow RAM support
        // For bitmap modes, raster line is part of the address (8 bytes per character)
        uint16_t screen_addr = translate_screen_address(crtc_output.address, crtc_output.raster);
        uint8_t screen_byte = crtc_output.display ? hardware_.peek_video(screen_addr) : 0;

        // Generate PixelBatch
        PixelBatch batch;

        if (hardware_.video_ula.teletext_mode()) {
            render_teletext(batch, crtc_output, screen_byte);
            return;  // Teletext pushes its own batches
        } else {
            render_bitmap(batch, crtc_output, screen_byte);
        }

        // Set sync flags
        uint8_t flags = VIDEO_FLAG_NONE;
        if (crtc_output.hsync) flags |= VIDEO_FLAG_HSYNC;
        if (crtc_output.vsync) flags |= VIDEO_FLAG_VSYNC;
        if (crtc_output.display) flags |= VIDEO_FLAG_DISPLAY;
        if (crtc_output.interlace) flags |= VIDEO_FLAG_INTERLACE;
        if (crtc_output.odd_field) flags |= VIDEO_FLAG_ODD_FIELD;
        batch.set_flags(flags);
        batch.set_char_scanlines(char_scanlines());

        // Push to output queue
        hardware_.video_output->push(batch);
    }

    // Reset renderer state
    void reset() {
        last_hsync_ = false;
        last_vsync_ = false;
        last_display_ = false;
        teletext_column_ = 0;
    }

    // Translate CRTC address and raster to BBC memory address
    //
    // The CRTC outputs:
    // - MA0-MA13: 14-bit character address
    // - RA0-RA4: 5-bit raster line within character (0-7 for 8-line chars)
    //
    // For bitmap modes, the screen memory is organized as 8 bytes per character
    // (one byte per scanline). The full address is: (char_addr * 8) + raster
    //
    // For teletext (Mode 7), bit 13 of the CRTC address is set, and only
    // the character address matters (raster is handled by the SAA5050).
    //
    // The screen_base bits in IC32 control wrap adjustment for large screens
    // that would otherwise exceed the 14-bit address space.
    //
    uint16_t translate_screen_address(uint16_t crtc_addr, uint8_t raster) const {
        // Check for teletext mode (CRTC address bit 13 set)
        if (crtc_addr & 0x2000) {
            // Mode 7: Screen at 0x7C00-0x7FFF (1KB)
            // Raster is handled by SAA5050, not used in address
            return 0x7C00 | (crtc_addr & 0x03FF);
        }

        // Bitmap modes: address = (char_addr * 8) + raster
        // But first handle wrap adjustment for large screens
        uint16_t addr = crtc_addr;

        if (addr & 0x1000) {
            // Screen wrap adjustment based on screen_base bits
            // These values match the BBC Micro hardware behavior
            static constexpr uint16_t SCREEN_WRAP_ADJUSTMENTS[] = {
                0x4000 >> 3,  // screen_base 0: 0x0800 (for 20KB modes 0,1,2)
                0x2000 >> 3,  // screen_base 1: 0x0400 (for 16KB mode 3)
                0x5000 >> 3,  // screen_base 2: 0x0A00 (for 10KB modes 4,5)
                0x2800 >> 3,  // screen_base 3: 0x0500 (for 8KB mode 6)
            };

            uint8_t screen_base_bits = hardware_.addressable_latch.screen_base();
            addr -= SCREEN_WRAP_ADJUSTMENTS[screen_base_bits & 0x03];
            addr &= ~0x1000u;  // Clear bit 12
        }

        // Combine: (char_addr * 8) + raster
        // Raster is 0-7 for standard 8-line characters
        return ((addr << 3) | (raster & 0x07)) & 0xFFFF;
    }

private:
    void render_teletext(PixelBatch& batch, const Crtc6845::Output& crtc_output, uint8_t screen_byte) {
        // Handle VSYNC rising edge
        if (crtc_output.vsync && !last_vsync_) {
            hardware_.saa5050.vsync();
            teletext_column_ = 0;
        }

        // Pass CRTC raster to SAA5050
        hardware_.saa5050.set_raster(crtc_output.raster);

        // Start of display area - reset per-line state
        if (crtc_output.display && teletext_column_ == 0) {
            hardware_.saa5050.start_of_line();
        }

        // Feed byte to SAA5050
        hardware_.saa5050.byte(screen_byte, crtc_output.display ? 1 : 0, crtc_output.cursor != 0);

        // Emit first batch (left half of character)
        hardware_.saa5050.emit_pixels(batch, bbc_colors::PALETTE);

        // Set sync flags
        uint8_t flags = VIDEO_FLAG_NONE;
        if (crtc_output.hsync) flags |= VIDEO_FLAG_HSYNC;
        if (crtc_output.vsync) flags |= VIDEO_FLAG_VSYNC;
        if (crtc_output.display) flags |= VIDEO_FLAG_DISPLAY;
        if (crtc_output.interlace) flags |= VIDEO_FLAG_INTERLACE;
        if (crtc_output.odd_field) flags |= VIDEO_FLAG_ODD_FIELD;
        batch.set_flags(flags);
        batch.set_char_scanlines(char_scanlines());

        hardware_.video_output->push(batch);

        // Emit second batch (right half of character)
        PixelBatch batch2;
        hardware_.saa5050.emit_pixels(batch2, bbc_colors::PALETTE);
        batch2.set_flags(flags);
        batch2.set_char_scanlines(char_scanlines());
        hardware_.video_output->push(batch2);

        if (crtc_output.display) {
            ++teletext_column_;
        }

        // Reset column counter when leaving display area
        if (!crtc_output.display && last_display_ && teletext_column_ > 0) {
            hardware_.saa5050.end_of_line();
            teletext_column_ = 0;
        }

        last_display_ = crtc_output.display;
        last_hsync_ = crtc_output.hsync;
        last_vsync_ = crtc_output.vsync;
    }

    // Scanlines per character row, from CRTC R9 + 1.
    //
    // Ridden on every batch so that reading text off the screen can recover the
    // grid pitch per band: the CRTC can be reprogrammed mid-frame, and the
    // pitch cannot be inferred from the pixels afterwards.
    uint8_t char_scanlines() const {
        return static_cast<uint8_t>(hardware_.crtc.max_scanline() + 1);
    }

    // Character data exists only for scanlines 0-7 within each character cell.
    // Scanlines 8+ are "gap scanlines" used in modes like MODE 3 (10-line cells)
    // and MODE 6 (8-line cells with gaps). These must render as blank.
    static constexpr bool has_character_data(uint8_t raster) {
        return raster < 8;
    }

    void render_bitmap(PixelBatch& batch, const Crtc6845::Output& crtc_output, uint8_t screen_byte) {
        hardware_.video_ula.byte(screen_byte, crtc_output.cursor != 0);

        if (crtc_output.display && has_character_data(crtc_output.raster)) {
            hardware_.video_ula.emit_pixels(batch);
        } else {
            hardware_.video_ula.emit_blank(batch);
        }
    }

    Hardware& hardware_;

    // Teletext state tracking
    bool last_hsync_ = false;
    bool last_vsync_ = false;
    bool last_display_ = false;
    uint8_t teletext_column_ = 0;
};

// Factory function to create VideoRenderer with type deduction
template<typename Hardware>
auto make_video_renderer(Hardware& hardware) {
    return VideoRenderer<Hardware>(hardware);
}

} // namespace beebium
