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

// The Music 5000 DSP against expectations worked by hand from
// docs/music5000/hardware-model.md (section numbers in the test names).

#include "Music5000Synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

using namespace beebium;

namespace {

// JIM page holding a RAM address, for the Music 5000's page nibble 3.
uint8_t page_for(uint16_t ram_address) {
    return static_cast<uint8_t>(0x30 | ((ram_address >> 8) << 1));
}

void poke(Music5000Synth& synth, uint16_t ram_address, uint8_t value) {
    synth.write(page_for(ram_address), static_cast<uint8_t>(ram_address & 0xFF), value);
}

}  // namespace

TEST_CASE("Music5000Synth host writes land in the RAM block page bits 3-1 select (1.2)",
          "[music5000][synth][registers]") {
    Music5000Synth synth;

    synth.write(0x30, 0x10, 0xA1);
    synth.write(0x32, 0x00, 0xA2);
    synth.write(0x3C, 0x7F, 0xA3);
    synth.write(0x3E, 0x05, 0xA4);

    auto ram = synth.ram();
    REQUIRE(ram[0x010] == 0xA1);
    REQUIRE(ram[0x100] == 0xA2);
    REQUIRE(ram[0x67F] == 0xA3);
    REQUIRE(ram[0x705] == 0xA4);
}

TEST_CASE("Music5000Synth ignores page bit 0 (1.1)", "[music5000][synth][registers]") {
    Music5000Synth synth;

    synth.write(0x3F, 0x85, 0x5A);
    REQUIRE(synth.ram()[0x785] == 0x5A);

    synth.write(0x31, 0x00, 0x11);
    synth.write(0x30, 0x00, 0x22);
    REQUIRE(synth.ram()[0x000] == 0x22);
}

TEST_CASE("Music5000Synth leaves page nibble decoding to its bus claim",
          "[music5000][synth][registers]") {
    // The synthesiser decodes only page bits 3-1; the extension claims pages
    // 0x30-0x3F (or 0x50-0x5F), so a Music 3000 page lands in the same block.
    Music5000Synth synth;
    synth.write(0x5E, 0x01, 0x77);
    REQUIRE(synth.ram()[0x701] == 0x77);
}

TEST_CASE("Music5000Synth register columns follow the counter order (1.3)",
          "[music5000][synth][registers]") {
    constexpr std::array<std::size_t, 16> expected = {0, 8, 1, 9, 2, 10, 3, 11,
                                                      4, 12, 5, 13, 6, 14, 7, 15};
    for (std::size_t k = 0; k < 16; ++k) {
        REQUIRE(Music5000Synth::column_for_counter(k) == expected[k]);
    }

    // Rows within a bank, e.g. column 9's amplitude in the alternate bank.
    REQUIRE(Music5000Synth::register_address(Music5000Synth::kAlternateBank,
                                             Music5000Synth::kAmplitudeRow, 9) == 0x7E9);
    REQUIRE(Music5000Synth::register_address(Music5000Synth::kNormalBank,
                                             Music5000Synth::kControlRow, 15) == 0x77F);
}

TEST_CASE("Music5000Synth reset zeroes the normal bank only (1.5)", "[music5000][synth][reset]") {
    Music5000Synth synth;
    for (uint16_t a = 0x000; a < 0x800; ++a) {
        poke(synth, a, 0xEE);
    }

    synth.reset();

    auto ram = synth.ram();
    for (uint16_t a = 0x700; a < 0x780; ++a) {
        INFO("address " << a);
        REQUIRE(ram[a] == 0x00);
    }
    for (uint16_t a = 0x780; a < 0x800; ++a) {
        INFO("address " << a);
        REQUIRE(ram[a] == 0xEE);
    }
    REQUIRE(ram[0x000] == 0xEE);
    REQUIRE(ram[0x6FF] == 0xEE);
    REQUIRE_FALSE(synth.index());
}
