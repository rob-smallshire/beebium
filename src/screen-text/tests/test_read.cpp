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

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "Canvas.hpp"
#include "GlyphFile.hpp"
#include "screentext/ScreenText.hpp"

using namespace screentext;
using screentext::testing::Canvas;
using screentext::testing::load_glyph_file;
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

TEST_CASE("A glyph drawn light on dark and dark on light both read")
{
    // Neither is the reversal of the other; they are two colour
    // arrangements, and each cell says which colour its glyph was drawn in.
    Canvas canvas(8 * 2, 8, 0);
    canvas.stamp(0, 0, Canvas::glyph_for(acorn(), U'H'), 7);
    canvas.fill(8, 0, 8, 8, 7);
    canvas.stamp(8, 0, Canvas::glyph_for(acorn(), U'I'), 0);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "HI");
    REQUIRE(result.runs[0].cells.size() == 2);
    CHECK(result.unmatched_cells == 0);

    CHECK(result.runs[0].cells[0].foreground == 7);
    CHECK(result.runs[0].cells[0].background == 0);
    CHECK(result.runs[0].cells[1].foreground == 0);
    CHECK(result.runs[0].cells[1].background == 7);
}

TEST_CASE("A blank cell reports its one colour as both")
{
    Canvas canvas(8 * 3, 8, 0);
    canvas.stamp(0, 0, Canvas::glyph_for(acorn(), U'A'), 7);
    canvas.fill(8, 0, 8, 8, 4); // blank, in a colour of its own
    canvas.stamp(16, 0, Canvas::glyph_for(acorn(), U'B'), 7);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "A B");
    CHECK(result.unmatched_cells == 0);

    // There is no glyph in it, so there is no colour a glyph was drawn in.
    const Cell& blank = result.runs[0].cells[1];
    CHECK(blank.codepoint == U' ');
    CHECK(blank.foreground == 4);
    CHECK(blank.background == 4);
}

TEST_CASE("When both readings are glyphs, the larger background wins")
{
    // Needs a glyph set holding some glyph's complement, which no built-in
    // set does. The rule is that the colour covering more of the cell is the
    // background, so the answer is fixed rather than left to iteration order.
    GlyphSet ambiguous;
    ambiguous.name = "ambiguous";
    const Bitmap sparse
        = Bitmap::from_rows({0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    ambiguous.glyphs.push_back(Glyph(U'S', sparse));
    ambiguous.glyphs.push_back(Glyph(U'D', sparse.inverted()));

    Canvas canvas(8, 8, 0);
    canvas.stamp(0, 0, sparse, 7);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               {ambiguous});

    REQUIRE(result.runs.size() == 1);
    REQUIRE(result.runs[0].cells.size() == 1);

    // One pixel of 7 against sixty-three of 0: the 0 is the background, so
    // this is the sparse glyph rather than the dense one.
    CHECK(result.runs[0].cells[0].codepoint == U'S');
    CHECK(result.runs[0].cells[0].background == 0);
    CHECK(result.runs[0].cells[0].foreground == 7);
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

    // Dark letters on a light ground, which is all this is. Nothing here is
    // a reversal of anything; the cells simply report the colours they were
    // drawn from, and the light one is the ground throughout.
    for (const Cell& cell : result.runs[0].cells) {
        CHECK(cell.background == 1);
    }
    CHECK(result.runs[0].cells[0].foreground == 0);
    CHECK(result.runs[0].cells[2].foreground == 0);

    // The middle cell is blank -- a solid block of the ground colour -- so
    // there is no glyph in it to have a colour of its own.
    CHECK(result.runs[0].cells[1].foreground == 1);
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

namespace {

Options offset_options()
{
    Options options;
    options.search = Search::OffsetOnly;
    return options;
}

// A word of distinctive glyphs, stamped at an off-grid position.
Canvas off_grid_word(const std::string& word, std::size_t x, std::size_t y,
                     std::size_t width, std::size_t height)
{
    Canvas canvas(width, height);
    canvas.stamp_text(x, y, word, acorn());
    return canvas;
}

} // namespace

TEST_CASE("The offset search finds a word placed off the grid")
{
    // The inversion of the first increment's guard: what was asserted
    // unfindable is now found, at its exact off-grid position.
    Canvas canvas = off_grid_word("HELLO", 5, 3, 8 * 8, 16);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(), offset_options());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "HELLO");
    CHECK(result.runs[0].bounds.x == 5);
    CHECK(result.runs[0].bounds.y == 3);
}

