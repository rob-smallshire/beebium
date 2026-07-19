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

#include "BuiltinGlyphSets.hpp"

#include <stdexcept>
#include <string>

#include "screentext/Glyph.hpp"

namespace screentext {

namespace {

// Built once, on first use, and never mutated afterwards, so that handing out
// references to the sets is safe and the same set is shared by every caller.
const std::vector<GlyphSet>& builtin_sets()
{
    static const std::vector<GlyphSet> sets = {
        builtin::make_acorn_mos_1_20(),
    };
    return sets;
}

} // namespace

std::vector<std::string> builtin_glyph_set_names()
{
    std::vector<std::string> names;
    names.reserve(builtin_sets().size());
    for (const GlyphSet& set : builtin_sets()) {
        names.push_back(set.name);
    }
    return names;
}

const GlyphSet* find_builtin_glyph_set(std::string_view name)
{
    for (const GlyphSet& set : builtin_sets()) {
        if (set.name == name) {
            return &set;
        }
    }
    return nullptr;
}

const GlyphSet& builtin_glyph_set(std::string_view name)
{
    if (const GlyphSet* set = find_builtin_glyph_set(name)) {
        return *set;
    }
    throw std::out_of_range("no built-in glyph set named " + std::string(name));
}

} // namespace screentext
