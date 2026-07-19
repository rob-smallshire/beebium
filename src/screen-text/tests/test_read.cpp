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

// Reading text out of images: the matching behaviour the library exists for.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Canvas.hpp"
#include "screentext/ScreenText.hpp"

using namespace screentext;
using screentext::testing::Canvas;
using screentext::testing::whole_image_band;

namespace {

const GlyphSet& acorn()
{
    return builtin_glyph_set("acorn-mos-1.20");
}

std::vector<GlyphSet> acorn_only()
{
    return {acorn()};
}

// The cycle of printable characters a testcard walks, character 96 of the
// Acorn set being a pound sign rather than a grave accent.
char32_t codepoint_for_testcard(std::size_t index)
{
    const std::size_t character = 32 + index % 95;
    return character == 96 ? 0x00A3U : static_cast<char32_t>(character);
}

// Join expected rows the way runs are reported: blanks trimmed from each end,
// wholly blank rows dropped, the rest joined with newlines.
std::string trimmed_text(const std::vector<std::u32string>& rows)
{
    std::string text;
    for (const std::u32string& row : rows) {
        const std::size_t first = row.find_first_not_of(U' ');
        if (first == std::u32string::npos) {
            continue;
        }
        const std::size_t last = row.find_last_not_of(U' ');
        if (!text.empty()) {
            text.push_back('\n');
        }
        for (std::size_t index = first; index <= last; ++index) {
            const char32_t codepoint = row[index];
            if (codepoint < 0x80U) {
                text.push_back(static_cast<char>(codepoint));
            } else {
                text.push_back(static_cast<char>(0xC0U | (codepoint >> 6)));
                text.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
            }
        }
    }
    return text;
}

} // namespace

TEST_CASE("Plain text on the character grid is read back exactly")
{
    Canvas canvas(8 * 12, 8);
    canvas.stamp_text(0, 0, "HELLO", acorn());

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "HELLO");
    CHECK(result.unmatched_cells == 0);
    CHECK(result.total_cells == 12);
    CHECK(result.text() == "HELLO");
}

TEST_CASE("A run reports the rectangle it occupied")
{
    Canvas canvas(8 * 10, 8);
    canvas.stamp_text(8 * 2, 0, "ABC", acorn());

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    const Rect& bounds = result.runs[0].bounds;
    CHECK(bounds.x == 16);
    CHECK(bounds.y == 0);
    CHECK(bounds.width == 24);
    CHECK(bounds.height == 8);
}

TEST_CASE("Leading and trailing blanks are trimmed but interior spacing is kept")
{
    Canvas canvas(8 * 12, 8);
    canvas.stamp_text(8 * 3, 0, "A  B", acorn());

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "A  B");
    CHECK(result.runs[0].bounds.x == 24);
}

TEST_CASE("Inverse text is matched and flagged")
{
    Canvas canvas(8 * 4, 8);
    canvas.stamp_inverted(0, 0, Canvas::glyph_for(acorn(), U'H'));
    canvas.stamp_inverted(8, 0, Canvas::glyph_for(acorn(), U'I'));

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "HI");
    REQUIRE(result.runs[0].cells.size() == 2);
    CHECK(result.runs[0].cells[0].inverted);
    CHECK(result.runs[0].cells[1].inverted);
    CHECK(result.unmatched_cells == 0);
}

TEST_CASE("Inverse matching can be turned off, leaving inverse text unread")
{
    Canvas canvas(8 * 2, 8);
    canvas.stamp_inverted(0, 0, Canvas::glyph_for(acorn(), U'H'));

    Options options;
    options.match_inverted = false;

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(),
                               options);

    CHECK(result.unmatched_cells == 1);
    REQUIRE(result.runs.size() == 1);
    CHECK_FALSE(result.runs[0].cells[0].matched());
}

TEST_CASE("An upright match is preferred to an inverted one")
{
    // A bitmap can be one glyph upright and another inverted. The upright
    // match wins, so the answer is fixed by the rule rather than by iteration
    // order.
    GlyphSet ambiguous;
    ambiguous.name = "ambiguous";
    const Bitmap upright
        = Bitmap::from_rows({0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0});
    ambiguous.glyphs.push_back(Glyph(U'U', upright));
    ambiguous.glyphs.push_back(Glyph(U'I', upright.inverted()));

    Canvas canvas(8, 8);
    canvas.stamp(0, 0, upright);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               {ambiguous});

    REQUIRE(result.runs.size() == 1);
    REQUIRE(result.runs[0].cells.size() == 1);
    CHECK(result.runs[0].cells[0].codepoint == U'U');
    CHECK_FALSE(result.runs[0].cells[0].inverted);
}

