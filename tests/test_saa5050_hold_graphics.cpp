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

// test_saa5050_hold_graphics.cpp
//
// Held graphics must reproduce the character being held, exactly.
//
// A teletext graphics character is expanded to a 12-bit pixel row and emitted
// as two 6-bit halves. The hold latch stores that row so a following control
// code can display it again instead of a space. If the latch is narrower than
// the row, the right-hand half of every held cell loses pixels -- visible as a
// blanked rightmost column wherever hold graphics is used. GitHub issue #57.
//
// These tests state the requirement rather than the bit layout: whatever the
// source character renders as, the held cell must render the same.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Saa5050.hpp>
#include <beebium/PixelBatch.hpp>

#include <array>

using namespace beebium;

namespace {

// Distinguishable colours so a foreground pixel cannot be confused with a
// background one. Index 7 is the default foreground, 0 the default background.
std::array<VideoDataPixel, 8> make_palette() {
    std::array<VideoDataPixel, 8> palette{};
    for (size_t i = 0; i < palette.size(); ++i) {
        VideoDataPixel pixel;
        pixel.bits.r = static_cast<uint16_t>(i & 1 ? 15 : 0);
        pixel.bits.g = static_cast<uint16_t>(i & 2 ? 15 : 0);
        pixel.bits.b = static_cast<uint16_t>(i & 4 ? 15 : 0);
        pixel.bits.x = 0;
        palette[i] = pixel;
    }
    return palette;
}

// The 16 pixels a single character cell produces: two emitted halves of 8.
struct Cell {
    std::array<uint16_t, 16> pixels{};

    bool operator==(const Cell& other) const { return pixels == other.pixels; }

    // Pixels in the right-hand half, which is what the truncation loses.
    std::array<uint16_t, 8> right_half() const {
        std::array<uint16_t, 8> half{};
        for (size_t i = 0; i < 8; ++i) half[i] = pixels[8 + i];
        return half;
    }

    // Is any pixel in this half lit?
    //
    // Compares colour only. The top nibble of a pixel is a metadata/type
    // field, and the chip sets it on the first pixel of each emitted batch, so
    // a blank cell is not all-zero words.
    static constexpr uint16_t COLOUR_MASK = 0x0FFF;

    bool any_set(const std::array<uint16_t, 8>& half, uint16_t background) const {
        const uint16_t bg = background & COLOUR_MASK;
        for (uint16_t pixel : half) {
            if ((pixel & COLOUR_MASK) != bg) return true;
        }
        return false;
    }
};

// Feed one character through the chip and collect the cell it renders.
Cell render(Saa5050& chip, uint8_t value, const std::array<VideoDataPixel, 8>& palette) {
    chip.byte(value, 1);

    Cell cell;
    for (int half = 0; half < 2; ++half) {
        PixelBatch batch;
        chip.emit_pixels(batch, palette.data());
        for (int i = 0; i < 8; ++i) {
            cell.pixels[static_cast<size_t>(half * 8 + i)] = batch.pixels.pixels[i].value;
        }
    }
    return cell;
}

// Teletext control codes used here.
constexpr uint8_t GRAPHICS_WHITE = 0x17;  // Select contiguous graphics
constexpr uint8_t HOLD_GRAPHICS  = 0x1E;
constexpr uint8_t RELEASE_GRAPHICS = 0x1F;

// A mosaic character with pixels in every sixel position, so both halves of
// the expanded row are non-empty. Bit 5 set marks it as graphics.
constexpr uint8_t SOLID_MOSAIC = 0x7F;

} // namespace

TEST_CASE("Held graphics reproduce the character being held", "[saa5050][hold]") {
    const auto palette = make_palette();
    const uint16_t background = palette[0].value;

    Saa5050 chip;
    chip.byte(GRAPHICS_WHITE, 1);
    // Discard the control code's own output.
    for (int i = 0; i < 2; ++i) {
        PixelBatch batch;
        chip.emit_pixels(batch, palette.data());
    }

    const Cell source = render(chip, SOLID_MOSAIC, palette);

    SECTION("the source mosaic lights pixels in both halves") {
        // Guards the test itself: a character blank on the right would make
        // the comparison below vacuous.
        REQUIRE(source.any_set(source.right_half(), background));
    }

    SECTION("a control code under hold renders the held character exactly") {
        chip.byte(HOLD_GRAPHICS, 1);
        Cell held;
        for (int half = 0; half < 2; ++half) {
            PixelBatch batch;
            chip.emit_pixels(batch, palette.data());
            for (int i = 0; i < 8; ++i) {
                held.pixels[static_cast<size_t>(half * 8 + i)] =
                    batch.pixels.pixels[i].value;
            }
        }

        // The whole cell, not merely its left half. Truncating the hold latch
        // leaves the left half correct and blanks the right.
        CHECK(held.right_half() == source.right_half());
        CHECK(held == source);
    }

    SECTION("hold persists across several control codes") {
        chip.byte(HOLD_GRAPHICS, 1);
        for (int i = 0; i < 2; ++i) {
            PixelBatch batch;
            chip.emit_pixels(batch, palette.data());
        }

        // A second control code while hold is active must still show the
        // held character, right half included.
        const Cell held = render(chip, 0x08 /* Flash */, palette);
        CHECK(held.right_half() == source.right_half());
    }
}

TEST_CASE("Releasing graphics stops holding", "[saa5050][hold]") {
    const auto palette = make_palette();
    const uint16_t background = palette[0].value;

    Saa5050 chip;
    chip.byte(GRAPHICS_WHITE, 1);
    for (int i = 0; i < 2; ++i) {
        PixelBatch batch;
        chip.emit_pixels(batch, palette.data());
    }
    const Cell source = render(chip, SOLID_MOSAIC, palette);
    REQUIRE(source.any_set(source.right_half(), background));

    chip.byte(HOLD_GRAPHICS, 1);
    for (int i = 0; i < 2; ++i) {
        PixelBatch batch;
        chip.emit_pixels(batch, palette.data());
    }

    // Release Graphics is a "set-after" code: the cell carrying it still
    // shows the held character, and the release takes effect from the next
    // cell onwards. Hold Graphics, by contrast, is "set-at".
    const Cell releasing = render(chip, RELEASE_GRAPHICS, palette);

    SECTION("the releasing cell still shows the held character") {
        CHECK(releasing.right_half() == source.right_half());
    }

    SECTION("the cell after the release is blank") {
        const Cell after = render(chip, 0x08 /* Flash */, palette);
        CHECK_FALSE(after.any_set(after.right_half(), background));
    }
}
