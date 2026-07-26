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

#include <set>

#include <algorithm>
#include <vector>

using namespace beebium;

namespace {

// Build a screen from rows of plain ASCII, one string per row.
//
// Returns a snapshot rather than the grid: that is what a reader outside the
// emulation thread holds, so the tests exercise the same path the service does.
TeletextGrid::Snapshot grid_of(const std::vector<std::string>& rows) {
    TeletextGrid grid;
    // Fill the whole standard screen, spaces included, as a real capture does,
    // rather than only the cells that carry text.
    for (size_t row = 0; row < TeletextGrid::DEFAULT_ROWS; ++row) {
        const std::string& text = row < rows.size() ? rows[row] : std::string();
        for (size_t column = 0; column < TeletextGrid::DEFAULT_COLUMNS; ++column) {
            TeletextCell cell;
            cell.character = column < text.size()
                                 ? static_cast<uint8_t>(text[column])
                                 : static_cast<uint8_t>(' ');
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

TEST_CASE("Graphics cells copy as spaces under Codes", "[teletext-text]") {
    // Under Codes a mosaic byte is a graphics code and not text, so it holds
    // its column and says nothing. The Displayed reading is where it becomes
    // the block it drew -- see the sixel cases below.
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

TEST_CASE("By default a character copies as the byte it is", "[teletext-text]") {
    // The default repertoire is Codes, because the commonest thing anyone
    // copies off a BBC screen is a program listing and MODE 7 is the default
    // mode: '[' and ']' delimit assembler blocks in BBC BASIC and '^' is
    // exponentiation, so reading them as arrows corrupts the listing.
    TeletextCell cell;

    cell.character = 0x5B;
    CHECK(teletext_text(grid_with(cell)) == "[");

    cell.character = 0x5D;
    CHECK(teletext_text(grid_with(cell)) == "]");

    cell.character = 0x5E;
    CHECK(teletext_text(grid_with(cell)) == "^");

    cell.character = 0x7E;
    CHECK(teletext_text(grid_with(cell)) == "~");

    cell.character = 0x23;
    CHECK(teletext_text(grid_with(cell)) == "#");
}

TEST_CASE("A BASIC listing survives being copied from MODE 7",
          "[teletext-text]") {
    // The case the default exists for. An assembler block and a power operator
    // must come back as themselves, not as arrows.
    const auto grid = grid_of({"10 P%=&2000", "20 [OPT2:LDA #&41:]", "30 A=B^2"});

    const std::string text = teletext_text(grid);

    CHECK(text.find("[OPT2") != std::string::npos);
    CHECK(text.find(":]") != std::string::npos);
    CHECK(text.find("B^2") != std::string::npos);
}

TEST_CASE("Characters copy as what the screen displays when asked",
          "[teletext-text]") {
    // The familiar MODE 7 surprise: 0x23 shows a pound sign, 0x5F a hash.
    // Copying the ASCII byte instead would produce text that does not match
    // the screen.
    TeletextCell cell;

    const auto displayed = [](const TeletextGrid::Snapshot& screen) {
        return teletext_text(screen, TeletextRegion::whole_screen(),
                             TeletextLinearisation::Rows,
                             TeletextCharacters::Displayed);
    };

    cell.character = 0x23;
    CHECK(displayed(grid_with(cell)) == "\xC2\xA3");  // U+00A3

    cell.character = 0x5F;
    CHECK(displayed(grid_with(cell)) == "#");

    cell.character = 0x5C;
    CHECK(displayed(grid_with(cell)) == "\xC2\xBD");  // U+00BD one half

    cell.character = 0x7E;
    CHECK(displayed(grid_with(cell)) == "\xC3\xB7");  // U+00F7 division

    cell.character = 0x5B;
    CHECK(displayed(grid_with(cell)) == "\xE2\x86\x90");  // U+2190 left arrow
}

TEST_CASE("Ordinary ASCII is unchanged", "[teletext-text]") {
    const auto grid = grid_of({"AZ az 09 !?"});
    CHECK(teletext_text(grid) == "AZ az 09 !?");
}

TEST_CASE("Only the eleven divergent codes differ between the repertoires",
          "[teletext-text]") {
    // Every other printable character means itself either way, so the choice
    // is narrow and a caller who picks the wrong one loses only these.
    static constexpr uint8_t divergent[] = {0x23, 0x5B, 0x5C, 0x5D, 0x5E,
                                            0x5F, 0x60, 0x7B, 0x7C, 0x7D, 0x7E};

    for (uint8_t code = 0x20; code < 0x7F; ++code) {
        const bool is_divergent =
            std::find(std::begin(divergent), std::end(divergent), code)
            != std::end(divergent);

        const char32_t codes =
            teletext_alpha_codepoint(code, TeletextCharacters::Codes);
        const char32_t shown =
            teletext_alpha_codepoint(code, TeletextCharacters::Displayed);

        INFO("code " << static_cast<int>(code));
        CHECK(codes == code);
        CHECK((codes != shown) == is_divergent);
    }
}

TEST_CASE("The alpha mapping is the inverse of the paste substitutions",
          "[teletext-text]") {
    // Text copied from a MODE 7 screen must paste back as the same characters,
    // so this table and the teletext entries in TextTranslation.cpp have to
    // agree. See the kTeletext table there.
    using enum TeletextCharacters;
    CHECK(teletext_alpha_codepoint(0x5B, Displayed) == 0x2190);  // [ left arrow
    CHECK(teletext_alpha_codepoint(0x5D, Displayed) == 0x2192);  // ] right arrow
    CHECK(teletext_alpha_codepoint(0x5E, Displayed) == 0x2191);  // ^ up arrow
    CHECK(teletext_alpha_codepoint(0x7B, Displayed) == 0x00BC);  // { one quarter
    CHECK(teletext_alpha_codepoint(0x7D, Displayed) == 0x00BE);  // } three quarters
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
    region.row = TeletextGrid::DEFAULT_ROWS - 1;
    region.column = TeletextGrid::DEFAULT_COLUMNS - 2;
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
    std::string full_row(TeletextGrid::DEFAULT_COLUMNS, 'A');
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

// ============================================================================
// Sixels: what a graphics cell drew
// ============================================================================
//
// Expected characters are written as UTF-8 byte escapes rather than as
// literals, because source here stays 7-bit ASCII: a non-ASCII byte in a test
// name or literal breaks CTest and Catch2 on Windows.

namespace {

// A mosaic cell holding one graphics code.
TeletextCell mosaic_cell(uint8_t character,
                         TeletextCellCharset charset =
                             TeletextCellCharset::ContiguousGraphics) {
    TeletextCell cell;
    cell.character = character;
    cell.charset = charset;
    return cell;
}

std::string displayed(const TeletextCell& cell) {
    return teletext_text(grid_with(cell), TeletextRegion::whole_screen(),
                         TeletextLinearisation::Rows,
                         TeletextCharacters::Displayed);
}

} // namespace

TEST_CASE("A mosaic copies as the block sextant it drew", "[teletext-text]") {
    // 0x21: the graphics marker plus bit 0, so only the top-left block is lit.
    // That is the first sextant, U+1FB00.
    CHECK(displayed(mosaic_cell(0x21)) == "\xF0\x9F\xAC\x80");

    // 0x7E: every block but the top-left. The last sextant, U+1FB3B -- worth
    // pinning because it is the far end of the range the arithmetic walks.
    CHECK(displayed(mosaic_cell(0x7E)) == "\xF0\x9F\xAC\xBB");
}

TEST_CASE("The four patterns Unicode already had are not sextants",
          "[teletext-text]") {
    // Blank, both half blocks and the full block predate the sextant range,
    // which is why it holds sixty characters and not sixty-four. Getting these
    // wrong would shift every codepoint above them.
    CHECK(displayed(mosaic_cell(0x20)) == "");          // blank: a space, stripped
    CHECK(displayed(mosaic_cell(0x35)) == "\xE2\x96\x8C");  // U+258C LEFT HALF
    CHECK(displayed(mosaic_cell(0x6A)) == "\xE2\x96\x90");  // U+2590 RIGHT HALF
    CHECK(displayed(mosaic_cell(0x7F)) == "\xE2\x96\x88");  // U+2588 FULL BLOCK
}

TEST_CASE("Every mosaic maps to a distinct character", "[teletext-text]") {
    // Sixty-four patterns, sixty-four different characters: the mapping is a
    // bijection, so no two mosaics can copy as the same thing.
    std::set<std::string> seen;
    for (uint8_t code = 0x20; code <= 0x3F; ++code) {
        seen.insert(displayed(mosaic_cell(code)));
    }
    for (uint16_t code = 0x60; code <= 0x7F; ++code) {
        seen.insert(displayed(mosaic_cell(static_cast<uint8_t>(code))));
    }
    CHECK(seen.size() == 64);
}

TEST_CASE("Separated graphics copy as the same sextants", "[teletext-text]") {
    // Separation is how the chip draws a mosaic, not a different character --
    // as italics are not different letters.
    for (uint8_t code = 0x20; code <= 0x3F; ++code) {
        CHECK(displayed(mosaic_cell(code, TeletextCellCharset::SeparatedGraphics))
              == displayed(mosaic_cell(code)));
    }
}

TEST_CASE("Letters show through in graphics mode", "[teletext-text]") {
    // 0x40-0x5F have the graphics marker bit clear, so the chip draws them
    // from the font even in graphics mode. They are letters and copy as
    // letters, in either repertoire.
    const TeletextCell letter = mosaic_cell(0x41);  // 'A'

    CHECK(teletext_text(grid_with(letter)) == "A");
    CHECK(displayed(letter) == "A");
}

TEST_CASE("A held-graphics cell copies as the mosaic it showed",
          "[teletext-text]") {
    // Under hold graphics a control code draws the last mosaic instead of a
    // space. The capture resolved that -- the cell carries the held character
    // and the graphics charset, with is_control_code still set -- so the
    // reading follows what was on the screen.
    TeletextCell held = mosaic_cell(0x21);
    held.is_control_code = true;

    CHECK(displayed(held) == "\xF0\x9F\xAC\x80");

    // Under Codes it is what its byte says it is: a control code, and no text.
    CHECK(teletext_text(grid_with(held)) == "");
}