TEST_CASE("An inverse space reads as a space, not as a control code")
{
    // A solid cell is what an inverse-video space looks like, and inverse
    // video is most of what makes solid cells. The MOS font holds a solid
    // block at character 127, but VDU 127 deletes rather than prints, so it
    // is excluded from the built-in set and this reads as a space.
    Canvas canvas(8 * 3, 8);
    canvas.stamp_inverted(0, 0, Canvas::glyph_for(acorn(), U'A'));
    canvas.stamp_inverted(8, 0, Canvas::glyph_for(acorn(), U' '));
    canvas.stamp_inverted(16, 0, Canvas::glyph_for(acorn(), U'B'));

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "A B");
    CHECK(result.unmatched_cells == 0);

    // Every cell here is reverse video, so there is nothing for any of them
    // to be reverse of: dark letters on a light ground is exactly what this
    // is, and that is how it reads. Inverse video is a relation between a
    // cell and the screen around it, not a property of a cell, so a screen
    // made entirely of it has none.
    for (const Cell& cell : result.runs[0].cells) {
        CHECK_FALSE(cell.inverted);
    }
}

TEST_CASE("A cell that matches nothing is unmatched, never guessed")
{
    Canvas canvas(8, 8);
    // A shape that is not in the font: a single pixel in the corner.
    canvas.set_pixel(0, 0, 1);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    CHECK(result.total_cells == 1);
    CHECK(result.unmatched_cells == 1);
    REQUIRE(result.runs.size() == 1);
    REQUIRE(result.runs[0].cells.size() == 1);
    CHECK_FALSE(result.runs[0].cells[0].matched());
    CHECK(result.runs[0].cells[0].codepoint == 0);
    CHECK(result.runs[0].cells[0].glyph_set.empty());
}

TEST_CASE("Matching is exact: one wrong pixel means unmatched")
{
    // The guard against matching ever becoming approximate. An 'H' with a
    // single pixel added is not an 'H', and is not anything else either.
    Canvas canvas(8, 8);
    canvas.stamp(0, 0, Canvas::glyph_for(acorn(), U'H'));
    canvas.set_pixel(0, 7, 1);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    CHECK(result.unmatched_cells == 1);
    REQUIRE(result.runs.size() == 1);
    CHECK_FALSE(result.runs[0].cells[0].matched());
}

TEST_CASE("An unmatched cell holds its column but is distinguishable from a blank")
{
    // The mistake B-Em's textsave.c makes: an unmatched cell there becomes a
    // space, so "I could not read this" and "this was blank" are the same
    // output. Here the text keeps the column, and the cell says which it was.
    Canvas canvas(8 * 3, 8);
    canvas.stamp_text(0, 0, "A", acorn());
    canvas.set_pixel(8, 0, 1); // middle cell: unreadable
    canvas.stamp_text(16, 0, "B", acorn());

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    const Run& run = result.runs[0];
    CHECK(run.text == "A B");
    REQUIRE(run.cells.size() == 3);
    CHECK(run.cells[0].matched());
    CHECK_FALSE(run.cells[1].matched());
    CHECK(run.cells[2].matched());
    CHECK(run.unmatched_cells() == 1);

    // A genuinely blank middle cell gives the same text but no unmatched cell.
    Canvas blank(8 * 3, 8);
    blank.stamp_text(0, 0, "A B", acorn());
    const Result other = read(blank.image(),
                              {whole_image_band(blank.image())},
                              acorn_only());
    REQUIRE(other.runs.size() == 1);
    CHECK(other.runs[0].text == "A B");
    CHECK(other.runs[0].unmatched_cells() == 0);
}

TEST_CASE("A row of nothing but unmatched cells is still reported")
{
    Canvas canvas(8 * 2, 8);
    canvas.set_pixel(0, 0, 1);
    canvas.set_pixel(8, 0, 1);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "  ");
    CHECK(result.runs[0].unmatched_cells() == 2);
}

