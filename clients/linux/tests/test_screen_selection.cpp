// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Screen text selection as geometry (client specification 6.5), in frame
// pixels: snapping a drag to the grid, the rows flow and its trim, the
// selected range, which cells to highlight, and the text a copy takes.

#include "connection/ScreenSelection.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

TextRun glyph_run(std::string text, std::uint32_t x, std::uint32_t y, std::vector<bool> matched) {
    TextRun run;
    run.text = std::move(text);
    run.cell_width = 8;
    run.cell_height = 8;
    run.bounds = {x, y, static_cast<std::uint32_t>(8 * matched.size()), 8};
    for (std::size_t i = 0; i < matched.size(); ++i) {
        run.cells.push_back({{x + static_cast<std::uint32_t>(8 * i), y, 8, 8}, matched[i]});
    }
    return run;
}

// MODE 3's grid: an eight-line glyph on a ten-line pitch, 80 columns.
ScreenBand mode3_band() {
    ScreenBand band;
    band.top = 0;
    band.bottom = 250;
    band.cell_width = 8;
    band.cell_height = 8;
    band.column_pitch = 8;
    band.row_pitch = 10;
    return band;
}

// A teletext-style run: no cells, one character per column.
TextRun row_run(std::string text, std::uint32_t x, std::uint32_t y) {
    TextRun run;
    run.bounds = {x, y, static_cast<std::uint32_t>(8 * text.size()), 8};
    run.text = std::move(text);
    run.cell_width = 8;
    run.cell_height = 8;
    return run;
}

}  // namespace

TEST_CASE("A point snaps to the cell it is inside, from the grid's origin",
          "[screen-selection][grid]") {
    ScreenBand band = mode3_band();
    band.origin_x = 4;
    band.origin_y = 20;
    CHECK(column_at(band, 4) == 0);
    CHECK(column_at(band, 11) == 0);
    CHECK(column_at(band, 12) == 1);
    CHECK(row_at(band, 29) == 0);
    CHECK(row_at(band, 30) == 1);
    // Above or left of the origin is the first cell, not a negative one.
    CHECK(column_at(band, 0) == 0);
    CHECK(row_at(band, 0) == 0);
    CHECK(frame_x_of(band, 2) == 20);
    CHECK(frame_y_of(band, 2) == 40);
}

TEST_CASE("A selection snaps to the band holding its top, else the first gridded band",
          "[screen-selection][grid]") {
    ScreenGrid grid;
    ScreenBand ungridded;
    ungridded.top = 0;
    ungridded.bottom = 100;
    ScreenBand upper = mode3_band();
    upper.top = 100;
    upper.bottom = 150;
    ScreenBand lower = mode3_band();
    lower.top = 150;
    lower.bottom = 256;
    lower.column_pitch = 16;
    grid.bands = {ungridded, upper, lower};
    CHECK(selection_band(grid, 160.0) == &grid.bands[2]);
    CHECK(selection_band(grid, 120.5) == &grid.bands[1]);
    CHECK(selection_band(grid, 10.0) == &grid.bands[1]);  // its own band has no grid
    CHECK(selection_band(ScreenGrid{}, 10.0) == nullptr);
}

TEST_CASE("The marquee is the drag's bounding box out to whole pixels",
          "[screen-selection][anywhere]") {
    CHECK(bounding_rect({30.5, 40.2}, {10.7, 12.0}) == PixelRect{10, 12, 21, 29});
}

TEST_CASE("Columns take every cell the drag touches", "[screen-selection][columns]") {
    const ScreenBand band = mode3_band();
    // From column 1 of row 1 back up to column 4 of row 0, drawn upwards.
    CHECK(snapped_rectangle({39.0, 5.0}, {9.0, 12.0}, &band) == PixelRect{8, 0, 32, 18});
    // No grid: the marquee.
    CHECK(snapped_rectangle({39.0, 5.0}, {9.0, 12.0}, nullptr) == PixelRect{9, 5, 30, 7});
}

TEST_CASE("Rows run from the earlier cell to the later, read at full width",
          "[screen-selection][rows]") {
    const ScreenBand band = mode3_band();
    // Dragged backwards: from row 2 column 5 up to row 0 column 3.
    const RowsFlow flow = rows_flow({44.0, 25.0}, {26.0, 3.0}, band, 640);
    CHECK(flow.start == GridCell{0, 3});
    CHECK(flow.end == GridCell{2, 5});
    // Top of the first row to the bottom of the last row's glyphs.
    CHECK(flow.request_region == PixelRect{0, 0, 640, 28});
}

