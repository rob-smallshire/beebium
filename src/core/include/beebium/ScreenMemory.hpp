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

#pragma once

#include <cstdint>
#include <string_view>

namespace beebium {

// The bytes the board's adder subtracts from a screen address that has run
// past &7FFF (the 6845's MA12 set), wrapping it back into the screen: the
// screen size that System VIA addressable-latch outputs B4 and B5 select,
// indexed by AddressableLatch::screen_base() (B4 + 2 * B5). This is wiring,
// not a convention: a program that programs the 6845 its own way gets the
// same wrap. MOS 1.20 writes B5=1 B4=0 for its 20K modes and B5=1 B4=1 for
// its 10K ones, and those scroll correctly with this table, as in B2 and
// B-Em; the Advanced User Guide's latch table has those two rows swapped.
inline constexpr uint16_t screen_wrap_bytes(uint8_t screen_base_bits) {
    constexpr uint16_t BYTES[] = {0x4000, 0x2000, 0x5000, 0x2800};  // 16K 8K 20K 10K
    return BYTES[screen_base_bits & 0x03];
}

// The CPU address the video fetches for a 6845 character address and raster
// line. With MA13 set it is teletext: 1K at &7C00, addressed by MA0-MA9, the
// SAA5050 taking the raster. Otherwise it is a bitmap, eight bytes per
// character cell (RA0-RA2), with a character address past &7FFF (MA12)
// wrapped by the latch's screen size.
inline uint16_t screen_address(uint16_t crtc_addr, uint8_t raster, uint8_t screen_base_bits) {
    if (crtc_addr & 0x2000) {
        return 0x7C00 | (crtc_addr & 0x03FF);
    }
    uint16_t addr = crtc_addr;
    if (addr & 0x1000) {
        addr -= screen_wrap_bytes(screen_base_bits) >> 3;
        addr &= ~0x1000u;
    }
    return ((addr << 3) | (raster & 0x07)) & 0xFFFF;
}

// Where the screen the video shows lies in memory, from how the 6845, the
// latch and the paging are programmed now, not from a screen mode.
struct ScreenMemory {
    // The memory region the video fetches from (a name get_memory_regions()
    // lists): main_ram, or shadow_ram on a B+ showing its shadow RAM.
    std::string_view region;
    // The CPU address of the first displayed byte (the 6845 start address).
    uint16_t start_address = 0;
    // The bytes one frame displays: characters per row (R1) times rows (R6),
    // times eight in a bitmap.
    uint32_t length = 0;
    // Where a read continues once it passes &7FFF: &7C00 in teletext, else
    // &8000 less the latch's screen size. A screen that ends below &8000
    // never reaches it.
    uint16_t wrap_address = 0;
    bool teletext = false;
};

// The ScreenMemory a hardware policy is showing now. `Hardware` provides crtc,
// addressable_latch and, when its video can show memory other than main RAM,
// video_region().
template<typename Hardware>
ScreenMemory screen_memory(const Hardware& hardware) {
    const auto& crtc = hardware.crtc;
    const uint16_t start = crtc.screen_start();
    const uint8_t screen_base = hardware.addressable_latch.screen_base();
    const uint32_t cells = static_cast<uint32_t>(crtc.reg(1)) * crtc.reg(6);
    ScreenMemory screen;
    if constexpr (requires { hardware.video_region(); }) {
        screen.region = hardware.video_region();
    } else {
        screen.region = "main_ram";
    }
    screen.start_address = screen_address(start, 0, screen_base);
    screen.teletext = (start & 0x2000) != 0;
    screen.length = screen.teletext ? cells : cells * 8;
    screen.wrap_address = screen.teletext
        ? uint16_t{0x7C00}
        : static_cast<uint16_t>(0x8000 - screen_wrap_bytes(screen_base));
    return screen;
}

}  // namespace beebium