TEST_CASE("A blank image yields no runs but still counts its cells")
{
    Canvas canvas(8 * 4, 8 * 2);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    CHECK(result.runs.empty());
    CHECK(result.total_cells == 8);
    CHECK(result.unmatched_cells == 0);
    CHECK(result.text().empty());
}

TEST_CASE("An empty image yields an empty result")
{
    const Image image;
    const Result result = read(image, {}, acorn_only());

    CHECK(result.runs.empty());
    CHECK(result.total_cells == 0);
    CHECK(result.unmatched_cells == 0);
}

TEST_CASE("Several rows become several runs, in reading order")
{
    Canvas canvas(8 * 8, 8 * 3);
    canvas.stamp_text(0, 0, "TOP", acorn());
    // Middle row deliberately blank.
    canvas.stamp_text(0, 16, "END", acorn());

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 2);
    CHECK(result.runs[0].text == "TOP");
    CHECK(result.runs[0].bounds.y == 0);
    CHECK(result.runs[1].text == "END");
    CHECK(result.runs[1].bounds.y == 16);
    CHECK(result.text() == "TOP\nEND");
}

TEST_CASE("A selection clips a run at cell boundaries")
{
    Canvas canvas(8 * 8, 8);
    canvas.stamp_text(0, 0, "ABCDEFGH", acorn());

    Options options;
    options.selection = Rect{16, 0, 24, 8}; // cells 2, 3 and 4

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(),
                               options);

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "CDE");
    CHECK(result.total_cells == 3);
}

TEST_CASE("A cell only partly inside the selection is not read")
{
    Canvas canvas(8 * 4, 8);
    canvas.stamp_text(0, 0, "ABCD", acorn());

    Options options;
    options.selection = Rect{4, 0, 20, 8}; // straddles cells 0 and 3

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(),
                               options);

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "BC");
    CHECK(result.total_cells == 2);
}

TEST_CASE("A selection outside the image reads nothing")
{
    Canvas canvas(8 * 2, 8);
    canvas.stamp_text(0, 0, "AB", acorn());

    Options options;
    options.selection = Rect{800, 800, 8, 8};

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(),
                               options);

    CHECK(result.runs.empty());
    CHECK(result.total_cells == 0);
}

TEST_CASE("Bands may have different geometry, and are read top to bottom")
{
    // The split-screen case: a band of 8x8 cells above a band of 8x16 cells.
    Canvas canvas(8 * 6, 8 + 16);
    canvas.stamp_text(0, 0, "TOP", acorn());

    // In the lower band each cell is sixteen rows tall, so a glyph occupying
    // the top half of a cell and blank below is a distinct bitmap. Build the
    // glyph set for that geometry explicitly.
    GlyphSet tall;
    tall.name = "tall";
    for (const Glyph& glyph : acorn().glyphs) {
        Bitmap bitmap(8, 16);
        for (std::size_t y = 0; y < 8; ++y) {
            for (std::size_t x = 0; x < 8; ++x) {
                bitmap.set_pixel(x, y, glyph.bitmap.pixel(x, y));
            }
        }
        tall.glyphs.push_back(Glyph(glyph.codepoint, bitmap));
    }
    canvas.stamp_text(0, 8, "LOW", acorn());

    Band upper;
    upper.top = 0;
    upper.bottom = 8;

    Band lower;
    lower.top = 8;
    lower.bottom = 24;
    lower.origin_y = 8;
    lower.cell_height = 16;

    const Result result
        = read(canvas.image(), {upper, lower}, {acorn(), tall});

    REQUIRE(result.runs.size() == 2);
    CHECK(result.runs[0].text == "TOP");
    CHECK(result.runs[0].bounds.height == 8);
    CHECK(result.runs[1].text == "LOW");
    CHECK(result.runs[1].bounds.height == 16);
}

TEST_CASE("A cell straddling a band boundary is not read")
{
    Canvas canvas(8 * 2, 16);
    canvas.stamp_text(0, 4, "AB", acorn()); // sits across the boundary

    Band band;
    band.top = 0;
    band.bottom = 12; // not a whole number of cells
    band.cell_height = 8;

    const Result result = read(canvas.image(), {band}, acorn_only());

    // Only the first cell row fits wholly inside the band, and the text is
    // not aligned to it, so nothing matches -- but nothing is guessed either.
    CHECK(result.total_cells == 2);
    CHECK(result.unmatched_cells == 2);
}

