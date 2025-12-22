#pragma once

#include <array>
#include <cstdint>

namespace beebium {

// Video ULA (Uncommitted Logic Array) - Register Stub
//
// The Video ULA handles:
// - Palette mapping (16 logical colors to 8 physical)
// - Display mode control (teletext, character width)
// - Cursor display
// - CRTC clock rate selection
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
    static constexpr uint8_t CTRL_CHARS_PER_LINE = 0x0C; // Bits 2-3: Characters per line
    static constexpr uint8_t CTRL_FAST_CLOCK    = 0x10;  // Bit 4: 2MHz CRTC clock
    static constexpr uint8_t CTRL_CURSOR_WIDTH  = 0xE0;  // Bits 5-7: Cursor width

    uint8_t read(uint16_t) {
        // Per BeebEm: reads from Video ULA return 0xFE
        return 0xFE;
    }

    void write(uint16_t offset, uint8_t value) {
        if (offset & 1) {
            // Palette register: upper nibble = index, lower nibble = color
            uint8_t index = (value >> 4) & 0x0F;
            // Physical color is XORed with 7 (per hardware design)
            palette_[index] = (value & 0x0F) ^ 0x07;
        } else {
            // Control register
            control_ = value;
        }
    }

    // Control register queries
    bool flash_select() const { return (control_ & CTRL_FLASH) != 0; }
    bool teletext_mode() const { return (control_ & CTRL_TELETEXT) != 0; }
    bool fast_clock() const { return (control_ & CTRL_FAST_CLOCK) != 0; }

    // Characters per line mode (0-3)
    // 0 = 10 chars, 1 = 20 chars, 2 = 40 chars, 3 = 80 chars
    uint8_t chars_per_line_mode() const {
        return (control_ >> 2) & 0x03;
    }

    uint8_t cursor_width() const { return (control_ >> 5) & 0x07; }

    // Palette access
    uint8_t palette(uint8_t index) const {
        return (index < 16) ? palette_[index] : 0;
    }

    uint8_t control() const { return control_; }

    void reset() {
        control_ = 0;
        palette_.fill(0);
    }

private:
    uint8_t control_ = 0;
    std::array<uint8_t, 16> palette_{};
};

} // namespace beebium
