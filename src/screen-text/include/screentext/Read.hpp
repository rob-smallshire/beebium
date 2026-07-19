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

#ifndef SCREENTEXT_READ_HPP
#define SCREENTEXT_READ_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "screentext/Glyph.hpp"
#include "screentext/Image.hpp"

namespace screentext {

enum class Search {
    // Character cells only: fast, exact, well structured, and what reading a
    // listing or an error message needs.
    AlignedOnly,

    // Also search sub-cell offsets, for text that is not on the character
    // grid. Accepted but not yet honoured; reading is aligned either way.
    IncludeOffset,
};

struct Options {
    Search search = Search::AlignedOnly;

    // Also compare each cell against the complement of every glyph. The BBC
    // produces inverse text routinely by swapping foreground and background,
    // so this is not an edge case and is on by default.
    bool match_inverted = true;

    // Region to read, in image pixels. The whole image when unset. A cell not
    // wholly inside it is not read, so a selection clips runs at cell
    // boundaries rather than splitting characters.
    std::optional<Rect> selection;
};

// One cell that was matched, or could not be.
struct Cell {
    Rect bounds;

    // What the cell says. Zero when nothing matched -- never a guess.
    char32_t codepoint = 0;

    // Matched against the complement of a glyph rather than the glyph.
    bool inverted = false;

    // Matched away from the character grid. Never set while offset search is
    // unimplemented.
    bool offset = false;

    // Which set matched. Empty when unmatched.
    std::string glyph_set;

    // Distinguishes "I could not read this" from "this was blank", which is
    // the distinction the whole design rests on. An unmatched cell still
    // contributes a space to `Run::text`, so columns stay aligned, but says
    // here that it was unread.
    bool matched() const { return codepoint != 0; }
};

// A contiguous run of cells on one character row.
//
// One run per row that holds anything worth reporting, with leading and
// trailing blanks trimmed and interior spacing preserved, so that columns
// line up as they did on screen.
struct Run {
    std::string text; // UTF-8
    Rect bounds;
    std::vector<Cell> cells;

    // Cells in this run that matched nothing.
    std::size_t unmatched_cells() const;
};

struct Result {
    std::vector<Run> runs;

    // Over every cell read, including those outside any run.
    std::size_t unmatched_cells = 0;
    std::size_t total_cells = 0;

    // The runs joined with newlines, for a caller that only wants the text.
    // Everything a caller needs to judge what was uncertain is in `runs`.
    std::string text() const;
};

// Read text from an image.
//
// `glyph_sets` are searched with later sets taking precedence over earlier
// ones, and an upright match taking precedence over an inverted one. Matching
// is exact byte comparison throughout: a cell either is a glyph or is not.
//
// Throws std::invalid_argument when the image is inconsistent with its
// dimensions, or a band has a zero cell size.
Result read(const Image& image,
            const std::vector<Band>& bands,
            const std::vector<GlyphSet>& glyph_sets,
            const Options& options = {});

} // namespace screentext

#endif // SCREENTEXT_READ_HPP