TEST_CASE("The grid origin shifts where cells are taken from")
{
    Canvas canvas(8 * 4, 8);
    canvas.stamp_text(4, 0, "AB", acorn()); // off the grid by four pixels

    Band aligned = whole_image_band(canvas.image());
    const Result missed = read(canvas.image(), {aligned}, acorn_only());
    CHECK(missed.unmatched_cells > 0);

    Band shifted = aligned;
    shifted.origin_x = 4;
    const Result found = read(canvas.image(), {shifted}, acorn_only());
    REQUIRE(found.runs.size() == 1);
    CHECK(found.runs[0].text == "AB");
}

TEST_CASE("Neither colour need be zero, and neither need be the brighter")
{
    // Nothing assumes background is zero, or dark, or anything else. Here
    // the glyph is drawn darker than the ground it sits on.
    Canvas canvas(8, 8, 7);
    canvas.stamp(0, 0, Canvas::glyph_for(acorn(), U'Q'), 3);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "Q");
}

TEST_CASE("A supplied glyph set overrides a built-in character")
{
    // How a VDU 23 redefinition would arrive: the same bitmap, a different
    // meaning, supplied as a later set.
    GlyphSet redefined;
    redefined.name = "redefined";
    redefined.glyphs.push_back(
        Glyph(U'Z', Canvas::glyph_for(acorn(), U'A')));

    Canvas canvas(8, 8);
    canvas.stamp_text(0, 0, "A", acorn());

    const Result plain = read(canvas.image(),
                              {whole_image_band(canvas.image())},
                              acorn_only());
    REQUIRE(plain.runs.size() == 1);
    CHECK(plain.runs[0].text == "A");
    CHECK(plain.runs[0].cells[0].glyph_set == "acorn-mos-1.20");

    const Result overridden = read(canvas.image(),
                                   {whole_image_band(canvas.image())},
                                   {acorn(), redefined});
    REQUIRE(overridden.runs.size() == 1);
    CHECK(overridden.runs[0].text == "Z");
    CHECK(overridden.runs[0].cells[0].glyph_set == "redefined");
}

TEST_CASE("A supplied glyph set can add a character the built-in set lacks")
{
    GlyphSet extra;
    extra.name = "extra";
    extra.glyphs.push_back(Glyph::from_rows(
        0x2500U, {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00}));

    Canvas canvas(8, 8);
    canvas.stamp(0, 0, extra.glyphs[0].bitmap);

    const Result without = read(canvas.image(),
                                {whole_image_band(canvas.image())},
                                acorn_only());
    CHECK(without.unmatched_cells == 1);

    const Result with = read(canvas.image(),
                             {whole_image_band(canvas.image())},
                             {acorn(), extra});
    REQUIRE(with.runs.size() == 1);
    CHECK(with.runs[0].cells[0].codepoint == 0x2500U);
    CHECK(with.unmatched_cells == 0);
}

TEST_CASE("Reading with no glyph sets matches nothing")
{
    Canvas canvas(8, 8);
    canvas.stamp_text(0, 0, "A", acorn());

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               {});

    CHECK(result.total_cells == 1);
    CHECK(result.unmatched_cells == 1);
}

TEST_CASE("Text is UTF-8, so a pound sign survives the round trip")
{
    // Character 96 in the Acorn set is a pound sign, not a grave accent.
    Canvas canvas(8 * 3, 8);
    canvas.stamp(0, 0, Canvas::glyph_for(acorn(), 0x00A3U));
    canvas.stamp_text(8, 0, "5", acorn(), 8);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "\xC2\xA3" "5");
    CHECK(result.runs[0].cells[0].codepoint == 0x00A3U);
}

TEST_CASE("Offset search is accepted but changes nothing yet")
{
    Canvas canvas(8 * 2, 8);
    canvas.stamp_text(4, 0, "A", acorn()); // off the grid

    Options options;
    options.search = Search::IncludeOffset;

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(),
                               options);

    // Aligned reading is all that happens, so the off-grid glyph is not
    // found, and no cell claims to have been matched at an offset.
    CHECK(result.unmatched_cells > 0);
    for (const Run& run : result.runs) {
        for (const Cell& cell : run.cells) {
            CHECK_FALSE(cell.offset);
        }
    }
}

