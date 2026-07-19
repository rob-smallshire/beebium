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

// Bitmap: the representation everything else compares.

#include <catch2/catch_test_macros.hpp>

#include <unordered_set>

#include "screentext/Bitmap.hpp"

using namespace screentext;

TEST_CASE("A bitmap from rows is eight pixels wide, one byte per row")
{
    const Bitmap bitmap = Bitmap::from_rows({0x81, 0x00, 0xFF});

    CHECK(bitmap.width() == 8);
    CHECK(bitmap.height() == 3);
    CHECK(bitmap.bytes_per_row() == 1);
    CHECK(bitmap.pixel(0, 0));
    CHECK_FALSE(bitmap.pixel(1, 0));
    CHECK(bitmap.pixel(7, 0));
    CHECK_FALSE(bitmap.pixel(0, 1));
    CHECK(bitmap.pixel(4, 2));
}

TEST_CASE("The most significant bit is leftmost, as a font ROM dump has it")
{
    const Bitmap bitmap = Bitmap::from_rows({0x80});
    CHECK(bitmap.pixel(0, 0));
    for (std::size_t x = 1; x < 8; ++x) {
        CHECK_FALSE(bitmap.pixel(x, 0));
    }
}

TEST_CASE("Pixels outside the bitmap read as clear and cannot be written")
{
    Bitmap bitmap(8, 8);
    CHECK_FALSE(bitmap.pixel(8, 0));
    CHECK_FALSE(bitmap.pixel(0, 8));
    CHECK_THROWS_AS(bitmap.set_pixel(8, 0, true), std::out_of_range);
}

TEST_CASE("Inverting complements every pixel within the width")
{
    const Bitmap bitmap = Bitmap::from_rows({0x0F, 0xFF});
    const Bitmap inverted = bitmap.inverted();

    CHECK(inverted.bytes() == std::vector<std::uint8_t>{0xF0, 0x00});
    CHECK(inverted.inverted() == bitmap);
}

TEST_CASE("Padding bits stay clear, so equal pixels mean equal bitmaps")
{
    // A five-pixel-wide bitmap has three padding bits per row. Two bitmaps
    // whose visible pixels agree must compare equal whatever was written into
    // the padding, or matching would depend on invisible state.
    Bitmap first(5, 2);
    first.set_pixel(0, 0, true);
    first.set_pixel(4, 1, true);

    Bitmap second(5, 2);
    second.set_pixel(0, 0, true);
    second.set_pixel(4, 1, true);

    CHECK(first == second);

    const Bitmap inverted = first.inverted();
    CHECK(inverted.bytes()[0] == 0x78); // 01111000: five pixels, three padding
    CHECK(inverted.inverted() == first);
}

TEST_CASE("A blank bitmap is recognised as blank")
{
    CHECK(Bitmap(8, 8).is_blank());
    CHECK(Bitmap::from_rows({0x00, 0x00}).is_blank());
    CHECK_FALSE(Bitmap::from_rows({0x00, 0x01}).is_blank());
}

TEST_CASE("Bitmaps of different sizes are never equal")
{
    CHECK(Bitmap(8, 8) != Bitmap(8, 16));
    CHECK(Bitmap(8, 8) != Bitmap(16, 8));
    CHECK(Bitmap() == Bitmap());
}

TEST_CASE("Bitmaps can be used as hash keys")
{
    std::unordered_set<Bitmap> seen;
    seen.insert(Bitmap::from_rows({0x01}));
    seen.insert(Bitmap::from_rows({0x01}));
    seen.insert(Bitmap::from_rows({0x02}));

    CHECK(seen.size() == 2);
    CHECK(seen.count(Bitmap::from_rows({0x01})) == 1);
    CHECK(seen.count(Bitmap::from_rows({0x03})) == 0);
}

TEST_CASE("An empty bitmap has no pixels and survives being inverted")
{
    const Bitmap bitmap;
    CHECK(bitmap.empty());
    CHECK(bitmap.is_blank());
    CHECK(bitmap.inverted().empty());
}
