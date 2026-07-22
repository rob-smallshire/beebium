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

#include <algorithm>

namespace beebium {

namespace {

// Append a codepoint to a UTF-8 string.
void append_utf8(std::string& out, char32_t codepoint) {
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

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

            // Everything that is not readable alpha text occupies its column
            // as a space, so what is copied lines up with what is displayed.
            const bool readable =
                !cell.is_control_code
                && !cell.concealed
                && !cell.double_height_bottom
                && cell.charset == TeletextCellCharset::Alpha;

            const char32_t codepoint =
                readable ? teletext_alpha_codepoint(cell.character, characters) : 0;

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