TEST_CASE("Reading the same input twice gives the same answer")
{
    Canvas canvas(8 * 6, 8 * 2);
    canvas.stamp_text(0, 0, "DETERM", acorn());
    canvas.set_pixel(3, 9, 1);

    const Result first = read(canvas.image(),
                              {whole_image_band(canvas.image())},
                              acorn_only());
    const Result second = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    CHECK(first.text() == second.text());
    CHECK(first.unmatched_cells == second.unmatched_cells);
}

TEST_CASE("Malformed input is rejected rather than read")
{
    Image inconsistent;
    inconsistent.width = 8;
    inconsistent.height = 8;
    inconsistent.pixels.assign(4, 0); // too few

    CHECK_THROWS_AS(read(inconsistent, {}, acorn_only()), std::invalid_argument);

    Canvas canvas(8, 8);
    Band degenerate = whole_image_band(canvas.image());
    degenerate.cell_width = 0;

    CHECK_THROWS_AS(read(canvas.image(), {degenerate}, acorn_only()),
                    std::invalid_argument);
}

// Cell size and grid pitch are different things. A display may leave a gap
// between character rows: MODE 3 and MODE 6 put an 8-scanline glyph on a
// 10-scanline pitch, and the two spare scanlines are blanked to black
// whatever the palette says. Sampling them would be wrong, and with a
// non-black background it would stop every cell matching.

TEST_CASE("A row pitch larger than the cell height skips the gap between rows")
{
    // Three rows of 8-pixel glyphs on a 10-pixel pitch, as MODE 6 has.
    Canvas canvas(8 * 3, 30);
    canvas.stamp_text(0, 0, "ABC", acorn());
    canvas.stamp_text(0, 10, "DEF", acorn());
    canvas.stamp_text(0, 20, "GHI", acorn());

    Band band;
    band.top = 0;
    band.bottom = 30;
    band.cell_height = 8;
    band.row_pitch = 10;

    const Result result = read(canvas.image(), {band}, acorn_only());

    REQUIRE(result.runs.size() == 3);
    CHECK(result.runs[0].text == "ABC");
    CHECK(result.runs[1].text == "DEF");
    CHECK(result.runs[2].text == "GHI");
    CHECK(result.unmatched_cells == 0);
}

TEST_CASE("Pixels in the gap between rows are never read")
{
    // The blanked scanlines hold a value that is neither the background nor
    // any part of a glyph. Reading them would corrupt every cell; skipping
    // them leaves the text intact.
    Canvas canvas(8 * 3, 20, 4);
    canvas.stamp_text(0, 0, "ABC", acorn(), 8, 1);
    canvas.stamp_text(0, 10, "DEF", acorn(), 8, 1);

    // The gap scanlines are blanked to a third value, as the BBC blanks them
    // to black while the cell background is some other colour.
    for (std::size_t y : {8u, 9u, 18u, 19u}) {
        for (std::size_t x = 0; x < canvas.image().width; ++x) {
            canvas.set_pixel(x, y, 0);
        }
    }

    Band band;
    band.top = 0;
    band.bottom = 20;
    band.cell_height = 8;
    band.row_pitch = 10;

    const Result result = read(canvas.image(), {band}, acorn_only());

    REQUIRE(result.runs.size() == 2);
    CHECK(result.runs[0].text == "ABC");
    CHECK(result.runs[1].text == "DEF");
    CHECK(result.unmatched_cells == 0);
}

TEST_CASE("Sampling the gap as though it were part of the cell matches nothing")
{
    // The failure the pitch exists to avoid, stated as a test so that the
    // distinction cannot quietly collapse again.
    Canvas canvas(8, 20, 4);
    canvas.stamp_text(0, 0, "A", acorn(), 8, 1);
    for (std::size_t y : {8u, 9u}) {
        for (std::size_t x = 0; x < 8; ++x) {
            canvas.set_pixel(x, y, 0);
        }
    }

    Band naive;
    naive.top = 0;
    naive.bottom = 20;
    naive.cell_height = 10; // wrong: the gap is not part of the glyph

    const Result result = read(canvas.image(), {naive}, acorn_only());
    CHECK(result.unmatched_cells > 0);
}

