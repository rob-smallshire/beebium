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
template<typename RamType>
class VideoRenderer {
public:
    VideoRenderer(
        const RamType& ram,
        const AddressableLatch& latch,
        VideoUla& video_ula,
        Saa5050& saa5050,
        std::optional<OutputQueue<PixelBatch>>& output
    )
        : ram_(ram)
        , latch_(latch)
        , video_ula_(video_ula)
        , saa5050_(saa5050)
        , output_(output)
    {}

    // Render video output from CRTC state
    void render(const Crtc6845::Output& crtc_output) {
        // Read screen memory byte at CRTC address
        uint16_t screen_addr = translate_screen_address(crtc_output.address);
        uint8_t screen_byte = crtc_output.display ? ram_.read(screen_addr) : 0;

        // Generate PixelBatch
        PixelBatch batch;

        if (video_ula_.teletext_mode()) {
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
        output_->push(batch);
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
        uint8_t screen_base_bits = latch_.screen_base();

        // Mode 7 uses different addressing
        if (video_ula_.teletext_mode()) {
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
            saa5050_.vsync();
            teletext_column_ = 0;
        }

        // Pass CRTC raster to SAA5050
        saa5050_.set_raster(crtc_output.raster);

        // Start of display area - reset per-line state
        if (crtc_output.display && teletext_column_ == 0) {
            saa5050_.start_of_line();
        }

        // Feed byte to SAA5050
        saa5050_.byte(screen_byte, crtc_output.display ? 1 : 0, crtc_output.cursor != 0);

        // Emit first batch (left half of character)
        saa5050_.emit_pixels(batch, bbc_colors::PALETTE);

        // Set sync flags
        uint8_t flags = VIDEO_FLAG_NONE;
        if (crtc_output.hsync) flags |= VIDEO_FLAG_HSYNC;
        if (crtc_output.vsync) flags |= VIDEO_FLAG_VSYNC;
        if (crtc_output.display) flags |= VIDEO_FLAG_DISPLAY;
        batch.set_flags(flags);

        output_->push(batch);

        // Emit second batch (right half of character)
        PixelBatch batch2;
        saa5050_.emit_pixels(batch2, bbc_colors::PALETTE);
        batch2.set_flags(flags);
        output_->push(batch2);

        if (crtc_output.display) {
            ++teletext_column_;
        }

        // Reset column counter when leaving display area
        if (!crtc_output.display && last_display_ && teletext_column_ > 0) {
            saa5050_.end_of_line();
            teletext_column_ = 0;
        }

        last_display_ = crtc_output.display;
        last_hsync_ = crtc_output.hsync;
        last_vsync_ = crtc_output.vsync;
    }

    void render_bitmap(PixelBatch& batch, const Crtc6845::Output& crtc_output, uint8_t screen_byte) {
        video_ula_.byte(screen_byte, crtc_output.cursor != 0);

        if (crtc_output.display) {
            video_ula_.emit_pixels(batch);
        } else {
            video_ula_.emit_blank(batch);
        }
    }

    const RamType& ram_;
    const AddressableLatch& latch_;
    VideoUla& video_ula_;
    Saa5050& saa5050_;
    std::optional<OutputQueue<PixelBatch>>& output_;

    // Teletext state tracking
    bool last_hsync_ = false;
    bool last_vsync_ = false;
    bool last_display_ = false;
    uint8_t teletext_column_ = 0;
};

// Factory function to create VideoRenderer with type deduction
template<typename RamType>
auto make_video_renderer(
    const RamType& ram,
    const AddressableLatch& latch,
    VideoUla& video_ula,
    Saa5050& saa5050,
    std::optional<OutputQueue<PixelBatch>>& output
) {
    return VideoRenderer<RamType>(ram, latch, video_ula, saa5050, output);
}

} // namespace beebium