TEST_CASE("Every cell of an off-grid run is flagged as an offset match")
{
    Canvas canvas = off_grid_word("ABC", 3, 1, 8 * 5, 16);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(), offset_options());

    REQUIRE(result.runs.size() == 1);
    for (const Cell& cell : result.runs[0].cells) {
        CHECK(cell.offset);
    }
}

TEST_CASE("The aligned pass never flags a cell as an offset match")
{
    Canvas canvas(8 * 3, 8);
    canvas.stamp_text(0, 0, "ABC", acorn());

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    REQUIRE(result.runs.size() == 1);
    for (const Cell& cell : result.runs[0].cells) {
        CHECK_FALSE(cell.offset);
    }
}

TEST_CASE("The two passes partition text: aligned reads grid, offset reads off")
{
    // Grid text at (0,0) and off-grid text a few pixels down and across, on
    // one image. Each pass sees its own and not the other's, so a caller
    // merging them concatenates rather than deduplicates.
    Canvas canvas(8 * 10, 8 * 4);
    canvas.stamp_text(0, 0, "GRID", acorn());       // on the grid
    canvas.stamp_text(3, 19, "AWAY", acorn());      // off it, y=19, x=3

    const Result aligned = read(canvas.image(),
                                {whole_image_band(canvas.image())},
                                acorn_only());
    const Result offset = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(), offset_options());

    CHECK(aligned.text().find("GRID") != std::string::npos);
    CHECK(aligned.text().find("AWAY") == std::string::npos);

    CHECK(offset.text().find("AWAY") != std::string::npos);
    CHECK(offset.text().find("GRID") == std::string::npos);
}

TEST_CASE("An off-grid run is rejoined across a space")
{
    // A space matches nothing, so two words come back as two candidate runs;
    // sharing a baseline, a colour and the lattice, they are rejoined with
    // the space written back.
    Canvas canvas(8 * 12, 16);
    canvas.stamp_text(5, 2, "AB", acorn());
    canvas.stamp_text(5 + 8 * 3, 2, "CD", acorn()); // one blank cell between

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(), offset_options());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "AB CD");
}

TEST_CASE("A lone distinctive glyph over graphics is found")
{
    // Waffle's scattered example letters are the evidence that one glyph is
    // enough. An 'A' is not a shape graphics make by accident.
    Canvas canvas(8 * 4, 8 * 3);
    // A little graphics clutter that is not glyph-shaped.
    canvas.fill(0, 0, 8 * 4, 4, 2);
    canvas.stamp(11, 9, Canvas::glyph_for(acorn(), U'A'), 1);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(), offset_options());

    bool found_a = false;
    for (const Run& run : result.runs) {
        if (run.text.find('A') != std::string::npos) {
            found_a = true;
        }
    }
    CHECK(found_a);
}

TEST_CASE("A lone simple glyph over graphics is not reported")
{
    // A full stop is HV-convex -- a blob, which graphics are made of -- so
    // alone, in registration with nothing, it is not credible and is left
    // out rather than guessed at.
    Canvas canvas(8 * 4, 8 * 3);
    canvas.stamp(11, 9, Canvas::glyph_for(acorn(), U'.'), 1);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(), offset_options());

    for (const Run& run : result.runs) {
        CHECK(run.text.find('.') == std::string::npos);
    }
}

TEST_CASE("Simple glyphs are read when a distinctive one vouches for the run")
{
    // "A." alone: the full stop rides along because the 'A' beside it, in
    // registration, makes the run credible.
    Canvas canvas(8 * 4, 8 * 2);
    canvas.stamp(3, 1, Canvas::glyph_for(acorn(), U'A'), 1);
    canvas.stamp(11, 1, Canvas::glyph_for(acorn(), U'.'), 1);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(), offset_options());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "A.");
}

TEST_CASE("Drop-shadowed text is read from the colour drawn last")
{
    // Text over its own shadow: three colours in the window. The shadow goes
    // down first, the text over it, so the text colour forms the glyph and
    // the shadow colour a crescent that matches nothing.
    Canvas canvas(8 * 6, 16, 0);
    const Bitmap& letter = Canvas::glyph_for(acorn(), U'H');
    canvas.stamp(6, 3, letter, 2); // shadow, one pixel down and right
    canvas.stamp(5, 2, letter, 1); // text, over it

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(), offset_options());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "H");
    CHECK(result.runs[0].cells[0].foreground == 1);
}

