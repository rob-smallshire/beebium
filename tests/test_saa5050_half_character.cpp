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

// The SAA5050's half-character drawing, against its reference: the full
// decode-and-blend path, kept as the oracle. The drawn pixels depend only on
// the half-character's foreground, background, six font bits and cursor, and
// the palette, so comparing every one of those inputs proves every frame the
// chip draws is unchanged.

#include <beebium/PixelBatch.hpp>
#include <beebium/Saa5050.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

using namespace beebium;

namespace {

std::string describe(const Saa5050::Output& output) {
    return "fg " + std::to_string(output.fg) + ", bg " + std::to_string(output.bg) +
           ", data " + std::to_string(output.data) + ", cursor " +
           std::to_string(output.cursor);
}

// The number of inputs whose drawn pixels differ from the reference's.
int count_differences(const VideoDataPixel* palette) {
    int differences = 0;
    for (int cursor = 0; cursor < 2; ++cursor) {
        for (uint8_t fg = 0; fg < 8; ++fg) {
            for (uint8_t bg = 0; bg < 8; ++bg) {
                for (uint8_t data = 0; data < 64; ++data) {
                    const Saa5050::Output output{fg, bg, data, cursor != 0};
                    PixelBatch drawn{};
                    PixelBatch reference{};
                    Saa5050::draw_half_character(output, drawn, palette);
                    Saa5050::draw_half_character_reference(output, reference, palette);
                    for (int i = 0; i < 8; ++i) {
                        if (drawn.pixels.pixels[i].value != reference.pixels.pixels[i].value) {
                            UNSCOPED_INFO(describe(output) + ", pixel " + std::to_string(i));
                            ++differences;
                            break;
                        }
                    }
                }
            }
        }
    }
    return differences;
}

}  // namespace

TEST_CASE("SAA5050 draws every half-character exactly as the reference does",
          "[saa5050][half_character]") {
    CHECK(count_differences(bbc_colors::PALETTE) == 0);
}

TEST_CASE("SAA5050 draws every half-character as the reference does in any palette",
          "[saa5050][half_character]") {
    // Channel values the BBC palette never uses, where blending a colour with
    // itself need not give the colour back.
    const VideoDataPixel palette[8] = {
        {1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12},
        {13, 14, 1}, {2, 4, 8}, {3, 6, 9}, {5, 10, 7},
    };
    CHECK(count_differences(palette) == 0);
}

TEST_CASE("SAA5050 reference draws a cell's colours and the cursor's inversion",
          "[saa5050][half_character]") {
    // A guard that the comparison above compares something: a white-on-black
    // half-character with all six bits lit is white, and the cursor inverts it.
    PixelBatch batch{};
    Saa5050::draw_half_character_reference({7, 0, 0x3F, false}, batch, bbc_colors::PALETTE);
    CHECK(batch.pixels.pixels[0].value == bbc_colors::WHITE.value);
    Saa5050::draw_half_character_reference({7, 0, 0x3F, true}, batch, bbc_colors::PALETTE);
    CHECK(batch.pixels.pixels[0].value == (bbc_colors::WHITE.value ^ 0x0FFF));
}