TEST_CASE("The rows range is a flow, each row a pitch tall", "[screen-selection][rows]") {
    const ScreenBand band = mode3_band();
    CHECK(range_rects(Interpretation::rows, {26.0, 3.0}, {44.0, 35.0}, &band, 640) ==
          std::vector<PixelRect>{{24, 0, 616, 10}, {0, 10, 640, 20}, {0, 30, 48, 10}});
    // Two rows: no middle block.
    CHECK(range_rects(Interpretation::rows, {26.0, 3.0}, {44.0, 15.0}, &band, 640) ==
          std::vector<PixelRect>{{24, 0, 616, 10}, {0, 10, 48, 10}});
    // One row: from cell to cell.
    CHECK(range_rects(Interpretation::rows, {44.0, 3.0}, {26.0, 3.0}, &band, 640) ==
          std::vector<PixelRect>{{24, 0, 24, 10}});
}

TEST_CASE("Columns' range is its block and anywhere has none", "[screen-selection][range]") {
    const ScreenBand band = mode3_band();
    CHECK(range_rects(Interpretation::columns, {39.0, 5.0}, {9.0, 12.0}, &band, 640) ==
          std::vector<PixelRect>{{8, 0, 32, 18}});
    CHECK(range_rects(Interpretation::anywhere, {39.0, 5.0}, {9.0, 12.0}, &band, 640).empty());
    CHECK(range_rects(Interpretation::rows, {39.0, 5.0}, {9.0, 12.0}, nullptr, 640).empty());
}

TEST_CASE("Rows text trims the ends of the flow and keeps whole rows between",
          "[screen-selection][rows]") {
    const ScreenBand band = mode3_band();
    // From row 0 column 2 to row 2 column 3.
    const RowsFlow flow = rows_flow({16.0, 0.0}, {24.0, 20.0}, band, 640);
    const RowsText rows =
        trim_to_flow({row_run("HELLO", 0, 0), row_run("WORLD", 0, 10), row_run("AGAIN", 0, 20)},
                     band, flow);
    CHECK(rows.text == "LLO\nWORLD\nAGAI");
    REQUIRE(rows.runs.size() == 3);
    CHECK(rows.runs[0].bounds == PixelRect{16, 0, 24, 8});
    CHECK(rows.runs[2].bounds == PixelRect{0, 20, 32, 8});
}

TEST_CASE("Rows text has a line for every row in the flow, blank ones included",
          "[screen-selection][rows]") {
    const ScreenBand band = mode3_band();
    // Rows 0 to 4: text on rows 1 and 3 only, an empty teletext run on 2.
    const RowsFlow flow = rows_flow({0.0, 0.0}, {632.0, 40.0}, band, 640);
    const RowsText rows =
        trim_to_flow({row_run("ONE", 0, 10), row_run("", 0, 20), row_run("TWO", 0, 30)}, band, flow);
    CHECK(rows.text == "\nONE\n\nTWO\n");
}

TEST_CASE("Runs on one row join in reading order; runs outside the flow are dropped",
          "[screen-selection][rows]") {
    const ScreenBand band = mode3_band();
    const RowsFlow flow = rows_flow({0.0, 10.0}, {632.0, 10.0}, band, 640);
    const RowsText rows = trim_to_flow(
        {row_run("ABOVE", 0, 0), row_run("RIGHT", 80, 10), row_run("LEFT ", 0, 10)}, band, flow);
    CHECK(rows.text == "LEFT RIGHT");
}

TEST_CASE("A trimmed run keeps each character's own cell", "[screen-selection][rows]") {
    const ScreenBand band = mode3_band();
    const RowsFlow flow = rows_flow({8.0, 0.0}, {16.0, 0.0}, band, 640);
    const RowsText rows = trim_to_flow({glyph_run("A?C D", 0, 0, {true, false, true, true, true})},
                                       band, flow);
    CHECK(rows.text == "?C");
    REQUIRE(rows.runs.size() == 1);
    REQUIRE(rows.runs[0].cells.size() == 2);
    CHECK_FALSE(rows.runs[0].cells[0].matched);
    CHECK(rows.runs[0].cells[1].bounds == PixelRect{16, 0, 8, 8});
}

TEST_CASE("A multi-byte character trims as one cell", "[screen-selection][rows]") {
    const ScreenBand band = mode3_band();
    const RowsFlow flow = rows_flow({8.0, 0.0}, {8.0, 0.0}, band, 640);
    const RowsText rows = trim_to_flow({row_run("x\xE2\x86\x91y", 0, 0)}, band, flow);
    CHECK(rows.text == "\xE2\x86\x91");
}

TEST_CASE("Text splits into one character per cell, multi-byte ones whole",
          "[screen-selection][text]") {
    CHECK(characters("AB") == std::vector<std::string>{"A", "B"});
    // A Mode 7 screen copied as displayed: an up arrow is three bytes.
    CHECK(characters("x\xE2\x86\x91y") == std::vector<std::string>{"x", "\xE2\x86\x91", "y"});
    CHECK(characters("").empty());
}

