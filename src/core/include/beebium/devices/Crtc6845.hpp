#pragma once

#include <array>
#include <cstdint>

namespace beebium {

// MC6845 CRTC (Cathode Ray Tube Controller)
//
// The 6845 generates video timing signals and provides the memory address
// for reading character/pixel data from screen memory.
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
    static constexpr uint8_t R0_HTOTAL          = 0;   // Horizontal total (chars - 1)
    static constexpr uint8_t R1_HDISPLAYED      = 1;   // Horizontal displayed (chars)
    static constexpr uint8_t R2_HSYNC_POS       = 2;   // Horizontal sync position
    static constexpr uint8_t R3_SYNC_WIDTH      = 3;   // Sync widths (H low nibble, V high)
    static constexpr uint8_t R4_VTOTAL          = 4;   // Vertical total (rows - 1)
    static constexpr uint8_t R5_VTOTAL_ADJ      = 5;   // Vertical total adjust (scanlines)
    static constexpr uint8_t R6_VDISPLAYED      = 6;   // Vertical displayed (rows)
    static constexpr uint8_t R7_VSYNC_POS       = 7;   // Vertical sync position
    static constexpr uint8_t R8_INTERLACE       = 8;   // Interlace and skew
    static constexpr uint8_t R9_MAX_SCANLINE    = 9;   // Max scanline address (rows - 1)
    static constexpr uint8_t R10_CURSOR_START   = 10;  // Cursor start scanline
    static constexpr uint8_t R11_CURSOR_END     = 11;  // Cursor end scanline
    static constexpr uint8_t R12_START_ADDR_HI  = 12;  // Start address high (6 bits)
    static constexpr uint8_t R13_START_ADDR_LO  = 13;  // Start address low
    static constexpr uint8_t R14_CURSOR_HI      = 14;  // Cursor position high
    static constexpr uint8_t R15_CURSOR_LO      = 15;  // Cursor position low
    static constexpr uint8_t R16_LIGHTPEN_HI    = 16;  // Light pen high (read-only)
    static constexpr uint8_t R17_LIGHTPEN_LO    = 17;  // Light pen low (read-only)

    // Output signals from the CRTC - updated by tick()
    struct Output {
        uint16_t address : 14;   // Memory address (MA0-MA13)
        uint16_t raster : 5;     // Current scanline within character row (RA0-RA4)
        uint16_t hsync : 1;      // Horizontal sync
        uint16_t vsync : 1;      // Vertical sync
        uint16_t display : 1;    // Display enable (active during visible area)
        uint16_t cursor : 1;     // Cursor display at current position
    };

    // Read from CRTC register
    uint8_t read(uint16_t offset) const {
        if (offset & 1) {
            // Data register read - only some registers are readable
            if (address_register_ >= 12 && address_register_ <= 17) {
                return registers_[address_register_];
            }
        }
        return 0x00;
    }

    // Write to CRTC register
    void write(uint16_t offset, uint8_t value) {
        if (offset & 1) {
            // Data register write
            if (address_register_ < 18) {
                registers_[address_register_] = value & register_masks_[address_register_];
            }
        } else {
            // Address register write
            address_register_ = value & 0x1F;
        }
    }