TEST_CASE("A run tolerates a one-pixel jog in its lattice")
{
    // Krazy Ape's LIVES= sits at gaps of 9, 8, 9, 8, 8. A hand-placed line
    // need not be a perfect lattice.
    Canvas canvas(8 * 8, 16);
    std::size_t pen = 3;
    const std::size_t gaps[] = {9, 8, 9, 8};
    const char* const word = "LIVES";
    canvas.stamp(pen, 2, Canvas::glyph_for(acorn(), U'L'), 1);
    for (std::size_t i = 0; i < 4; ++i) {
        pen += gaps[i];
        canvas.stamp(pen, 2,
                     Canvas::glyph_for(acorn(),
                                       static_cast<char32_t>(word[i + 1])),
                     1);
    }

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(), offset_options());

    REQUIRE(result.runs.size() == 1);
    CHECK(result.runs[0].text == "LIVES");
}

TEST_CASE("A pure-graphics screen yields no off-grid runs")
{
    // The negative guarantee: filled regions, edges and blobs form no
    // credible run, so nothing is invented from a picture.
    Canvas canvas(8 * 20, 8 * 20, 0);
    for (std::size_t i = 0; i < 40; ++i) {
        const std::uint8_t colour = static_cast<std::uint8_t>(1 + i % 3);
        // Rectangles and diagonals, the stuff of pictures.
        canvas.fill((i * 13) % 140, (i * 7) % 140, 10 + i % 30, 4 + i % 20,
                    colour);
        for (std::size_t d = 0; d < 30; ++d) {
            canvas.set_pixel((i * 11 + d) % 155, (i * 5 + d) % 155, colour);
        }
    }

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only(), offset_options());

    CHECK(result.runs.empty());
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

TEST_CASE("A cell standing out from its neighbours reports its own colours")
{
    // A caller wanting to know which cells stand out compares backgrounds
    // itself, with the whole picture in view. That is a question about a
    // screen, not about a cell, so a cell does not answer it.
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
    CHECK(result.runs[0].cells[0].background == 0);
    CHECK(result.runs[0].cells[1].background == 0);
    CHECK(result.runs[0].cells[2].background == 7);
    CHECK(result.runs[0].cells[3].background == 0);

    // Which is enough for a caller to spot the odd one out.
    const std::uint8_t common = result.runs[0].cells[0].background;
    std::size_t standing_out = 0;
    for (const Cell& cell : result.runs[0].cells) {
        if (cell.background != common) {
            ++standing_out;
        }
    }
    CHECK(standing_out == 1);
}

TEST_CASE("A solid cell reads as a space in the colour it is filled with")
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
    CHECK(result.runs[0].cells[1].background == 7);
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

// Characters a font does not distinguish.
//
// Real fonts collide. Of sixty-eight period BBC fonts surveyed, seventeen
// contain two or more characters drawn with exactly the same pixels: '0' with
// 'O', 'l' with '|', '(' with '[', even '5' with 'S'. Nothing can separate
// those from an image, because the difference is not in the image.
//
// Reporting one of them and saying nothing would be the same mistake as
// turning an unreadable cell into a space: a plausible answer where the truth
// is unknowable, with no way for a caller to tell.

namespace {

// Two glyphs, deliberately identical, as a colliding font has them.
GlyphSet colliding_set(std::initializer_list<char32_t> codepoints)
{
    GlyphSet set;
    set.name = "colliding";
    const Bitmap shared
        = Bitmap::from_rows({0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00});
    for (const char32_t codepoint : codepoints) {
        set.glyphs.push_back(Glyph(codepoint, shared));
    }
    return set;
}

} // namespace

TEST_CASE("A cell a font cannot pin down reports what else it might be")
{
    const GlyphSet set = colliding_set({U'O', U'0'});

    Canvas canvas(8, 8);
    canvas.stamp(0, 0, set.glyphs[0].bitmap);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               {set});

    REQUIRE(result.runs.size() == 1);
    REQUIRE(result.runs[0].cells.size() == 1);
    const Cell& cell = result.runs[0].cells[0];

    CHECK(cell.matched());
    CHECK(cell.ambiguous());
    CHECK(cell.codepoint == U'0'); // the lower of the two
    CHECK(cell.alternatives == std::vector<char32_t>{U'O'});

    // Counted apart from unmatched cells: one says what could not be read,
    // the other what could not be pinned down.
    CHECK(result.unmatched_cells == 0);
    CHECK(result.ambiguous_cells == 1);
    CHECK(result.runs[0].ambiguous_cells() == 1);
}

