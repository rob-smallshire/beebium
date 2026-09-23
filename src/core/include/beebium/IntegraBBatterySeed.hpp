// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include "devices/Mc146818Rtc.hpp"
#include "devices/Ram.hpp"

#include <cstdint>

// The battery-backed state of an Integra-B that has been set up.
//
// A new board (or one whose battery has gone flat) boots to the MOS
// "Language?" prompt until IBOS's Full System Reset is run (hold CTRL and @
// and press BREAK, then answer Y) and the clock is set. Each launch of the
// emulated board instead starts as a board that has been through that set-up,
// as a real board would be on every power-on thanks to its battery.
//
// The values were captured from the emulator itself running IBOS 1.26: Full
// System Reset, then *CONFIGURE LANG 3 (BASIC in motherboard slot 3, this
// machine's default), then *TIME / *DATE to start the clock. They match the
// defaults other emulators hard-code (e.g. OSMODE 4 at &83B2, century &14 at
// &83B5, RAM in banks 4-7 at &83FF). Battery-backed contents are not persisted
// between launches.
namespace beebium::integra_b_battery_seed {

// Register A: 32.768 kHz time base, no periodic interrupt.
inline constexpr uint8_t RTC_REGISTER_A = 0x20;
// Register B: clock running (SET clear), binary data, 24-hour, square wave
// enabled - as IBOS leaves it after setting the time.
inline constexpr uint8_t RTC_REGISTER_B = 0x0E;

// CMOS RAM from register &0E (IBOS configuration: LANG, FILE, MODE, ...);
// registers beyond these are zero.
inline constexpr uint8_t CMOS_RAM[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 0xFF, 0xFF, 0x78, 0x00,
    0x17, 0x20, 0x19, 0x05, 0x0A, 0x2D, 0xA1, 0xFF,
};

// IBOS workspace in private RAM (&8000-&AFFF): the non-zero bytes, as
// (address, value) pairs; all other private RAM is zero.
struct PrivateRamByte {
    uint16_t address;
    uint8_t value;
};

inline constexpr PrivateRamByte PRIVATE_RAM[] = {
    {0x8201, 0x90}, {0x8204, 0x90}, {0x8207, 0x20}, {0x820A, 0x20},
    {0x820C, 0x90}, {0x820E, 0xB0}, {0x8252, 0xFF}, {0x8300, 0x0F},
    {0x8301, 0xFF}, {0x8302, 0xFF}, {0x8303, 0xFF}, {0x8304, 0xFF},
    {0x8305, 0xFF}, {0x8306, 0xFF}, {0x8307, 0xFF}, {0x8308, 0x04},
    {0x8309, 0x05}, {0x830A, 0x06}, {0x830B, 0x07}, {0x830C, 0xFF},
    {0x830D, 0xFF}, {0x830E, 0xFF}, {0x830F, 0xFF}, {0x8318, 0x4F},
    {0x8319, 0xFF}, {0x831A, 0xFF}, {0x831B, 0xFF}, {0x832D, 0x82},
    {0x832F, 0x60}, {0x833B, 0xC2}, {0x833C, 0x04}, {0x833F, 0x07},
    {0x8343, 0x01}, {0x8345, 0x90}, {0x83B2, 0x04}, {0x83B5, 0x14},
    {0x83B8, 0xFF}, {0x83B9, 0xFF}, {0x83BA, 0x90}, {0x83FF, 0x0F},
};

inline void apply(Ram<32768>& shadow_and_private_ram, Mc146818Rtc& rtc) {
    Mc146818Rtc::BatteryImage image{};
    image[Mc146818Rtc::REG_A] = RTC_REGISTER_A;
    image[Mc146818Rtc::REG_B] = RTC_REGISTER_B;
    for (size_t i = 0; i < sizeof(CMOS_RAM); ++i) {
        image[Mc146818Rtc::FIRST_RAM_REGISTER + i] = CMOS_RAM[i];
    }
    rtc.load_battery_image(image);

    // Private RAM at CPU address &8000 is chip address &0000.
    for (const auto& byte : PRIVATE_RAM) {
        shadow_and_private_ram.write(static_cast<uint16_t>(byte.address - 0x8000), byte.value);
    }
}

}  // namespace beebium::integra_b_battery_seed
