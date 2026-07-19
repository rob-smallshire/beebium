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

// test_teletext_text.cpp
//
// Turning captured MODE 7 cells into text.
//
// The governing rule is "copy what is displayed", which is what settles the
// awkward cases: graphics, control codes, concealed text and the bottom halves
// of double-height rows all occupy their column as a space rather than
// contributing characters. Non-ASCII inputs are written as UTF-8 byte escapes
// so this file stays 7-bit clean.

#include <catch2/catch_test_macros.hpp>

#include <beebium/TeletextText.hpp>

#include <vector>

using namespace beebium;

namespace {

// Build a screen from rows of plain ASCII, one string per row.
//
// Returns a snapshot rather than the grid: that is what a reader outside the
// emulation thread holds, so the tests exercise the same path the service does.
TeletextGrid::Snapshot grid_of(const std::vector<std::string>& rows) {
    TeletextGrid grid;
    for (size_t row = 0; row < rows.size() && row < TeletextGrid::ROWS; ++row) {
        const std::string& text = rows[row];
        for (size_t column = 0;
             column < text.size() && column < TeletextGrid::COLUMNS;
             ++column) {
            TeletextCell cell;
            cell.character = static_cast<uint8_t>(text[column]);
            grid.set_cell(row, column, cell);
        }
    }
    grid.swap();
    return grid.snapshot();
}

// Place one cell, fully specified, in an otherwise empty screen.
TeletextGrid::Snapshot grid_with(const TeletextCell& cell) {
    TeletextGrid grid;
    grid.set_cell(0, 0, cell);
    grid.swap();
    return grid.snapshot();
}

} // namespace

// ============================================================================
// Plain text
// ============================================================================

TEST_CASE("An empty screen copies as nothing", "[teletext-text]") {
    TeletextGrid grid;
    grid.swap();
    CHECK(teletext_text(grid.snapshot()) == "");
}

TEST_CASE("Rows copy as lines", "[teletext-text]") {
    const auto grid = grid_of({"HELLO", "WORLD"});
    CHECK(teletext_text(grid) == "HELLO\nWORLD");
}

TEST_CASE("Trailing spaces are stripped from each line", "[teletext-text]") {
    const auto grid = grid_of({"HI   ", "THERE  "});
    CHECK(teletext_text(grid) == "HI\nTHERE");
}

TEST_CASE("Blank rows between text are preserved", "[teletext-text]") {
    // The gap is part of what is on screen.
    const auto grid = grid_of({"TOP", "", "BOTTOM"});
    CHECK(teletext_text(grid) == "TOP\n\nBOTTOM");
}

TEST_CASE("Trailing blank rows are dropped", "[teletext-text]") {
    // Otherwise every copy ends with two dozen empty lines.
    const auto grid = grid_of({"ONLY LINE"});
    CHECK(teletext_text(grid) == "ONLY LINE");
}

TEST_CASE("Leading spaces are kept", "[teletext-text]") {
    // Indentation is information; only trailing padding is noise.
    const auto grid = grid_of({"   INDENTED"});
    CHECK(teletext_text(grid) == "   INDENTED");
}

// ============================================================================
// What does not copy as text
// ============================================================================

TEST_CASE("Graphics cells copy as spaces", "[teletext-text]") {
    // Mosaics have Unicode forms and a renderer should use them, but text
    // pasted into a bug report should not be full of block characters.
    TeletextCell mosaic;
    mosaic.character = 0x7F;
    mosaic.charset = TeletextCellCharset::ContiguousGraphics;
    CHECK(teletext_text(grid_with(mosaic)) == "");

    mosaic.charset = TeletextCellCharset::SeparatedGraphics;
    CHECK(teletext_text(grid_with(mosaic)) == "");
}

TEST_CASE("Control codes copy as spaces", "[teletext-text]") {
    TeletextCell control;
    control.character = 0x11;
    control.is_control_code = true;
    CHECK(teletext_text(grid_with(control)) == "");
}

TEST_CASE("Concealed cells copy as spaces", "[teletext-text]") {
    // Copying hidden text would leak what the display deliberately conceals.
    TeletextCell hidden;
    hidden.character = 'X';
    hidden.concealed = true;
    CHECK(teletext_text(grid_with(hidden)) == "");
}

TEST_CASE("A double-height row copies once, not twice", "[teletext-text]") {
    TeletextGrid grid;

    TeletextCell top;
    top.character = 'A';
    top.double_height_top = true;
    grid.set_cell(0, 0, top);

    TeletextCell bottom;
    bottom.character = 'A';
    bottom.double_height_bottom = true;
    grid.set_cell(1, 0, bottom);
    grid.swap();

    CHECK(teletext_text(grid.snapshot()) == "A");
}