TEST_CASE("A column pitch larger than the cell width skips gaps between columns")
{
    // No BBC mode does this, but the library is not a BBC library and the
    // two axes are treated alike.
    Canvas canvas(30, 8);
    canvas.stamp_text(0, 0, "A", acorn());
    canvas.stamp(10, 0, Canvas::glyph_for(acorn(), U'B'));
    canvas.stamp(20, 0, Canvas::glyph_for(acorn(), U'C'));

    Band band;
    band.top = 0;
    band.bottom = 8;
    band.cell_width = 8;
    band.column_pitch = 10;

    const Result result = read(canvas.image(), {band}, acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "ABC");
}

TEST_CASE("The pitch defaults to the cell size")
{
    Canvas canvas(8 * 3, 8);
    canvas.stamp_text(0, 0, "ABC", acorn());

    Band band = whole_image_band(canvas.image());
    CHECK(band.effective_row_pitch() == band.cell_height);
    CHECK(band.effective_column_pitch() == band.cell_width);

    const Result result = read(canvas.image(), {band}, acorn_only());
    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "ABC");
}

TEST_CASE("A pitch smaller than the cell is rejected, not read as overlap")
{
    Canvas canvas(8 * 3, 8);
    Band band = whole_image_band(canvas.image());
    band.row_pitch = 4;

    CHECK_THROWS_AS(read(canvas.image(), {band}, acorn_only()),
                    std::invalid_argument);
}

// Colour is worked out from the image rather than declared.
//
// A character cell drawn by the VDU drivers holds exactly two pixel values,
// the glyph's colour and its background, and the two ways of assigning those
// values are precisely the upright and inverse interpretations. So which is
// which need not be known in order to match -- and a caller often cannot say
// any more easily than this can work it out.

TEST_CASE("Text is read without any background being declared")
{
    Canvas canvas(8 * 3, 8, 5);
    canvas.stamp_text(0, 0, "ABC", acorn(), 8, 2);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "ABC");
    CHECK(result.unmatched_cells == 0);
}

TEST_CASE("Cells with different background colours are all read")
{
    // Each cell is a glyph on its own background. Nothing links one cell's
    // colours to another's, so nothing global can describe them.
    Canvas canvas(8 * 3, 8, 0);
    const std::uint8_t backgrounds[] = {1, 9, 4};
    const std::uint8_t foregrounds[] = {7, 2, 6};
    const char* const letters = "XYZ";

    for (std::size_t index = 0; index < 3; ++index) {
        canvas.fill(index * 8, 0, 8, 8, backgrounds[index]);
        canvas.stamp(index * 8, 0,
                     Canvas::glyph_for(acorn(), static_cast<char32_t>(
                                                    letters[index])),
                     foregrounds[index]);
    }

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "XYZ");
    CHECK(result.unmatched_cells == 0);
}

TEST_CASE("Cells with different foreground colours are all read")
{
    Canvas canvas(8 * 3, 8, 0);
    canvas.stamp(0, 0, Canvas::glyph_for(acorn(), U'R'), 1);
    canvas.stamp(8, 0, Canvas::glyph_for(acorn(), U'G'), 2);
    canvas.stamp(16, 0, Canvas::glyph_for(acorn(), U'B'), 4);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "RGB");
    CHECK(result.unmatched_cells == 0);
}

TEST_CASE("A cell of more than two colours is unmatched, not guessed at")
{
    // One colour per glyph is what the VDU drivers produce. A cell holding
    // three colours is something else -- text drawn over graphics, most
    // likely -- and reporting it unread is the honest answer.
    Canvas canvas(8, 8, 0);
    canvas.stamp(0, 0, Canvas::glyph_for(acorn(), U'A'), 1);
    canvas.set_pixel(7, 7, 9); // a third colour, from something else

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    CHECK(result.unmatched_cells == 1);
}

TEST_CASE("Two colours neither of which is the screen background still match")
{
    Canvas canvas(8 * 2, 8, 0);
    canvas.fill(0, 0, 8, 8, 3);
    canvas.stamp(0, 0, Canvas::glyph_for(acorn(), U'Q'), 6);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "Q");
}

