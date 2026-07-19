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

namespace {

// True when a line of pixels holds more than one run of set pixels: a gap
// between ink, which is the mark of a letter rather than a filled shape.
bool broken(const Bitmap& bitmap, std::size_t length,
            bool (*sample)(const Bitmap&, std::size_t, std::size_t),
            std::size_t fixed)
{
    std::size_t runs = 0;
    bool previous = false;
    for (std::size_t i = 0; i < length; ++i) {
        const bool set = sample(bitmap, fixed, i);
        if (set && !previous) {
            ++runs;
        }
        previous = set;
    }
    return runs > 1;
}

bool at_row(const Bitmap& b, std::size_t y, std::size_t x) { return b.pixel(x, y); }
bool at_col(const Bitmap& b, std::size_t x, std::size_t y) { return b.pixel(x, y); }

// A bitmap is HV-convex when every row and every column holds at most one run
// of set pixels: the shape of a filled region, an edge, a bar, a blob or a
// diagonal, which is what a picture is made of. A glyph is distinctive when it
// is not -- when a concavity or a hole breaks some row or column into two --
// because imagery produces those only by coincidence. No threshold, so it
// judges an unseen font as readily as the ROM's.
bool is_hv_convex(const Bitmap& bitmap)
{
    const std::size_t width = bitmap.width();
    const std::size_t height = bitmap.height();
    for (std::size_t y = 0; y < height; ++y) {
        if (broken(bitmap, width, at_row, y)) {
            return false;
        }
    }
    for (std::size_t x = 0; x < width; ++x) {
        if (broken(bitmap, height, at_col, x)) {
            return false;
        }
    }
    return true;
}

} // namespace

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
            match.distinctive = !is_hv_convex(glyph.bitmap);
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
