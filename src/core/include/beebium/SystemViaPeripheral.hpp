#pragma once

#include "AddressableLatch.hpp"
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
// This minimal implementation:
// - Returns "no keys pressed" for all keyboard scans
// - Handles addressable latch writes via Port B
// - Returns joystick fire buttons as unpressed
//
class SystemViaPeripheral : public ViaPeripheral {
public:
    explicit SystemViaPeripheral(AddressableLatch& latch)
        : latch_(latch) {}

    uint8_t update_port_a(uint8_t output, uint8_t ddr) override {
        (void)ddr;

        // Port A reads keyboard matrix data
        // The MOS writes a key number to Port A (bits 0-6) where:
        //   - Bits 0-3: column (0-15)
        //   - Bits 4-6: row (0-7)
        // Then reads back bit 7 to check if that key is pressed.
        //
        // IMPORTANT: The bit 7 convention is:
        //   - Bit 7 = 0: Key/link NOT pressed (open/broken)
        //   - Bit 7 = 1: Key/link IS pressed (closed/made)
        //
        // This is the software convention used by all emulators (BeebEm, B2),
        // even though the physical hardware is active-low.
        //
        // For a keyboard with no keys pressed, ALL positions return bit 7 = 0.
        // This includes SHIFT, CTRL, and all keyboard link positions.

        uint8_t key_number = output & 0x7F;

        // Store for debugging
        last_scanned_key_ = key_number;

        // No keys pressed - return the key number echoed back with bit 7 = 0
        // This indicates "not pressed" for all keyboard matrix positions.
        return output & 0x7F;
    }

    uint8_t update_port_b(uint8_t output, uint8_t ddr) override {
        (void)ddr;

        // Port B outputs control the addressable latch:
        // Bits 0-2: Latch address (selects which of 8 latch bits)
        // Bit 3: Latch data value
        uint8_t latch_addr = output & 0x07;
        bool latch_data = (output & 0x08) != 0;
        latch_.write(latch_addr, latch_data);

        // Store keyboard column for future matrix scanning
        // (The slow data bus uses Port A value when KB_WRITE is low)
        keyboard_column_ = output & 0x0F;

        // Port B input bits:
        // Bit 4: Joystick 0 fire (active low) - return 1 = not pressed
        // Bit 5: Joystick 1 fire (active low) - return 1 = not pressed
        // Bit 6: Speech ready (active low) - return 1 = ready
        // Bit 7: Speech interrupt (active low) - return 1 = no interrupt
        return 0xF0;
    }

    void update_control_lines(uint8_t& ca1, uint8_t& ca2,
                              uint8_t& cb1, uint8_t& cb2) override {
        (void)ca1;  // Don't modify CA1 - let VIA keep its default behavior
        (void)ca2;
        (void)cb1;
        (void)cb2;

        // CA1: VSYNC from CRTC (directly connected)
        // Only modify if video output is actively being used
        // For now, we let the caller (ModelBHardware::tick_video) handle this
        // by calling set_vsync() which stores state but doesn't directly affect VIA
        // until video output is integrated.
        //
        // TODO: Once video output is fully integrated, pass vsync_ to ca1 here.

        // CA2: Directly connected to keyboard matrix for interrupt generation
        // When a key in the currently selected column is pressed, CA2 goes low
        // For minimal boot: no keys pressed, so CA2 stays high (no change)

        // CB1: ADC end of conversion - not implemented
        // CB2: Light pen strobe - directly connected to CRTC
    }

    // Set VSYNC state (called by video hardware)
    void set_vsync(bool active) {
        vsync_ = active;
    }

    bool vsync() const { return vsync_; }

    // Accessor for testing/debugging
    AddressableLatch& latch() { return latch_; }
    const AddressableLatch& latch() const { return latch_; }
    uint8_t keyboard_column() const { return keyboard_column_; }
    uint8_t last_scanned_key() const { return last_scanned_key_; }

private:
    AddressableLatch& latch_;
    uint8_t keyboard_column_ = 0;
    uint8_t last_scanned_key_ = 0;
    bool vsync_ = false;

    // Future: keyboard matrix state
    // uint8_t key_matrix_[16] = {};  // 16 columns, 8 rows each
};

} // namespace beebium
