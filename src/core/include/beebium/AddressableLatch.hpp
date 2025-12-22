#pragma once

#include <cstdint>

namespace beebium {

// IC32 Addressable Latch (74LS259)
//
// This 8-bit latch is controlled by the System VIA Port B:
// - Bits 0-2: Address (selects which latch bit to write)
// - Bit 3: Data value to write
//
// The latch controls various system functions:
// - Sound chip write enable
// - Speech chip (BBC B only)
// - Keyboard scan enable
// - Screen base address
// - LED indicators
//
struct AddressableLatch {
    uint8_t value = 0;

    // Bit definitions
    static constexpr uint8_t SOUND_WRITE    = 0x01;  // Bit 0: Sound chip write (active low)
    static constexpr uint8_t SPEECH_READ    = 0x02;  // Bit 1: Speech chip read
    static constexpr uint8_t SPEECH_WRITE   = 0x04;  // Bit 2: Speech chip write
    static constexpr uint8_t KB_WRITE       = 0x08;  // Bit 3: Keyboard write enable
    static constexpr uint8_t SCREEN_BASE_LO = 0x10;  // Bit 4: Screen base A12
    static constexpr uint8_t SCREEN_BASE_HI = 0x20;  // Bit 5: Screen base A13
    static constexpr uint8_t CAPS_LOCK_LED  = 0x40;  // Bit 6: Caps lock LED
    static constexpr uint8_t SHIFT_LOCK_LED = 0x80;  // Bit 7: Shift lock LED

    // Write a single bit to the latch
    // address: bits 0-2 select which latch bit (0-7)
    // data: value to write (true = set, false = clear)
    void write(uint8_t address, bool data) {
        uint8_t mask = 1 << (address & 0x07);
        if (data) {
            value |= mask;
        } else {
            value &= ~mask;
        }
    }

    // Query latch state
    bool sound_write_enabled() const { return (value & SOUND_WRITE) == 0; }
    bool keyboard_enabled() const { return (value & KB_WRITE) == 0; }
    bool caps_lock_led() const { return (value & CAPS_LOCK_LED) != 0; }
    bool shift_lock_led() const { return (value & SHIFT_LOCK_LED) != 0; }

    // Screen base address bits (used for wrap-around in screen memory)
    uint8_t screen_base() const { return (value >> 4) & 0x03; }

    void reset() { value = 0; }
};

} // namespace beebium