TEST_CASE("Three characters sharing one bitmap are all reported")
{
    // One of the surveyed fonts draws 'I', 'l' and '|' identically.
    const GlyphSet set = colliding_set({U'l', U'|', U'I'});

    Canvas canvas(8, 8);
    canvas.stamp(0, 0, set.glyphs[0].bitmap);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               {set});

    REQUIRE(result.runs.size() == 1);
    const Cell& cell = result.runs[0].cells[0];
    CHECK(cell.codepoint == U'I'); // U+0049, the lowest of the three
    CHECK(cell.alternatives == std::vector<char32_t>{U'l', U'|'});
}

TEST_CASE("Which character is reported does not depend on the order listed")
{
    // Chosen by value rather than by position, so that rearranging a font
    // file cannot change what a screen says.
    Canvas canvas(8, 8);
    canvas.stamp(0, 0, colliding_set({U'O'}).glyphs[0].bitmap);

    const Result forwards = read(canvas.image(),
                                 {whole_image_band(canvas.image())},
                                 {colliding_set({U'O', U'0'})});
    const Result backwards = read(canvas.image(),
                                  {whole_image_band(canvas.image())},
                                  {colliding_set({U'0', U'O'})});

    CHECK(forwards.runs[0].cells[0].codepoint
          == backwards.runs[0].cells[0].codepoint);
    CHECK(forwards.runs[0].cells[0].alternatives
          == backwards.runs[0].cells[0].alternatives);
}

TEST_CASE("An override from a later set is not an ambiguity")
{
    // Two sets disagreeing is a caller replacing a character deliberately --
    // how a VDU 23 redefinition arrives. Only a set colliding with itself
    // leaves a cell genuinely undecidable.
    const GlyphSet original = colliding_set({U'O'});
    GlyphSet redefined;
    redefined.name = "redefined";
    redefined.glyphs.push_back(Glyph(U'Q', original.glyphs[0].bitmap));

    Canvas canvas(8, 8);
    canvas.stamp(0, 0, original.glyphs[0].bitmap);

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               {original, redefined});

    REQUIRE(result.runs.size() == 1);
    const Cell& cell = result.runs[0].cells[0];
    CHECK(cell.codepoint == U'Q');
    CHECK_FALSE(cell.ambiguous());
    CHECK(cell.glyph_set == "redefined");
    CHECK(result.ambiguous_cells == 0);
}

TEST_CASE("An ordinary cell carries no alternatives")
{
    Canvas canvas(8 * 3, 8);
    canvas.stamp_text(0, 0, "ABC", acorn());

    const Result result = read(canvas.image(),
                               {whole_image_band(canvas.image())},
                               acorn_only());

    CHECK(result.ambiguous_cells == 0);
    for (const Cell& cell : result.runs[0].cells) {
        CHECK(cell.alternatives.empty());
        CHECK_FALSE(cell.ambiguous());
    }
}

// Period BBC fonts, as they were actually drawn.
//
// Five from a collection of sixty-eight, kept because each showed something
// the others did not. See tests/fixtures/fonts/README.md.

namespace {

GlyphSet font(const std::string& name)
{
    return load_glyph_file(
        std::string(SCREENTEXT_TEST_FIXTURES_DIR) + "/fonts/" + name
            + ".glyphs",
        name);
}

// Stamp every glyph of a set onto a grid and read it back, which is the whole
// of what a font has to survive.
Result round_trip(const GlyphSet& set, std::vector<char32_t>& expected)
{
    const std::size_t columns = 16;
    const std::size_t rows = (set.glyphs.size() + columns - 1) / columns;

    Canvas canvas(columns * 8, rows * 8);
    expected.clear();
    for (std::size_t index = 0; index < set.glyphs.size(); ++index) {
        const std::size_t x = (index % columns) * 8;
        const std::size_t y = (index / columns) * 8;
        canvas.stamp(x, y, set.glyphs[index].bitmap);
        expected.push_back(set.glyphs[index].codepoint);
    }
    return read(canvas.image(), {whole_image_band(canvas.image())}, {set});
}

} // namespace

