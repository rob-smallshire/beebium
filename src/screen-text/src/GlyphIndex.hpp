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

#ifndef SCREENTEXT_GLYPHINDEX_HPP
#define SCREENTEXT_GLYPHINDEX_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "screentext/Bitmap.hpp"
#include "screentext/Glyph.hpp"

namespace screentext {

// Bitmap-to-character lookup built from the supplied glyph sets.
//
// B-Em and ZEsarUX both scan their glyph tables linearly. A hash map is a
// strict improvement and costs nothing, because the comparison is exact
// equality either way -- there is no approximate matching anywhere in this
// design for a map to preclude.
class GlyphIndex {
public:
    struct Match {
        char32_t codepoint = 0;
        const std::string* glyph_set = nullptr;
    };

    // Later sets take precedence over earlier ones, so a caller can override
    // individual characters. Within one set, a later glyph likewise displaces
    // an earlier one with the same bitmap, so the outcome never depends on
    // iteration order.
    explicit GlyphIndex(const std::vector<GlyphSet>& sets);

    // Null when the bitmap is not a glyph. Exact equality, nothing else.
    // Callers reduce a cell both ways round and look up each.
    const Match* find(const Bitmap& bitmap) const;

    bool empty() const { return glyphs_.empty(); }

private:
    // The set names, owned here so that a Match can hand out a stable pointer
    // even after the caller's sets have gone.
    std::vector<std::string> names_;
    std::unordered_map<Bitmap, Match> glyphs_;
};

} // namespace screentext

#endif // SCREENTEXT_GLYPHINDEX_HPP
