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
#include <unordered_set>

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

TEST_CASE("Character 127 is excluded, so a solid cell reads as a space")
{
    // The ROM holds eight bytes of solid block at 127, but VDU 127 deletes a
    // character rather than printing one, so those bytes are never rendered
    // and do not describe any text.
    //
    // They were once excluded for a second reason as well -- a solid block
    // being the complement of a space, every solid cell read as an invisible
    // control code -- which reading cells both ways round has since settled
    // on its own. The reason above stands regardless.
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

    std::unordered_set<Bitmap> seen;
    for (const Glyph& glyph : set.glyphs) {
        CHECK(seen.insert(glyph.bitmap).second);
    }
}

TEST_CASE("No glyph's complement is also a glyph")
{
    // What lets a cell's two colours be told apart without being declared.
    // Each cell is read both ways round, and at most one way can be a glyph,
    // so whichever matched says which colour the glyph was drawn in. Were
    // some glyph's complement also a glyph, both readings could match and
    // the cell would be genuinely ambiguous -- resolvable by area, but a
    // coin-toss on a cell divided evenly.
    const GlyphSet& set = builtin_glyph_set("acorn-mos-1.20");

    // Hashed rather than ordered: Bitmap has an equality and a hash of its
    // own, and comparing the underlying byte vectors instead trips a GCC
    // false positive about memcmp bounds it cannot infer.
    std::unordered_set<Bitmap> bitmaps;
    for (const Glyph& glyph : set.glyphs) {
        bitmaps.insert(glyph.bitmap);
    }
    for (const Glyph& glyph : set.glyphs) {
        INFO("codepoint " << static_cast<unsigned>(glyph.codepoint));
        CHECK(bitmaps.count(glyph.bitmap.inverted()) == 0);
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
