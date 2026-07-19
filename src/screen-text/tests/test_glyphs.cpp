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

// The built-in glyph sets: baked in at build time, never read from a ROM.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

#include "screentext/Glyph.hpp"

using namespace screentext;

namespace {

const Glyph* glyph_with(const GlyphSet& set, char32_t codepoint)
{
    for (const Glyph& glyph : set.glyphs) {
        if (glyph.codepoint == codepoint) {
            return &glyph;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("The Acorn MOS 1.20 set is built in and named")
{
    const std::vector<std::string> names = builtin_glyph_set_names();
    CHECK(std::find(names.begin(), names.end(), "acorn-mos-1.20") != names.end());

    const GlyphSet& set = builtin_glyph_set("acorn-mos-1.20");
    CHECK(set.name == "acorn-mos-1.20");
}

TEST_CASE("The Acorn set covers characters 32 to 126")
{
    const GlyphSet& set = builtin_glyph_set("acorn-mos-1.20");
    CHECK(set.glyphs.size() == 95);

    for (const Glyph& glyph : set.glyphs) {
        CHECK(glyph.bitmap.width() == 8);
        CHECK(glyph.bitmap.height() == 8);
    }
}

TEST_CASE("Glyph bitmaps match the ROM font byte for byte")
{
    // Spot checks against the MOS 1.20 font, which was verified against a
    // running machine. If these change, the generated set is wrong.
    const GlyphSet& set = builtin_glyph_set("acorn-mos-1.20");

    const Glyph* space = glyph_with(set, U' ');
    REQUIRE(space != nullptr);
    CHECK(space->bitmap.is_blank());

    const Glyph* exclamation = glyph_with(set, U'!');
    REQUIRE(exclamation != nullptr);
    CHECK(exclamation->bitmap
          == Bitmap::from_rows({0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00}));

    const Glyph* letter_a = glyph_with(set, U'A');
    REQUIRE(letter_a != nullptr);
    CHECK(letter_a->bitmap
          == Bitmap::from_rows({0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00}));
}

TEST_CASE("Character 96 is a pound sign, not a grave accent")
{
    // The Acorn set is ASCII apart from this one character, and getting it
    // wrong is invisible until a listing containing money comes out wrong.
    const GlyphSet& set = builtin_glyph_set("acorn-mos-1.20");

    CHECK(glyph_with(set, 0x00A3U) != nullptr); // POUND SIGN
    CHECK(glyph_with(set, U'`') == nullptr);    // GRAVE ACCENT
}

TEST_CASE("Character 127 is excluded, so a solid cell reads as inverse space")
{
    // The ROM holds eight bytes of solid block at 127, but VDU 127 deletes a
    // character rather than printing one, so those bytes are never rendered
    // and do not describe any text.
    //
    // Including them would also break the common case: a solid block is
    // exactly the complement of a space, and an upright match beats an
    // inverted one, so every inverse space -- most of an inverse-video
    // line -- would come back as an invisible control code.
    const GlyphSet& set = builtin_glyph_set("acorn-mos-1.20");

    CHECK(glyph_with(set, 0x007FU) == nullptr);

    const Glyph* space = glyph_with(set, U' ');
    REQUIRE(space != nullptr);
    CHECK(space->bitmap.inverted()
          == Bitmap::from_rows({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}

TEST_CASE("No two glyphs in the Acorn set share a bitmap")
{
    // A collision would make one of the two unreachable, and which one would
    // depend on insertion order.
    const GlyphSet& set = builtin_glyph_set("acorn-mos-1.20");

    std::set<std::vector<std::uint8_t>> seen;
    for (const Glyph& glyph : set.glyphs) {
        CHECK(seen.insert(glyph.bitmap.bytes()).second);
    }
}

TEST_CASE("An unknown glyph set name is reported, not invented")
{
    CHECK(find_builtin_glyph_set("no-such-set") == nullptr);
    CHECK_THROWS_AS(builtin_glyph_set("no-such-set"), std::out_of_range);
}

TEST_CASE("The built-in set is the same object every time")
{
    const GlyphSet& first = builtin_glyph_set("acorn-mos-1.20");
    const GlyphSet& second = builtin_glyph_set("acorn-mos-1.20");
    CHECK(&first == &second);
}