TEST_CASE("A teletext run, which has no cells, is lit whole", "[screen-selection][highlight]") {
    TextRun run;
    run.text = "HELLO";
    run.bounds = {0, 20, 80, 20};
    CHECK(matched_cell_rects({run}) == std::vector<PixelRect>{{0, 20, 80, 20}});
}

TEST_CASE("Matched cells are lit from the first real glyph to the last",
          "[screen-selection][highlight]") {
    // " AB C?" : the leading space is adrift, the one between B and C bridges,
    // the unmatched last cell is ink no glyph fit.
    const auto run = glyph_run(" AB C?", 0, 0, {true, true, true, true, true, false});
    CHECK(matched_cell_rects({run}) ==
          std::vector<PixelRect>{{8, 0, 8, 8}, {16, 0, 8, 8}, {24, 0, 8, 8}, {32, 0, 8, 8}});
}

TEST_CASE("An unmatched glyph inside the span stays dark", "[screen-selection][highlight]") {
    const auto run = glyph_run("A?B", 0, 0, {true, false, true});
    CHECK(matched_cell_rects({run}) == std::vector<PixelRect>{{0, 0, 8, 8}, {16, 0, 8, 8}});
}

TEST_CASE("A run with no real glyph lights nothing", "[screen-selection][highlight]") {
    // A game's own font: every letter unmatched, only the gaps match.
    const auto run = glyph_run("?? ??", 0, 0, {false, false, true, false, false});
    CHECK(matched_cell_rects({run}).empty());
}

// The macOS client's cases for the same functions, on a plain eight-by-eight
// grid with no gaps between rows.

namespace {

ScreenBand plain_band() {
    ScreenBand band;
    band.bottom = 256;
    band.cell_width = 8;
    band.cell_height = 8;
    band.column_pitch = 8;
    band.row_pitch = 8;
    return band;
}

TextRun run_at(std::string text, PixelRect bounds) {
    TextRun run;
    run.text = std::move(text);
    run.bounds = bounds;
    run.cell_width = 8;
    run.cell_height = 8;
    return run;
}

TextRun celled_run(std::string text, std::vector<bool> matched) {
    TextRun run = run_at(std::move(text), {0, 0, static_cast<std::uint32_t>(8 * matched.size()), 8});
    for (std::size_t i = 0; i < matched.size(); ++i) {
        run.cells.push_back({{static_cast<std::uint32_t>(8 * i), 0, 8, 8}, matched[i]});
    }
    return run;
}

}  // namespace

TEST_CASE("Plain grid: the bounding box is the same whichever way the drag went",
          "[screen-selection][plain]") {
    CHECK(bounding_rect({30, 18}, {20, 2}) == PixelRect{20, 2, 10, 16});
}

TEST_CASE("Plain grid: the snapped block expands to whole cells, or is the box without a grid",
          "[screen-selection][plain]") {
    const ScreenBand band = plain_band();
    CHECK(snapped_rectangle({20, 2}, {30, 18}, &band) == PixelRect{16, 0, 16, 24});
    CHECK(snapped_rectangle({20, 2}, {30, 18}, nullptr) == PixelRect{20, 2, 10, 16});
}

TEST_CASE("Plain grid: reading order puts the higher row first, then the smaller column",
          "[screen-selection][plain]") {
    const ScreenBand band = plain_band();
    CHECK(rows_flow({44, 20}, {12, 4}, band, 640).start == GridCell{0, 1});
    CHECK(rows_flow({44, 12}, {20, 12}, band, 640).start == GridCell{1, 2});
}

TEST_CASE("Plain grid: rows ask for the full width across their rows", "[screen-selection][plain]") {
    const RowsFlow flow = rows_flow({20, 2}, {30, 18}, plain_band(), 640);
    CHECK(flow.start == GridCell{0, 2});
    CHECK(flow.end == GridCell{2, 3});
    CHECK(flow.request_region == PixelRect{0, 0, 640, 24});
}

TEST_CASE("Plain grid: the trim keeps ragged first and last rows", "[screen-selection][plain]") {
    const ScreenBand band = plain_band();
    const RowsFlow flow{{0, 2}, {2, 3}, {0, 0, 640, 24}};
    const RowsText rows = trim_to_flow({run_at("ABCDEF", {0, 0, 48, 8}), run_at("GHIJ", {0, 8, 32, 8}),
                                        run_at("KLMNOP", {0, 16, 48, 8})},
                                       band, flow);
    CHECK(rows.text == "CDEF\nGHIJ\nKLMN");
    REQUIRE(rows.runs.size() == 3);
    CHECK(rows.runs[0].bounds.x == 16);
    CHECK(rows.runs[0].text == "CDEF");
}

