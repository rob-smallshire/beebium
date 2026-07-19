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

        // The other characters this exact bitmap could equally be, lowest
        // first, empty in the ordinary case. Real fonts collide: '0' with
        // 'O', 'l' with '|', and in one of them 'I', 'l' and '|' all at once.
        // Nothing can separate those from the pixels, so the alternatives
        // are carried rather than quietly discarded.
        std::vector<char32_t> alternatives;

        // Whether the glyph's shape is one that imagery does not produce by
        // accident: not HV-convex, so some row or column of it holds more than
        // one run of set pixels. The off-grid search needs this to tell text
        // from the filled regions, edges, bars and blobs a picture is made
        // of. Computed once, from the bitmap alone -- no threshold, so it
        // holds for a font nobody has seen. Meaningless to the aligned path,
        // which never reads it.
        bool distinctive = false;
    };

    // Later sets take precedence over earlier ones, so a caller can override
    // individual characters: an override replaces what an earlier set said
    // rather than competing with it.
    //
    // Two glyphs of one set sharing a bitmap is a different matter. Neither
    // overrides the other and no reader can separate them, so both are kept:
    // the lowest codepoint is reported and the rest become alternatives. The
    // choice is by value rather than by position, so it does not depend on
    // the order glyphs were listed in.
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

    // Which set each bitmap came from, so that a second glyph from the same
    // set is recognised as an ambiguity rather than an override.
    std::unordered_map<Bitmap, std::size_t> owners_;
};

} // namespace screentext

#endif // SCREENTEXT_GLYPHINDEX_HPP
