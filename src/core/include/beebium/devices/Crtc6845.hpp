#pragma once

#include <array>
#include <cstdint>

namespace beebium {

// MC6845 CRTC (Cathode Ray Tube Controller) - Register Stub
//
// The 6845 generates video timing signals and provides the address
// for reading character/pixel data from memory. This minimal implementation
// just stores registers for MOS initialization.
//
// Address mapping (offset & 1):
//   0: Address register (write selects register 0-17)
//   1: Data register (read/write selected register)
//
// Memory-mapped at 0xFE00-0xFE07 with Mirror<0x07>
//
class Crtc6845 {
public:
    // Register indices
    static constexpr uint8_t R0_HTOTAL          = 0;   // Horizontal total
    static constexpr uint8_t R1_HDISPLAYED      = 1;   // Horizontal displayed
    static constexpr uint8_t R2_HSYNC_POS       = 2;   // Horizontal sync position
    static constexpr uint8_t R3_SYNC_WIDTH      = 3;   // Sync widths
    static constexpr uint8_t R4_VTOTAL          = 4;   // Vertical total
    static constexpr uint8_t R5_VTOTAL_ADJ      = 5;   // Vertical total adjust
    static constexpr uint8_t R6_VDISPLAYED      = 6;   // Vertical displayed
    static constexpr uint8_t R7_VSYNC_POS       = 7;   // Vertical sync position
    static constexpr uint8_t R8_INTERLACE       = 8;   // Interlace and skew
    static constexpr uint8_t R9_MAX_SCANLINE    = 9;   // Max scanline address
    static constexpr uint8_t R10_CURSOR_START   = 10;  // Cursor start
    static constexpr uint8_t R11_CURSOR_END     = 11;  // Cursor end
    static constexpr uint8_t R12_START_ADDR_HI  = 12;  // Start address high
    static constexpr uint8_t R13_START_ADDR_LO  = 13;  // Start address low
    static constexpr uint8_t R14_CURSOR_HI      = 14;  // Cursor position high
    static constexpr uint8_t R15_CURSOR_LO      = 15;  // Cursor position low
    static constexpr uint8_t R16_LIGHTPEN_HI    = 16;  // Light pen high (read-only)
    static constexpr uint8_t R17_LIGHTPEN_LO    = 17;  // Light pen low (read-only)

    uint8_t read(uint16_t offset) {
        if (offset & 1) {
            // Data register read
            if (address_register_ < 18) {
                return registers_[address_register_];
            }
        }
        // Address register reads return 0
        return 0x00;
    }

    void write(uint16_t offset, uint8_t value) {
        if (offset & 1) {
            // Data register write
            if (address_register_ < 18) {
                registers_[address_register_] = value;
            }
        } else {
            // Address register write
            address_register_ = value & 0x1F;
        }
    }

    // Accessors for commonly needed values
    uint16_t screen_start() const {
        return (static_cast<uint16_t>(registers_[R12_START_ADDR_HI] & 0x3F) << 8) |
               registers_[R13_START_ADDR_LO];
    }

    uint16_t cursor_position() const {
        return (static_cast<uint16_t>(registers_[R14_CURSOR_HI] & 0x3F) << 8) |
               registers_[R15_CURSOR_LO];
    }

    uint8_t max_scanline() const { return registers_[R9_MAX_SCANLINE] & 0x1F; }

    // Direct register access for testing/debugging
    uint8_t reg(uint8_t index) const {
        return (index < 18) ? registers_[index] : 0;
    }

    void reset() {
        address_register_ = 0;
        registers_.fill(0);
    }

private:
    uint8_t address_register_ = 0;
    std::array<uint8_t, 18> registers_{};
};

} // namespace beebium
