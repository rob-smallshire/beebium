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

#include <beebium/TeletextText.hpp>

#include <beebium/Utf8.hpp>

#include <algorithm>

namespace beebium {

namespace {


// Strip trailing spaces from the end of the accumulated text.
void strip_trailing_spaces(std::string& text) {
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
}

} // namespace

char32_t teletext_alpha_codepoint(uint8_t character, TeletextCharacters characters) {
    // Under Displayed, the eleven positions where the SAA5050's UK repertoire
    // departs from ASCII. This is the source of the familiar MODE 7 surprise
    // that '#' shows as a pound sign and '_' shows as a hash.
    //
    // Deliberately the inverse of the teletext substitutions in
    // TextTranslation.cpp, so text copied this way pastes back as the same
    // bytes. Those substitutions also mean paste needs no equivalent choice:
    // every one of these codepoints lies outside ASCII, so a left arrow and a
    // '[' both type &5B and the two readings converge on the way back in.
    if (characters == TeletextCharacters::Displayed) {
        switch (character) {
            case 0x23: return 0x00A3;  // POUND SIGN
            case 0x5B: return 0x2190;  // LEFTWARDS ARROW
            case 0x5C: return 0x00BD;  // VULGAR FRACTION ONE HALF
            case 0x5D: return 0x2192;  // RIGHTWARDS ARROW
            case 0x5E: return 0x2191;  // UPWARDS ARROW
            case 0x5F: return 0x0023;  // NUMBER SIGN
            case 0x60: return 0x2015;  // HORIZONTAL BAR
            case 0x7B: return 0x00BC;  // VULGAR FRACTION ONE QUARTER
            case 0x7C: return 0x2016;  // DOUBLE VERTICAL LINE
            case 0x7D: return 0x00BE;  // VULGAR FRACTION THREE QUARTERS
            case 0x7E: return 0x00F7;  // DIVISION SIGN
            default: break;
        }
    }

    // Under Codes, and for every character either way, the byte is itself.
    if (character >= 0x20 && character < 0x7F) {
        return character;
    }
    return 0;
}

char32_t teletext_graphics_codepoint(uint8_t character) {
    // Bit 5 is the graphics marker, not part of the pattern: without it the
    // code is one of the alphanumerics that show through in graphics mode.
    if (!(character & 0x20)) {
        return 0;
    }

    // Gather the six lit blocks into the order Unicode numbers them: top pair,
    // middle pair, bottom pair, left before right. The chip's bottom-right bit
    // is 6, not 5, because 5 was spent marking the code as graphics.
    const uint8_t pattern =
        static_cast<uint8_t>(((character & 0x01) ? 0x01 : 0) |
                             ((character & 0x02) ? 0x02 : 0) |
                             ((character & 0x04) ? 0x04 : 0) |
                             ((character & 0x08) ? 0x08 : 0) |
                             ((character & 0x10) ? 0x10 : 0) |
                             ((character & 0x40) ? 0x20 : 0));

    // The four patterns Unicode already had characters for, which is why the
    // sextant block holds sixty and not sixty-four.
    switch (pattern) {
        case 0x00: return 0x0020;  // SPACE
        case 0x15: return 0x258C;  // LEFT HALF BLOCK
        case 0x2A: return 0x2590;  // RIGHT HALF BLOCK
        case 0x3F: return 0x2588;  // FULL BLOCK
        default: break;
    }

    // The rest run in pattern order from U+1FB00, skipping those four. Only
    // two fall below any given pattern here, the blank and the full block
    // being the ends.
    const uint32_t skipped = (pattern > 0x15 ? 1u : 0u) + (pattern > 0x2A ? 1u : 0u);
    return 0x1FB00 + (pattern - 1 - skipped);
}

char32_t teletext_cell_codepoint(const TeletextCell& cell,
                                 TeletextCharacters characters) {
    // Showing nothing: a concealed cell is hidden until the viewer reveals it,
    // and the lower half of a double-height row repeats the row above rather
    // than saying anything new.
    if (cell.concealed || cell.double_height_bottom) {
        return 0;
    }

    if (cell.charset != TeletextCellCharset::Alpha && (cell.character & 0x20)) {
        // A mosaic. Only the Displayed reading has anything to say about it:
        // under Codes the byte is a graphics code, not text.
        //
        // Reached by held-graphics cells too, whose byte is a control code but
        // which draw the held mosaic -- the capture resolved that, so there is
        // nothing to re-derive here.
        return characters == TeletextCharacters::Displayed
                   ? teletext_graphics_codepoint(cell.character)
                   : 0;
    }

    if (cell.is_control_code) {
        return 0;
    }

    // Alpha, either because the cell is in alpha mode or because it is one of
    // the alphanumerics that show through in graphics mode: 0x40-0x5F, where
    // the graphics marker bit is clear.
    return teletext_alpha_codepoint(cell.character, characters);
}

namespace {

// Shared by the live grid and a snapshot: both expose cell(row, column).
template <typename Source>
std::string convert(const Source& grid,
                    const TeletextRegion& region,
                    TeletextLinearisation linearisation,
                    TeletextCharacters characters) {
    const size_t first_row = std::min(region.row, TeletextGrid::ROWS);
    const size_t first_column = std::min(region.column, TeletextGrid::COLUMNS);
    const size_t last_row = std::min(first_row + region.rows, TeletextGrid::ROWS);
    const size_t last_column =
        std::min(first_column + region.columns, TeletextGrid::COLUMNS);

    std::string text;
    std::string line;
    bool previous_row_reached_the_edge = false;

    for (size_t row = first_row; row < last_row; ++row) {
        line.clear();

        for (size_t column = first_column; column < last_column; ++column) {
            const TeletextCell& cell = grid.cell(row, column);

            const char32_t codepoint = teletext_cell_codepoint(cell, characters);

            if (codepoint == 0) {
                line.push_back(' ');
            } else {
                append_utf8(line, codepoint);
            }
        }

        // Whether the row ran right up to the edge, measured before trailing
        // spaces are stripped: that is what distinguishes a wrapped line from
        // one that simply ended.
        const bool reached_the_edge = !line.empty() && line.back() != ' ';

        strip_trailing_spaces(line);

        if (linearisation == TeletextLinearisation::Rows) {
            if (row != first_row) {
                text.push_back('\n');
            }
            text += line;
        } else {
            // Flowed: a row filled to the edge wrapped, so it continues
            // straight into the next with no break. A short row ended a line.
            if (row != first_row && !previous_row_reached_the_edge) {
                text.push_back('\n');
            }
            text += line;
        }

        previous_row_reached_the_edge = reached_the_edge;
    }

    // A screen is 25 rows whatever is written on it, so a copy otherwise ends
    // with a run of empty lines from the blank rows below the text.
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }

    return text;
}

} // namespace

std::string teletext_text(const TeletextGrid& grid,
                          const TeletextRegion& region,
                          TeletextLinearisation linearisation,
                          TeletextCharacters characters) {
    return convert(grid, region, linearisation, characters);
}

std::string teletext_text(const TeletextGrid::Snapshot& snapshot,
                          const TeletextRegion& region,
                          TeletextLinearisation linearisation,
                          TeletextCharacters characters) {
    return convert(snapshot, region, linearisation, characters);
}

} // namespace beebium
