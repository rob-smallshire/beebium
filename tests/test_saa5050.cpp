// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include <catch2/catch_test_macros.hpp>
#include <beebium/Saa5050.hpp>
#include <beebium/PixelBatch.hpp>

using namespace beebium;

// ============================================================================
// Phase 1: Font Data and Basic Structure
// ============================================================================

TEST_CASE("Teletext font data validity", "[saa5050][font]") {
    SECTION("space character (0x20) is blank") {
        for (int row = 0; row < 10; ++row) {
            CHECK(TELETEXT_FONT_RAW[0][row] == 0);
        }
    }

    SECTION("'A' character has recognizable pattern") {
        // 'A' is at index 33 (0x41 - 0x20)
        constexpr int A_index = 'A' - 0x20;

        // Row 0 should be blank (top of character)
        CHECK(TELETEXT_FONT_RAW[A_index][0] == 0);

        // Middle rows should have pixels
        bool has_pixels = false;
        for (int row = 1; row < 8; ++row) {
            if (TELETEXT_FONT_RAW[A_index][row] != 0) {
                has_pixels = true;
                break;
            }
        }
        CHECK(has_pixels);
    }

    SECTION("'#' (British Pound) has pixels") {
        // '#' is at index 3 (0x23 - 0x20)
        constexpr int pound_index = '#' - 0x20;

        bool has_pixels = false;
        for (int row = 0; row < 10; ++row) {
            if (TELETEXT_FONT_RAW[pound_index][row] != 0) {
                has_pixels = true;
                break;
            }
        }
        CHECK(has_pixels);
    }
}

TEST_CASE("SAA5050 construction and reset", "[saa5050]") {
    Saa5050 chip;

    SECTION("initial state after construction") {
        // Default: white foreground, black background
        CHECK(chip.foreground() == 7);
        CHECK(chip.background() == 0);
        CHECK(chip.charset() == TeletextCharset::Alpha);
        CHECK(chip.line() == 0);
    }

    SECTION("reset restores default state") {
        // Modify state
        chip.vsync();  // Increments frame counter
        chip.end_of_line();  // Advances the line count

        // Reset should restore defaults
        chip.reset();
        CHECK(chip.foreground() == 7);
        CHECK(chip.background() == 0);
        CHECK(chip.line() == 0);
    }
}

TEST_CASE("SAA5050 line management", "[saa5050]") {
    Saa5050 chip;

    SECTION("start_of_line resets per-line state") {
        chip.start_of_line();

        CHECK(chip.foreground() == 7);
        CHECK(chip.background() == 0);
        CHECK(chip.charset() == TeletextCharset::Alpha);
    }

    SECTION("the end of each displayed line advances the line count") {
        // LOSE's trailing edge, not the 6845's row address, moves the chip
        // down the character.
        CHECK(chip.line() == 0);
        chip.end_of_line();
        CHECK(chip.line() == 1);
        for (int i = 0; i < 4; ++i) {
            chip.end_of_line();
        }
        CHECK(chip.line() == 5);
    }

    SECTION("a character row is ten lines") {
        for (int i = 0; i < Saa5050::ROW_LINES - 1; ++i) {
            chip.end_of_line();
        }
        CHECK(chip.line() == 9);
        chip.end_of_line();
        CHECK(chip.line() == 0);
    }

    SECTION("CRS picks the rounded partner of the font line") {
        // Glyph line 2 x line + CRS of the twenty in the expanded font.
        chip.end_of_line();
        chip.end_of_line();
        chip.set_crs(false);
        CHECK(chip.glyph_line() == 4);
        chip.set_crs(true);
        CHECK(chip.glyph_line() == 5);
    }

    SECTION("the end of VSYNC clears the line count") {
        // DEW's trailing edge starts the field on its first character row.
        for (int i = 0; i < 3; ++i) {
            chip.end_of_line();
        }
        chip.vsync();
        CHECK(chip.line() == 3);  // the leading edge leaves the count alone
        chip.end_of_vsync();
        CHECK(chip.line() == 0);
    }
}

// ============================================================================
// Phase 2: Basic Character Rendering (tests to be added incrementally)
// ============================================================================

TEST_CASE("SAA5050 renders space character", "[saa5050][render]") {
    Saa5050 chip;
    chip.start_of_line();

    // New format: each byte() writes 2 entries (left and right halves)
    // write_index starts at 4, read_index starts at 0
    // After 4 byte() calls: write_index wraps around and fills all 8 slots
    chip.byte(0x20, 1);  // writes indices 4, 5
    chip.byte(0x20, 1);  // writes indices 6, 7
    chip.byte(0x20, 1);  // writes indices 0, 1
    chip.byte(0x20, 1);  // writes indices 2, 3

    // Now all 8 slots have space data; read any one
    PixelBatch batch;
    chip.emit_pixels(batch, bbc_colors::PALETTE);  // reads index 0

    // All pixels should be background (black) for space character
    // Compare RGB only (ignore metadata in x field)
    for (int i = 0; i < 8; ++i) {
        CHECK(batch.pixels.pixels[i].bits.r == 0);
        CHECK(batch.pixels.pixels[i].bits.g == 0);
        CHECK(batch.pixels.pixels[i].bits.b == 0);
    }
}

TEST_CASE("SAA5050 renders alpha character A", "[saa5050][render]") {
    Saa5050 chip;

    // Draw glyph line 2 -- font row 1, where 'A' has pixels (row 0 is blank
    // for most characters): one displayed line down the character, CRS low.
    chip.end_of_line();
    chip.start_of_line();

    // Each byte() writes 2 entries (left half, right half)
    // With no pipeline delay, byte('A') writes to indices 0 and 1
    chip.byte('A', 1);  // writes indices 0, 1

    // Read both halves of 'A' and check for white pixels
    // 'A' at row 2 has pixels that should appear as white (foreground color)
    bool has_white = false;

    // Left half (index 0)
    PixelBatch batch;
    chip.emit_pixels(batch, bbc_colors::PALETTE);
    for (int i = 0; i < 8; ++i) {
        if (batch.pixels.pixels[i].bits.r == 15 &&
            batch.pixels.pixels[i].bits.g == 15 &&
            batch.pixels.pixels[i].bits.b == 15) {
            has_white = true;
        }
    }

    // Right half (index 1)
    PixelBatch batch2;
    chip.emit_pixels(batch2, bbc_colors::PALETTE);
    for (int i = 0; i < 8; ++i) {
        if (batch2.pixels.pixels[i].bits.r == 15 &&
            batch2.pixels.pixels[i].bits.g == 15 &&
            batch2.pixels.pixels[i].bits.b == 15) {
            has_white = true;
        }
    }

    CHECK(has_white);
}

TEST_CASE("SAA5050 font row 1 of 'A' has pixels", "[saa5050][font]") {
    // Verify font data for 'A' at row 1 is non-zero
    constexpr int A_index = 'A' - 0x20;
    CHECK(TELETEXT_FONT_RAW[A_index][1] != 0);  // Row 1 should have pixels
    INFO("Font row 1 of 'A' = " << static_cast<int>(TELETEXT_FONT_RAW[A_index][1]));
}

TEST_CASE("SAA5050 emit_pixels basic output", "[saa5050][render]") {
    Saa5050 chip;
    chip.start_of_line();

    // The delay buffer starts with write_index=4, read_index=0
    // Each byte() writes 2 entries (left and right halves)
    // So 2 byte() calls fill 4 entries, and 4 byte() calls fill all 8

    // A fresh chip draws the first glyph line of the character.
    CHECK(chip.glyph_line() == 0);
}
