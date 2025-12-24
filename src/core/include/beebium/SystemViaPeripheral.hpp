#pragma once

#include "AddressableLatch.hpp"
#include "KeyboardMatrix.hpp"
#include "Via6522.hpp"
#include <cstdint>

namespace beebium {

// System VIA Peripheral - connects keyboard matrix and addressable latch
//
// The System VIA in a BBC Micro connects to:
// - Port A: Keyboard column data (active low when key pressed)
// - Port B bits 0-2: Addressable latch address
// - Port B bit 3: Addressable latch data
// - Port B bits 4-5: Joystick fire buttons (active low)
// - Port B bits 6-7: Speech/Econet ready (active low)
// - CA2: Keyboard interrupt (directly connected to keyboard matrix)
// - CB1: ADC end of conversion
// - CB2: Light pen strobe
//
// Keyboard scanning modes:
// 1. Auto-scan (KB_WRITE=1): Hardware cycles through columns, CA2 interrupts on keypress
// 2. Manual scan (KB_WRITE=0): MOS writes key number to Port A to check specific key
//
class SystemViaPeripheral : public ViaPeripheral {
public:
    SystemViaPeripheral(AddressableLatch& latch, KeyboardMatrix& keyboard)
        : latch_(latch), keyboard_(keyboard) {}

    // Legacy constructor for backward compatibility (uses internal keyboard)
    explicit SystemViaPeripheral(AddressableLatch& latch)
        : latch_(latch), keyboard_(internal_keyboard_) {}

    uint8_t update_port_a(uint8_t output, uint8_t ddr) override {
        (void)ddr;

        // Port A reads keyboard matrix data
        // The MOS writes a key number to Port A (bits 0-6) where:
        //   - Bits 0-3: column (0-15, but only 0-9 used)
        //   - Bits 4-6: row (0-7, rows 8-9 use columns 10+)
        // Then reads back bit 7 to check if that key is pressed.
        //
        // IMPORTANT: The bit 7 convention is:
        //   - Bit 7 = 0: Key/link NOT pressed (open/broken)
        //   - Bit 7 = 1: Key/link IS pressed (closed/made)
        //
        // This is the software convention used by all emulators (BeebEm, B2),
        // even though the physical hardware is active-low.

        uint8_t key_number = output & 0x7F;
        port_a_output_ = key_number;  // Store for CA2 calculation in manual scan mode

        // Extract row and column from key number
        // Standard BBC keyboard scanning: column = bits 0-3, row = bits 4-6
        uint8_t column = key_number & 0x0F;
        uint8_t row = (key_number >> 4) & 0x07;

        // Check if key is pressed in our matrix
        bool pressed = keyboard_.is_key_pressed(row, column);

        // Return the key number with bit 7 set if pressed
        if (pressed) {
            return key_number | 0x80;
        } else {
            return key_number & 0x7F;
        }
    }

    uint8_t update_port_b(uint8_t output, uint8_t ddr) override {
        (void)ddr;

        // Port B outputs control the addressable latch:
        // Bits 0-2: Latch address (selects which of 8 latch bits)
        // Bit 3: Latch data value
        uint8_t latch_addr = output & 0x07;
        bool latch_data = (output & 0x08) != 0;
        latch_.write(latch_addr, latch_data);

        // Port B input bits:
        // Bit 4: Joystick 0 fire (active low) - return 1 = not pressed
        // Bit 5: Joystick 1 fire (active low) - return 1 = not pressed
        // Bit 6: Speech ready (active low) - return 1 = ready
        // Bit 7: Speech interrupt (active low) - return 1 = no interrupt
        return 0xF0;
    }

    void update_control_lines(uint8_t& ca1, uint8_t& ca2,
                              uint8_t& cb1, uint8_t& cb2) override {
        (void)cb1;
        (void)cb2;

        // CA1: VSYNC from CRTC (directly connected)
        // Pass through the VSYNC state we've stored
        ca1 = vsync_ ? 0 : 1;  // Active low

        // CA2: Keyboard matrix interrupt
        // The MOS configures CA2 for POSITIVE edge detection (PCR bits 1-3 = 0b010).
        // This means the interrupt fires on a LOW→HIGH transition.
        //
        // BBC Keyboard scanning has two modes:
        // 1. Auto-scan (KB_WRITE=1): Hardware cycles through columns automatically
        // 2. Manual scan (KB_WRITE=0): CPU writes key number to Port A to check
        //
        // In auto-scan mode (matching B2 emulator):
        // - Hardware cycles through columns 0-15
        // - CA2 = 0 when NO key pressed in current column
        // - CA2 = 1 when a key IS pressed in current column (triggers interrupt)
        //
        if (latch_.keyboard_enabled()) {
            // Manual scan mode (KB_WRITE=0) - CA2 based on current scan result
            uint8_t column = port_a_output_ & 0x0F;
            bool key_pressed = keyboard_.any_key_in_column(column, true);  // Exclude row 0
            if (!key_pressed) {
                ca2 = 0;
            }
        } else {
            // Auto-scan mode (KB_WRITE=1) - cycle through columns
            // Check if any key (rows 1-9) pressed in current auto-scan column
            bool key_pressed = keyboard_.any_key_in_column(auto_scan_column_, true);

            if (!key_pressed) {
                ca2 = 0;
            }
            // If key IS pressed, leave ca2 = 1 (VIA's default), triggering positive edge

            // Advance auto-scan column for next update
            auto_scan_column_ = (auto_scan_column_ + 1) & 0x0F;
        }

        // CB1: ADC end of conversion - not implemented
        // CB2: Light pen strobe - not implemented
    }

    // Set VSYNC state (called by video hardware)
    void set_vsync(bool active) {
        vsync_ = active;
    }

    bool vsync() const { return vsync_; }

    // Keyboard input control - delegates to KeyboardMatrix
    void key_down(uint8_t row, uint8_t column) {
        keyboard_.key_down(row, column);
    }

    void key_up(uint8_t row, uint8_t column) {
        keyboard_.key_up(row, column);
    }

    bool is_key_pressed(uint8_t row, uint8_t column) const {
        return keyboard_.is_key_pressed(row, column);
    }

    uint16_t get_row_state(uint8_t row) const {
        return keyboard_.get_row_state(row);
    }

    void clear_all_keys() {
        keyboard_.clear();
    }

    // Access to keyboard matrix (for direct manipulation if needed)
    KeyboardMatrix& keyboard() { return keyboard_; }
    const KeyboardMatrix& keyboard() const { return keyboard_; }

    // Accessor for testing/debugging
    AddressableLatch& latch() { return latch_; }
    const AddressableLatch& latch() const { return latch_; }
    uint8_t keyboard_column() const { return auto_scan_column_; }

private:
    AddressableLatch& latch_;
    KeyboardMatrix& keyboard_;
    KeyboardMatrix internal_keyboard_;  // Used when no external keyboard provided

    uint8_t port_a_output_ = 0;     // Last Port A output (key number for manual scan)
    uint8_t auto_scan_column_ = 0;  // Hardware auto-scan column counter (0-15)
    bool vsync_ = false;
};

} // namespace beebium
