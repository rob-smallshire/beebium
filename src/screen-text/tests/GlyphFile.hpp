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

#ifndef SCREENTEXT_TESTS_GLYPHFILE_HPP
#define SCREENTEXT_TESTS_GLYPHFILE_HPP

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "screentext/Glyph.hpp"

namespace screentext::testing {

// Read a glyph set in the plain format the CLI accepts: one glyph per line,
// a codepoint then one byte per row, with # starting a comment.
//
// The same parsing lives in the CLI. It is repeated here rather than made
// part of the library because the format is a convenience for people typing
// files, not something the library itself needs to know about.
inline GlyphSet load_glyph_file(const std::string& filepath,
                                const std::string& name)
{
    std::ifstream stream(filepath);
    if (!stream) {
        throw std::runtime_error("cannot open glyph file " + filepath);
    }

    GlyphSet set;
    set.name = name;

    std::string line;
    while (std::getline(stream, line)) {
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }

        std::istringstream fields(line);
        std::string token;
        if (!(fields >> token)) {
            continue;
        }

        const auto value_of = [](const std::string& text) {
            return static_cast<unsigned long>(
                std::strtoul(text.c_str(), nullptr, 16));
        };

        const char32_t codepoint = static_cast<char32_t>(value_of(token));
        std::vector<std::uint8_t> rows;
        while (fields >> token) {
            rows.push_back(static_cast<std::uint8_t>(value_of(token)));
        }
        if (rows.empty()) {
            throw std::runtime_error("glyph with no rows in " + filepath);
        }
        set.glyphs.push_back(Glyph(codepoint, Bitmap::from_rows(rows)));
    }

    if (set.glyphs.empty()) {
        throw std::runtime_error("no glyphs in " + filepath);
    }
    return set;
}

} // namespace screentext::testing

#endif // SCREENTEXT_TESTS_GLYPHFILE_HPP