TEST_CASE("Control codes hold their column", "[teletext-text]") {
    // Column alignment is the main reason for copying a teletext screen, so a
    // control code must occupy its cell rather than close the gap.
    TeletextGrid grid;

    TeletextCell control;
    control.character = 0x11;
    control.is_control_code = true;
    grid.set_cell(0, 0, control);

    TeletextCell letter;
    letter.character = 'A';
    grid.set_cell(0, 1, letter);
    grid.swap();

    CHECK(teletext_text(grid.snapshot()) == " A");
}

// ============================================================================
// The SAA5050 repertoire
// ============================================================================

TEST_CASE("Characters copy as what the screen displays", "[teletext-text]") {
    // The familiar MODE 7 surprise: 0x23 shows a pound sign, 0x5F a hash.
    // Copying the ASCII byte instead would produce text that does not match
    // the screen.
    TeletextCell cell;

    cell.character = 0x23;
    CHECK(teletext_text(grid_with(cell)) == "\xC2\xA3");  // U+00A3

    cell.character = 0x5F;
    CHECK(teletext_text(grid_with(cell)) == "#");

    cell.character = 0x5C;
    CHECK(teletext_text(grid_with(cell)) == "\xC2\xBD");  // U+00BD one half

    cell.character = 0x7E;
    CHECK(teletext_text(grid_with(cell)) == "\xC3\xB7");  // U+00F7 division
}

TEST_CASE("Ordinary ASCII is unchanged", "[teletext-text]") {
    const auto grid = grid_of({"AZ az 09 !?"});
    CHECK(teletext_text(grid) == "AZ az 09 !?");
}

TEST_CASE("The alpha mapping is the inverse of the paste substitutions",
          "[teletext-text]") {
    // Text copied from a MODE 7 screen must paste back as the same characters,
    // so this table and the teletext entries in TextTranslation.cpp have to
    // agree. See the kTeletext table there.
    CHECK(teletext_alpha_codepoint(0x5B) == 0x2190);  // [ shows as left arrow
    CHECK(teletext_alpha_codepoint(0x5D) == 0x2192);  // ] right arrow
    CHECK(teletext_alpha_codepoint(0x5E) == 0x2191);  // ^ up arrow
    CHECK(teletext_alpha_codepoint(0x7B) == 0x00BC);  // { one quarter
    CHECK(teletext_alpha_codepoint(0x7D) == 0x00BE);  // } three quarters
}

// ============================================================================
// Regions
// ============================================================================

TEST_CASE("A region copies only its own cells", "[teletext-text]") {
    const auto grid = grid_of({"ABCDEFGH", "IJKLMNOP", "QRSTUVWX"});

    TeletextRegion region;
    region.row = 1;
    region.column = 2;
    region.rows = 2;
    region.columns = 3;

    CHECK(teletext_text(grid, region) == "KLM\nSTU");
}

TEST_CASE("A region beyond the grid is clipped, not an error",
          "[teletext-text]") {
    const auto grid = grid_of({"ABC"});

    TeletextRegion region;
    region.row = TeletextGrid::ROWS - 1;
    region.column = TeletextGrid::COLUMNS - 2;
    region.rows = 100;
    region.columns = 100;

    CHECK(teletext_text(grid, region) == "");
}

// ============================================================================
// Linearisation
// ============================================================================

TEST_CASE("Rows linearisation keeps the shape of the selection",
          "[teletext-text]") {
    // A column of figures must stay a column.
    const auto grid = grid_of({"12", "34"});
    CHECK(teletext_text(grid, TeletextRegion::whole_screen(),
                        TeletextLinearisation::Rows) == "12\n34");
}

TEST_CASE("Flowed linearisation rejoins a wrapped line", "[teletext-text]") {
    // A row filled to the right edge wrapped, so it continues into the next.
    std::string full_row(TeletextGrid::COLUMNS, 'A');
    const auto grid = grid_of({full_row, "BBB"});

    const std::string flowed = teletext_text(
        grid, TeletextRegion::whole_screen(), TeletextLinearisation::Flowed);

    CHECK(flowed == full_row + "BBB");
}

TEST_CASE("Flowed linearisation still breaks where a line ended",
          "[teletext-text]") {
    // A row that stopped short of the edge was a line of its own.
    const auto grid = grid_of({"SHORT", "NEXT"});

    CHECK(teletext_text(grid, TeletextRegion::whole_screen(),
                        TeletextLinearisation::Flowed) == "SHORT\nNEXT");
}