TEST_CASE("Inverse video is judged against the background of the screen")
{
    // Which cell is "inverse" is not a property of the cell -- both readings
    // of two colours are glyphs. It is a property of how the cell sits
    // against the rest of the screen, so that is what decides it.
    Canvas canvas(8 * 4, 8, 0);
    canvas.stamp_text(0, 0, "AB", acorn(), 8, 7);
    canvas.fill(16, 0, 8, 8, 7);
    canvas.stamp(16, 0, Canvas::glyph_for(acorn(), U'C'), 0);
    canvas.stamp(24, 0, Canvas::glyph_for(acorn(), U'D'), 7);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "ABCD");
    REQUIRE(result.runs[0].cells.size() == 4);
    CHECK_FALSE(result.runs[0].cells[0].inverted);
    CHECK_FALSE(result.runs[0].cells[1].inverted);
    CHECK(result.runs[0].cells[2].inverted);
    CHECK_FALSE(result.runs[0].cells[3].inverted);
}

TEST_CASE("A solid cell of a colour other than the background is an inverse space")
{
    Canvas canvas(8 * 3, 8, 0);
    canvas.stamp_text(0, 0, "A", acorn(), 8, 7);
    canvas.fill(8, 0, 8, 8, 7); // solid: an inverse space
    canvas.stamp(16, 0, Canvas::glyph_for(acorn(), U'B'), 7);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "A B");
    CHECK(result.unmatched_cells == 0);
    CHECK(result.runs[0].cells[1].codepoint == U' ');
    CHECK(result.runs[0].cells[1].inverted);
}


namespace {

// A deterministic pseudo-random sequence, written out rather than taken from
// the standard library so that the same test data is generated on every
// platform and every build. std::rand and std::mt19937 differ in the first
// respect and the second respectively.
class Sequence {
public:
    explicit Sequence(std::uint32_t seed) : state_(seed) {}

    std::uint8_t next()
    {
        state_ = state_ * 1664525U + 1013904223U;
        return static_cast<std::uint8_t>(state_ >> 24);
    }

private:
    std::uint32_t state_;
};

} // namespace

TEST_CASE("A full screen of text in a different colour pair per cell is read")
{
    // The testcard idea taken to colour: every cell holds its own foreground
    // and its own background, unequal, drawn from the whole range of byte
    // values rather than any machine's palette. Nothing links one cell's
    // colours to the next's, and no colour is declared anywhere.
    const std::size_t columns = 40;
    const std::size_t rows = 25;

    Canvas canvas(columns * 8, rows * 8);
    Sequence colours(20260719U);

    std::vector<std::u32string> expected_rows;
    for (std::size_t row = 0; row < rows; ++row) {
        std::u32string expected_row;
        for (std::size_t column = 0; column < columns; ++column) {
            const std::uint8_t background = colours.next();
            std::uint8_t foreground = colours.next();
            if (foreground == background) {
                foreground = static_cast<std::uint8_t>(background ^ 0xFFU);
            }

            const std::size_t index = row * columns + column;
            const char32_t codepoint = codepoint_for_testcard(index);

            canvas.fill(column * 8, row * 8, 8, 8, background);
            canvas.stamp(column * 8, row * 8,
                         Canvas::glyph_for(acorn(), codepoint), foreground);
            expected_row.push_back(codepoint);
        }
        expected_rows.push_back(expected_row);
    }

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    CHECK(result.total_cells == columns * rows);
    CHECK(result.unmatched_cells == 0);
    CHECK(result.text() == trimmed_text(expected_rows));
}

TEST_CASE("A full screen of colour pairs is read the same way twice")
{
    // Every cell here has a different background, so no value is the screen's
    // background in any meaningful sense and the vote that picks one is
    // deciding between near-equals. The text must not depend on how that
    // comes out, which is the point of trying both readings of every cell.
    const std::size_t columns = 20;
    const std::size_t rows = 16;

    Canvas canvas(columns * 8, rows * 8);
    Sequence colours(99991U);
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            const std::uint8_t background = colours.next();
            std::uint8_t foreground = colours.next();
            if (foreground == background) {
                foreground = static_cast<std::uint8_t>(background ^ 0x5AU);
            }
            canvas.fill(column * 8, row * 8, 8, 8, background);
            canvas.stamp(column * 8, row * 8,
                         Canvas::glyph_for(acorn(),
                                           codepoint_for_testcard(
                                               row * columns + column)),
                         foreground);
        }
    }

    const Result first = read(canvas.image(),
                              {whole_image_band(canvas.image())},
                              acorn_only());
    const Result second = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    CHECK(first.unmatched_cells == 0);
    CHECK(first.text() == second.text());
}
