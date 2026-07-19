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

#include <algorithm>

namespace screentext {

GlyphIndex::GlyphIndex(const std::vector<GlyphSet>& sets)
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

            const auto existing = glyphs_.find(glyph.bitmap);
            if (existing != glyphs_.end() && owners_[glyph.bitmap] == index) {
                // Another glyph of the same set has this bitmap. Neither
                // overrides the other; keep both, lowest codepoint reported.
                Match& match = existing->second;
                std::vector<char32_t> all = match.alternatives;
                all.push_back(match.codepoint);
                all.push_back(glyph.codepoint);
                std::sort(all.begin(), all.end());
                all.erase(std::unique(all.begin(), all.end()), all.end());

                match.codepoint = all.front();
                match.alternatives.assign(all.begin() + 1, all.end());
                continue;
            }

            // A new bitmap, or one from an earlier set being overridden.
            Match match;
            match.codepoint = glyph.codepoint;
            match.glyph_set = &names_[index];
            glyphs_.insert_or_assign(glyph.bitmap, match);
            owners_[glyph.bitmap] = index;
        }
    }
}

const GlyphIndex::Match* GlyphIndex::find(const Bitmap& bitmap) const
{
    if (const auto found = glyphs_.find(bitmap); found != glyphs_.end()) {
        return &found->second;
    }
    return nullptr;
}

} // namespace screentext
