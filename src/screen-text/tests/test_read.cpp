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
    // The MOS font has a solid block at &7F, which is exactly the complement
    // of the space glyph. Both match a filled cell; the upright one wins, and
    // the choice is deterministic rather than a matter of iteration order.
    Canvas canvas(8, 8, 0);
    for (std::size_t y = 0; y < 8; ++y) {
        for (std::size_t x = 0; x < 8; ++x) {
            canvas.set_pixel(x, y, 1);
        }
    }

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    REQUIRE(result.runs[0].cells.size() == 1);
    CHECK(result.runs[0].cells[0].codepoint == 0x7F);
    CHECK_FALSE(result.runs[0].cells[0].inverted);
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

TEST_CASE("A non-zero background makes the brighter pixels the glyph")
{
    // The caller has already reduced its image to one byte per pixel; which
    // value counts as background is its decision, not the library's.
    Canvas canvas(8, 8, 7);
    canvas.stamp(0, 0, Canvas::glyph_for(acorn(), U'Q'), 3);

    Band band = whole_image_band(canvas.image());
    band.background = 7;

    const Result result = read(canvas.image(), {band}, acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "Q");
}

TEST_CASE("Any pixel value other than the background is foreground")
{
    // Colour is reduced away before the library sees it: a glyph drawn in
    // several colours is still the same glyph.
    Canvas canvas(8, 8);
    const Bitmap& glyph = Canvas::glyph_for(acorn(), U'W');
    std::uint8_t colour = 1;
    for (std::size_t y = 0; y < 8; ++y) {
        for (std::size_t x = 0; x < 8; ++x) {
            if (glyph.pixel(x, y)) {
                canvas.set_pixel(x, y, colour);
                colour = static_cast<std::uint8_t>(colour % 15 + 1);
            }
        }
    }

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "W");
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
