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

#include "GlyphIndex.hpp"

namespace screentext {

GlyphIndex::GlyphIndex(const std::vector<GlyphSet>& sets, bool match_inverted)
{
    names_.reserve(sets.size());
    for (const GlyphSet& set : sets) {
        names_.push_back(set.name);
    }

    for (std::size_t index = 0; index < sets.size(); ++index) {
        const GlyphSet& set = sets[index];
        for (const Glyph& glyph : set.glyphs) {
            if (glyph.bitmap.empty()) {
                continue;
            }

            Match match;
            match.codepoint = glyph.codepoint;
            match.glyph_set = &names_[index];

            // insert_or_assign, so a later set displaces an earlier one.
            match.inverted = false;
            upright_.insert_or_assign(glyph.bitmap, match);

            if (match_inverted) {
                match.inverted = true;
                inverted_.insert_or_assign(glyph.bitmap.inverted(), match);
            }
        }
    }
}

const GlyphIndex::Match* GlyphIndex::find(const Bitmap& bitmap) const
{
    if (const auto found = upright_.find(bitmap); found != upright_.end()) {
        return &found->second;
    }
    if (const auto found = inverted_.find(bitmap); found != inverted_.end()) {
        return &found->second;
    }
    return nullptr;
}

} // namespace screentext