TEST_CASE("Plain grid: the trim drops runs outside the rows", "[screen-selection][plain]") {
    const RowsFlow flow{{1, 0}, {1, 3}, {0, 8, 640, 8}};
    CHECK(trim_to_flow({run_at("ABOVE", {0, 0, 40, 8}), run_at("WXYZ", {0, 8, 32, 8})}, plain_band(),
                       flow)
              .text == "WXYZ");
}

TEST_CASE("Plain grid: blank rows are empty lines, whether missing or empty runs",
          "[screen-selection][plain]") {
    const ScreenBand band = plain_band();
    const RowsFlow three{{0, 0}, {2, 79}, {0, 0, 640, 24}};
    CHECK(trim_to_flow({run_at("AAA", {0, 0, 24, 8}), run_at("CCC", {0, 16, 24, 8})}, band, three)
              .text == "AAA\n\nCCC");
    CHECK(trim_to_flow({run_at("AAA", {0, 0, 24, 8}), run_at("", {0, 8, 640, 8}),
                        run_at("CCC", {0, 16, 24, 8})},
                       band, three)
              .text == "AAA\n\nCCC");
    // At the ends too: five rows, five lines.
    const RowsFlow five{{0, 0}, {4, 79}, {0, 0, 640, 40}};
    CHECK(trim_to_flow({run_at("AAA", {0, 8, 24, 8}), run_at("CCC", {0, 24, 24, 8})}, band, five)
              .text == "\nAAA\n\nCCC\n");
}

TEST_CASE("Plain grid: an empty run is no highlight", "[screen-selection][plain]") {
    const RowsFlow flow{{0, 0}, {2, 79}, {0, 0, 640, 24}};
    const RowsText rows =
        trim_to_flow({run_at("AAA", {0, 0, 24, 8}), run_at("", {0, 8, 640, 8})}, plain_band(), flow);
    REQUIRE(rows.runs.size() == 1);
    CHECK(rows.runs[0].text == "AAA");
}

TEST_CASE("Plain grid: the trim carries cells, so an unread cell stays dark",
          "[screen-selection][plain]") {
    const RowsFlow flow{{0, 2}, {0, 5}, {0, 0, 640, 8}};
    const RowsText rows = trim_to_flow({celled_run("ABCDEF", {true, true, false, true, true, true})},
                                       plain_band(), flow);
    CHECK(rows.text == "CDEF");
    REQUIRE(rows.runs.size() == 1);
    CHECK(rows.runs[0].cells.size() == 4);
    CHECK(matched_cell_rects(rows.runs) ==
          std::vector<PixelRect>{{24, 0, 8, 8}, {32, 0, 8, 8}, {40, 0, 8, 8}});
}

TEST_CASE("Plain grid: the rows range flows to the screen's edges", "[screen-selection][plain]") {
    const ScreenBand band = plain_band();
    CHECK(range_rects(Interpretation::rows, {20, 2}, {30, 18}, &band, 640) ==
          std::vector<PixelRect>{{16, 0, 624, 8}, {0, 8, 640, 8}, {0, 16, 32, 8}});
    CHECK(range_rects(Interpretation::rows, {20, 2}, {52, 2}, &band, 640) ==
          std::vector<PixelRect>{{16, 0, 40, 8}});
    CHECK(range_rects(Interpretation::rows, {20, 2}, {30, 10}, &band, 640) ==
          std::vector<PixelRect>{{16, 0, 624, 8}, {0, 8, 32, 8}});
}

TEST_CASE("Plain grid: columns' range is the block, anywhere's and gridless rows' are empty",
          "[screen-selection][plain]") {
    const ScreenBand band = plain_band();
    CHECK(range_rects(Interpretation::columns, {20, 2}, {30, 18}, &band, 640) ==
          std::vector<PixelRect>{{16, 0, 16, 24}});
    CHECK(range_rects(Interpretation::anywhere, {20, 2}, {30, 18}, &band, 640).empty());
    CHECK(range_rects(Interpretation::rows, {20, 2}, {30, 18}, nullptr, 640).empty());
}

TEST_CASE("Plain grid: highlighting skips unmatched cells and lights bridging spaces",
          "[screen-selection][plain]") {
    CHECK(matched_cell_rects({celled_run("A B", {true, false, true})}) ==
          std::vector<PixelRect>{{0, 0, 8, 8}, {16, 0, 8, 8}});
    CHECK(matched_cell_rects({celled_run("A B", {true, true, true})}) ==
          std::vector<PixelRect>{{0, 0, 8, 8}, {8, 0, 8, 8}, {16, 0, 8, 8}});
    CHECK(matched_cell_rects({celled_run("    ", {false, false, true, false})}).empty());
    TextRun teletext = run_at("HELLO", {0, 0, 80, 20});
    teletext.cell_width = 16;
    teletext.cell_height = 20;
    CHECK(matched_cell_rects({teletext}) == std::vector<PixelRect>{{0, 0, 80, 20}});
}
