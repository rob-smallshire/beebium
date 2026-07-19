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

    // The two pixel values the cell was drawn from: the one the glyph is
    // drawn in, and the one around it. Equal when the cell is blank, there
    // being no glyph to have a colour. Meaningful only when `matched()`.
    //
    // There is deliberately no "inverted" flag. A cell holds two colours and
    // a shape, and neither arrangement of them is the real one that the other
    // is a reversal of -- calling one inverse would be asserting a canonical
    // orientation that does not exist. Which colour the glyph is drawn in is
    // the fact worth having, and it is reported rather than reduced to a
    // boolean about it. A caller wanting to know which cells stand out from
    // their neighbours can compare backgrounds and decide for itself, with
    // the whole picture in view rather than one cell at a time.
    std::uint8_t foreground = 0;
    std::uint8_t background = 0;

    // The other characters this cell could equally be, lowest first, empty in
    // the ordinary case. Fonts collide -- '0' with 'O', 'l' with '|', and in
    // one real font 'I', 'l' and '|' all at once -- and when they do, the
    // pixels simply do not say which was meant.
    //
    // `codepoint` still holds one of them, so the text remains usable, but a
    // caller is told it was a choice rather than a reading. Silently picking
    // one and saying nothing is the same mistake as turning an unreadable
    // cell into a space.
    std::vector<char32_t> alternatives;

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

    // True when the glyph set does not distinguish this bitmap from some
    // other character's.
    bool ambiguous() const { return !alternatives.empty(); }
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

    // Cells whose bitmap could equally be some other character.
    std::size_t ambiguous_cells() const;
};

struct Result {
    std::vector<Run> runs;

    // Over every cell read, including those outside any run.
    std::size_t unmatched_cells = 0;

    // Cells that matched, but whose bitmap the glyph sets do not distinguish
    // from some other character's. Read alongside `unmatched_cells`: one
    // counts what could not be read, the other what could not be pinned down.
    std::size_t ambiguous_cells = 0;

    std::size_t total_cells = 0;

    // The runs joined with newlines, for a caller that only wants the text.
    // Everything a caller needs to judge what was uncertain is in `runs`.
    std::string text() const;
};

// Read text from an image.
//
// `glyph_sets` are searched with later sets taking precedence over earlier
// ones. Matching is exact byte comparison throughout: a cell either is a
// glyph or is not.
//
// A cell's two colours are tried both ways round, and which way matched says
// which colour the glyph was drawn in. Where both ways match a glyph -- which
// needs a glyph set holding some glyph's complement, and no built-in set
// does -- the reading whose background covers more of the cell is taken, the
// lower pixel value breaking a tie.
//
// Throws std::invalid_argument when the image is inconsistent with its
// dimensions, or a band has a zero cell size.
Result read(const Image& image,
            const std::vector<Band>& bands,
            const std::vector<GlyphSet>& glyph_sets,
            const Options& options = {});

} // namespace screentext

#endif // SCREENTEXT_READ_HPP