TEST_CASE("A font nothing like the ROM's reads perfectly")
{
    // Broadway differs from the MOS font in ninety-four of its ninety-five
    // glyphs, and no two of them are alike. Nothing about matching depends on
    // the shapes being familiar.
    const GlyphSet set = font("broadway");
    CHECK(set.glyphs.size() == 95);

    std::vector<char32_t> expected;
    const Result result = round_trip(set, expected);

    CHECK(result.unmatched_cells == 0);
    CHECK(result.ambiguous_cells == 0);
}

TEST_CASE("A font that draws two characters alike says so")
{
    // FeltPen draws '0' and 'O' with the same pixels, and 'l' and '|' too.
    const GlyphSet set = font("feltpen");

    std::vector<char32_t> expected;
    const Result result = round_trip(set, expected);

    // FeltPen defines no space, alone among these fonts, so the cells left
    // over at the end of the grid are blank and match nothing. Honest: with
    // no space in the set, a blank cell really is unreadable.
    CHECK(set.glyphs.size() == 94);
    CHECK(result.unmatched_cells == 2);

    CHECK(result.ambiguous_cells == 4); // two pairs, both cells of each

    std::vector<std::vector<char32_t>> groups;
    for (const Run& run : result.runs) {
        for (const Cell& cell : run.cells) {
            if (!cell.ambiguous()) {
                continue;
            }
            std::vector<char32_t> group{cell.codepoint};
            group.insert(group.end(), cell.alternatives.begin(),
                         cell.alternatives.end());
            if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
                groups.push_back(group);
            }
        }
    }

    REQUIRE(groups.size() == 2);
    CHECK(groups[0] == std::vector<char32_t>{U'0', U'O'});
    CHECK(groups[1] == std::vector<char32_t>{U'l', U'|'});
}

TEST_CASE("A font can make three characters indistinguishable")
{
    // chocolate1 draws 'I', 'l' and '|' identically, so each of those cells
    // could equally be any of the three.
    const GlyphSet set = font("chocolate1");

    std::vector<char32_t> expected;
    const Result result = round_trip(set, expected);

    CHECK(result.unmatched_cells == 0);

    bool found = false;
    for (const Run& run : result.runs) {
        for (const Cell& cell : run.cells) {
            if (cell.codepoint == U'I' && cell.ambiguous()) {
                CHECK(cell.alternatives == std::vector<char32_t>{U'l', U'|'});
                found = true;
            }
        }
    }
    CHECK(found);
}

TEST_CASE("Characters no one would think to check can collide")
{
    // Futura draws '5' and 'S' alike; TrekFont draws '(' like '[' and ')'
    // like ']'. Worth stating outright, because a reader scanning output for
    // plausibility would never question any of them.
    for (const auto& [name, expected] :
         std::vector<std::pair<std::string, std::vector<char32_t>>>{
             {"futura", {U'5', U'S'}},
             {"trekfont", {U'(', U'['}},
         }) {
        INFO(name);
        const GlyphSet set = font(name);
        std::vector<char32_t> ignored;
        const Result result = round_trip(set, ignored);

        bool found = false;
        for (const Run& run : result.runs) {
            for (const Cell& cell : run.cells) {
                if (cell.codepoint == expected[0] && cell.ambiguous()) {
                    CHECK(cell.alternatives
                          == std::vector<char32_t>{expected[1]});
                    found = true;
                }
            }
        }
        CHECK(found);
    }
}

TEST_CASE("A screen in one font is not readable with another")
{
    // The reason a caller supplies a font at all. Read with the ROM set, a
    // Broadway screen is mostly unreadable -- and reported as such rather
    // than coming back as plausible nonsense.
    const GlyphSet broadway = font("broadway");

    std::vector<char32_t> expected;
    round_trip(broadway, expected);

    Canvas canvas(16 * 8, ((broadway.glyphs.size() + 15) / 16) * 8);
    for (std::size_t index = 0; index < broadway.glyphs.size(); ++index) {
        canvas.stamp((index % 16) * 8, (index / 16) * 8,
                     broadway.glyphs[index].bitmap);
    }

    const Result wrong = read(canvas.image(),
                              {whole_image_band(canvas.image())},
                              acorn_only());
    const Result right = read(canvas.image(),
                              {whole_image_band(canvas.image())},
                              {broadway});

    CHECK(right.unmatched_cells == 0);
    CHECK(wrong.unmatched_cells > broadway.glyphs.size() / 2);
}
