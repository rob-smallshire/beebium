// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// The CMOS a Master 128's clock holds when Beebium first fits its battery: the
// configuration Acorn's Welcome Guide tells a new owner to enter
// (docs/discussion/master-128-research.md, section 4 item 3), with FLOPPY
// for the guide's HARD, FILE 13 (ADFS) for its FILE 15, every ROM inserted
// and station 0. Bank 15 is Terminal, not a filing system, and MOS 3.20 does
// not fall back from it to another filing system ROM: with FILE 15 it starts
// on the tape filing system.
//
// CMOS byte n (OSBYTE &A1/&A2 address) is the clock's register 14 + n. The
// byte map is the Reference Manual's (research section 1.4, beebwiki's CMOS
// allocation), with the encodings as MOS 3.20's *STATUS reports them: BAUD n
// is held as n - 1 in byte 15 bits 2-4, and byte 16 bit 2 set is EXTUBE, so
// INTUBE is the bit clear.

#pragma once

#include "devices/Mc146818Rtc.hpp"

#include <array>
#include <cstdint>

namespace beebium::master_128_cmos_seed {

// The 50 CMOS RAM bytes, by OSBYTE &A1 address.
using CmosBytes = std::array<uint8_t, 50>;

inline constexpr uint8_t CMOS_STATION = 0;
inline constexpr uint8_t CMOS_FILE_LANG = 5;
inline constexpr uint8_t CMOS_ROMS_0_7 = 6;
inline constexpr uint8_t CMOS_ROMS_8_15 = 7;
inline constexpr uint8_t CMOS_MODE_TV = 10;
inline constexpr uint8_t CMOS_DRIVE_CAPS_ADFS = 11;
inline constexpr uint8_t CMOS_DELAY = 12;
inline constexpr uint8_t CMOS_REPEAT = 13;
inline constexpr uint8_t CMOS_IGNORE = 14;
inline constexpr uint8_t CMOS_TUBE_BAUD_PRINT = 15;
inline constexpr uint8_t CMOS_LOUD_TUBE_SCROLL_BOOT_DATA = 16;

// The clock's control registers as MOS 3.20 writes them on every reset:
// A = &20 (32.768 kHz time base, no periodic interrupt), B = &02 (BCD,
// 24-hour, no interrupts).
inline constexpr uint8_t RTC_REGISTER_A = 0x20;
inline constexpr uint8_t RTC_REGISTER_B = 0x02;

// The Welcome Guide's configuration, with `station` in byte 0.
constexpr CmosBytes welcome_guide(uint8_t station = 0) {
    CmosBytes cmos{};
    cmos[CMOS_STATION] = station;
    cmos[CMOS_FILE_LANG] = (12 << 4) | 13;           // LANG 12, FILE 13
    cmos[CMOS_ROMS_0_7] = 0xFF;                       // every ROM inserted
    cmos[CMOS_ROMS_8_15] = 0xFF;
    cmos[CMOS_MODE_TV] = 0x10 | 7;                    // TV 0,1; MODE 7
    cmos[CMOS_DRIVE_CAPS_ADFS] = 0x80 | 0x40 | 0x20 | 3;  // FLOPPY, NODIR, CAPS, FDRIVE 3
    cmos[CMOS_DELAY] = 50;                            // DELAY 50
    cmos[CMOS_REPEAT] = 10;                           // REPEAT 10
    cmos[CMOS_IGNORE] = 10;                           // IGNORE 10
    cmos[CMOS_TUBE_BAUD_PRINT] = (1 << 5) | ((4 - 1) << 2) | 0x01;  // PRINT 1, BAUD 4, TUBE
    cmos[CMOS_LOUD_TUBE_SCROLL_BOOT_DATA] =
        (4 << 5) | 0x02;                              // DATA 4, LOUD; INTUBE, SCROLL, NOBOOT
    return cmos;
}

// Give `rtc` the seed: the control registers and the CMOS RAM. The time is
// the clock's own (host time unless an offset is set).
inline void apply_to_clock(Mc146818Rtc& rtc, uint8_t station = 0) {
    Mc146818Rtc::BatteryImage image{};
    image[Mc146818Rtc::REG_A] = RTC_REGISTER_A;
    image[Mc146818Rtc::REG_B] = RTC_REGISTER_B;
    const CmosBytes cmos = welcome_guide(station);
    for (std::size_t i = 0; i < cmos.size(); ++i) {
        image[Mc146818Rtc::FIRST_RAM_REGISTER + i] = cmos[i];
    }
    rtc.load_battery_image(image);
}

}  // namespace beebium::master_128_cmos_seed
