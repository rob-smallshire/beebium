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
        uint16_t screen_addr = translate_screen_address(crtc_output.address);
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
        batch.set_flags(flags);

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

    // Translate CRTC address to BBC memory address
    uint16_t translate_screen_address(uint16_t crtc_addr) const {
        // Screen base from addressable latch bits 4-5
        uint8_t screen_base_bits = hardware_.addressable_latch.screen_base();

        // Mode 7 uses different addressing
        if (hardware_.video_ula.teletext_mode()) {
            // Mode 7: Screen at 0x7C00-0x7FFF (1KB)
            return 0x7C00 | (crtc_addr & 0x03FF);
        }

        // Graphics modes: Screen base determines start address
        // Bits 4-5 of latch: 00=0x3000, 01=0x4000, 10=0x5800, 11=0x6000
        uint16_t base;
        switch (screen_base_bits) {
            case 0: base = 0x3000; break;
            case 1: base = 0x4000; break;
            case 2: base = 0x5800; break;
            case 3: base = 0x6000; break;
            default: base = 0x3000; break;
        }

        return base + (crtc_addr & 0x3FFF);
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
        batch.set_flags(flags);

        hardware_.video_output->push(batch);

        // Emit second batch (right half of character)
        PixelBatch batch2;
        hardware_.saa5050.emit_pixels(batch2, bbc_colors::PALETTE);
        batch2.set_flags(flags);
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

    void render_bitmap(PixelBatch& batch, const Crtc6845::Output& crtc_output, uint8_t screen_byte) {
        hardware_.video_ula.byte(screen_byte, crtc_output.cursor != 0);

        if (crtc_output.display) {
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