    // Advance CRTC state by one character clock
    // Returns the current output signals
    // lightpen: state of light pen input (active high)
    Output tick(bool lightpen = false) {
        // Capture light pen position on rising edge
        if (lightpen && !prev_lightpen_) {
            registers_[R16_LIGHTPEN_HI] = static_cast<uint8_t>(char_addr_ >> 8) & 0x3F;
            registers_[R17_LIGHTPEN_LO] = static_cast<uint8_t>(char_addr_ & 0xFF);
        }
        prev_lightpen_ = lightpen;

        // Handle horizontal sync
        if (hsync_counter_ >= 0) {
            ++hsync_counter_;
            if (hsync_counter_ >= hsync_width()) {
                hsync_counter_ = -1;
            }
        }

        // Check horizontal displayed
        if (column_ == registers_[R1_HDISPLAYED]) {
            // Latch next line address at end of displayed area
            next_line_addr_ = char_addr_;
            h_display_ = false;
        }

        // End of horizontal total never displays
        if (column_ == registers_[R0_HTOTAL]) {
            h_display_ = false;
        }

        // Start horizontal sync
        if (column_ == registers_[R2_HSYNC_POS] && hsync_counter_ < 0) {
            hsync_counter_ = 0;
        }

        // Handle vertical sync (on interlace point)
        if (row_ == registers_[R7_VSYNC_POS] && vsync_counter_ < 0 && !had_vsync_this_row_) {
            vsync_counter_ = 0;
            had_vsync_this_row_ = true;
        }

        if (vsync_counter_ >= 0) {
            // Check end of vsync at each scanline
        }

        // Build output
        Output output;
        output.address = char_addr_ & 0x3FFF;
        output.raster = raster_ & 0x1F;
        output.hsync = hsync_counter_ >= 0 ? 1 : 0;
        output.vsync = vsync_counter_ >= 0 ? 1 : 0;
        output.display = (h_display_ && v_display_) ? 1 : 0;

        // Cursor display
        output.cursor = 0;
        if (output.display) {
            uint16_t cursor_pos = cursor_position();
            if (char_addr_ == cursor_pos) {
                uint8_t cursor_start = registers_[R10_CURSOR_START] & 0x1F;
                uint8_t cursor_end = registers_[R11_CURSOR_END] & 0x1F;
                uint8_t cursor_mode = (registers_[R10_CURSOR_START] >> 5) & 0x03;

                if (raster_ >= cursor_start && raster_ <= cursor_end) {
                    switch (cursor_mode) {
                        case 0: // Steady cursor
                            output.cursor = 1;
                            break;
                        case 1: // No cursor
                            break;
                        case 2: // Blink at 1/16 field rate
                            output.cursor = (frame_count_ & 0x08) ? 1 : 0;
                            break;
                        case 3: // Blink at 1/32 field rate
                            output.cursor = (frame_count_ & 0x10) ? 1 : 0;
                            break;
                    }
                }
            }
        }

        // Advance character address
        ++char_addr_;

        // Handle end of horizontal line
        if (column_ == registers_[R0_HTOTAL]) {
            end_of_scanline();
            column_ = 0;
            h_display_ = true;
        } else {
            ++column_;
        }

        // Handle end of vertical displayed
        if (row_ == registers_[R6_VDISPLAYED] && v_display_) {
            v_display_ = false;
            ++frame_count_;
        }

        return output;
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
    uint8_t hsync_width() const { return registers_[R3_SYNC_WIDTH] & 0x0F; }
    uint8_t vsync_width() const { return (registers_[R3_SYNC_WIDTH] >> 4) & 0x0F; }

    // Current position (for debugging)
    uint8_t column() const { return column_; }
    uint8_t row() const { return row_; }
    uint8_t raster() const { return raster_; }
    uint16_t address() const { return char_addr_; }

    // Direct register access for testing/debugging
    uint8_t reg(uint8_t index) const {
        return (index < 18) ? registers_[index] : 0;
    }

    void reset() {
        address_register_ = 0;
        registers_.fill(0);
        column_ = 0;
        row_ = 0;
        raster_ = 0;
        char_addr_ = 0;
        line_addr_ = 0;
        next_line_addr_ = 0;
        hsync_counter_ = -1;
        vsync_counter_ = -1;
        vadj_counter_ = 0;
        h_display_ = true;
        v_display_ = true;
        in_vadj_ = false;
        had_vsync_this_row_ = false;
        frame_count_ = 0;
        prev_lightpen_ = false;
    }

private:
    void end_of_scanline() {
        // Handle vsync counter
        if (vsync_counter_ >= 0) {
            ++vsync_counter_;
            uint8_t vw = vsync_width();
            if (vw == 0) vw = 16;  // 0 means 16
            if (vsync_counter_ >= vw) {
                vsync_counter_ = -1;
            }
        }

        // Check for end of character row
        bool at_max_raster = (raster_ == (registers_[R9_MAX_SCANLINE] & 0x1F));

        if (at_max_raster) {
            // Latch line address for next row
            line_addr_ = next_line_addr_;
        }

        // Increment raster
        ++raster_;
        raster_ &= 0x1F;

        if (at_max_raster && !in_vadj_) {
            end_of_row();
        }

        // Check for vertical adjust period
        if (row_ == registers_[R4_VTOTAL] + 1 && !in_vadj_) {
            if (registers_[R5_VTOTAL_ADJ] > 0) {
                in_vadj_ = true;
                vadj_counter_ = 0;
            } else {
                end_of_frame();
            }
        }

        if (in_vadj_) {
            ++vadj_counter_;
            if (vadj_counter_ >= registers_[R5_VTOTAL_ADJ]) {
                in_vadj_ = false;
                end_of_frame();
            }
        }

        // Reset character address to start of line
        char_addr_ = line_addr_;
    }

    void end_of_row() {
        ++row_;
        raster_ = 0;
        had_vsync_this_row_ = false;
    }

    void end_of_frame() {
        row_ = 0;
        raster_ = 0;

        // Reload start address
        line_addr_ = screen_start();
        next_line_addr_ = line_addr_;
        char_addr_ = line_addr_;

        v_display_ = true;
        in_vadj_ = false;
        had_vsync_this_row_ = false;
    }

    // Register masks (write masks for each register)
    static constexpr std::array<uint8_t, 18> register_masks_ = {
        0xFF, // R0  - Horizontal total
        0xFF, // R1  - Horizontal displayed
        0xFF, // R2  - Horizontal sync position
        0xFF, // R3  - Sync widths
        0x7F, // R4  - Vertical total (7 bits)
        0x1F, // R5  - Vertical total adjust (5 bits)
        0x7F, // R6  - Vertical displayed (7 bits)
        0x7F, // R7  - Vertical sync position (7 bits)
        0xF3, // R8  - Interlace mode (bits 0,1,4,5,6,7)
        0x1F, // R9  - Max scanline (5 bits)
        0x7F, // R10 - Cursor start (7 bits)
        0x1F, // R11 - Cursor end (5 bits)
        0x3F, // R12 - Start address high (6 bits)
        0xFF, // R13 - Start address low
        0x3F, // R14 - Cursor high (6 bits)
        0xFF, // R15 - Cursor low
        0x3F, // R16 - Light pen high (read-only, 6 bits)
        0xFF, // R17 - Light pen low (read-only)
    };

    // Registers
    uint8_t address_register_ = 0;
    std::array<uint8_t, 18> registers_{};

    // Timing state
    uint8_t column_ = 0;           // Horizontal character counter (0 to R0)
    uint8_t row_ = 0;              // Vertical character counter (0 to R4)
    uint8_t raster_ = 0;           // Scanline within character row (0 to R9)
    uint16_t char_addr_ = 0;       // Current character address (MA0-MA13)
    uint16_t line_addr_ = 0;       // Address at start of current scanline
    uint16_t next_line_addr_ = 0;  // Address for start of next character row

    // Sync state
    int8_t hsync_counter_ = -1;    // -1 = not in hsync, 0+ = counting
    int8_t vsync_counter_ = -1;    // -1 = not in vsync, 0+ = counting
    uint8_t vadj_counter_ = 0;     // Vertical adjust scanline counter

    // Display enable
    bool h_display_ = true;
    bool v_display_ = true;
    bool in_vadj_ = false;
    bool had_vsync_this_row_ = false;

    // Frame counting (for cursor blink)
    uint8_t frame_count_ = 0;

    // Light pen
    bool prev_lightpen_ = false;
};

} // namespace beebium
