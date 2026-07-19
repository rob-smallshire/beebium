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

#ifndef SCREENTEXT_GLYPH_HPP
#define SCREENTEXT_GLYPH_HPP

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "screentext/Bitmap.hpp"

namespace screentext {

// A glyph: a bitmap and what it means as text.
struct Glyph {
    Bitmap bitmap;
    char32_t codepoint = 0;

    Glyph() = default;
    Glyph(char32_t codepoint_, Bitmap bitmap_)
        : bitmap(std::move(bitmap_)), codepoint(codepoint_)
    {
    }

    // Eight pixels wide, one byte per row -- the shape of a font ROM dump.
    static Glyph from_rows(char32_t codepoint,
                           std::initializer_list<std::uint8_t> rows);
};

// A named collection of glyphs.
//
// When several sets are supplied to `read`, later sets take precedence over
// earlier ones, so a caller can override individual characters -- which is
// how VDU 23 redefinitions would arrive -- without restating a whole set.
struct GlyphSet {
    std::string name;
    std::vector<Glyph> glyphs;
};

// Built-in sets, by name. Baked in at build time: the library never reads a
// ROM at runtime and does not need one to build.
std::vector<std::string> builtin_glyph_set_names();

// Throws std::out_of_range when no such set exists.
const GlyphSet& builtin_glyph_set(std::string_view name);

// Null when no such set exists, for callers that would rather branch than
// catch.
const GlyphSet* find_builtin_glyph_set(std::string_view name);

} // namespace screentext

#endif // SCREENTEXT_GLYPH_HPP
