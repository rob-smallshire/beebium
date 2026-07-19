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

// The merge and linearisation rules behind GetScreenText.
//
// Exercised off synthetic runs rather than a booted machine, so the reading
// order and the layouts are pinned without any timing in the way. The strategy
// dispatch is tested here too, against a hand-built teletext grid.

#include <catch2/catch_test_macros.hpp>

#include "beebium/ScreenText.hpp"
#include "beebium/TeletextGrid.hpp"

#include <string>
#include <vector>

using namespace beebium;
using namespace beebium::screen;

namespace {

// A cell-aligned run on a 8x8 grid, at a given row and column.
TextRun run_at(uint32_t row, uint32_t column, const std::string& text,
               bool reached_right_edge = false) {
    TextRun run;
    run.text = text;
    run.bounds = {column * 8, row * 8, static_cast<uint32_t>(text.size()) * 8, 8};
    run.cell_width = 8;
    run.cell_height = 8;
    run.reached_right_edge = reached_right_edge;
    return run;
}

BandReading readable(std::vector<TextRun> runs) {
    BandReading reading;
    reading.supported = true;
    reading.runs = std::move(runs);
    return reading;
}

BandReading unreadable_band() {
    return BandReading{};
}

// A 40x25 grid with the given rows written from column 0.
TeletextGrid::Snapshot grid_of(const std::vector<std::string>& rows) {
    TeletextGrid grid;
    for (size_t row = 0; row < rows.size() && row < TeletextGrid::ROWS; ++row) {
        for (size_t column = 0;
             column < rows[row].size() && column < TeletextGrid::COLUMNS;
             ++column) {
            TeletextCell cell;
            cell.character = static_cast<uint8_t>(rows[row][column]);
            grid.set_cell(row, column, cell);
        }
    }
    grid.swap();
    return grid.snapshot();
}

Band teletext_band() {
    Band band;
    band.top = 0;
    band.bottom = 500;
    band.cell_width = 12;
    band.cell_height = 20;
    band.column_pitch = 12;
    band.row_pitch = 20;
    band.is_teletext = true;
    return band;
}

PixelRect whole_screen() {
    return {0, 0, 480, 500};
}

} // namespace

TEST_CASE("PixelRect intersection", "[screen-text]") {
    SECTION("overlapping rectangles meet in their overlap") {
        const PixelRect a{0, 0, 100, 100};
        const PixelRect b{50, 50, 100, 100};
        REQUIRE(a.intersected(b) == PixelRect{50, 50, 50, 50});
    }

    SECTION("disjoint rectangles do not meet") {
        const PixelRect a{0, 0, 10, 10};
        const PixelRect b{20, 20, 10, 10};
        REQUIRE(a.intersected(b).empty());
    }

    SECTION("a rectangle contained in another is itself") {
        const PixelRect outer{0, 0, 100, 100};
        const PixelRect inner{10, 10, 20, 20};
        REQUIRE(outer.intersected(inner) == inner);
    }
}

TEST_CASE("Linearisation joins runs into text", "[screen-text]") {
    SECTION("runs on different baselines become separate lines") {
        const std::vector<TextRun> runs{
            run_at(0, 0, "FIRST"),
            run_at(1, 0, "SECOND"),
        };
        REQUIRE(linearise(runs, Layout::Rows) == "FIRST\nSECOND");
    }

    SECTION("runs on one baseline share a line") {
        const std::vector<TextRun> runs{
            run_at(0, 0, "LEFT"),
            run_at(0, 20, "RIGHT"),
        };
        REQUIRE(linearise(runs, Layout::Rows) == "LEFT RIGHT");
    }

    SECTION("a blank row keeps its line") {
        // The shape of a selection is preserved: a gap between paragraphs is
        // part of what was on screen.
        const std::vector<TextRun> runs{
            run_at(0, 0, "ABOVE"),
            run_at(1, 0, ""),
            run_at(2, 0, "BELOW"),
        };
        REQUIRE(linearise(runs, Layout::Rows) == "ABOVE\n\nBELOW");
    }

    SECTION("trailing blank rows are stripped") {
        const std::vector<TextRun> runs{
            run_at(0, 0, "TEXT"),
            run_at(1, 0, ""),
            run_at(2, 0, ""),
        };
        REQUIRE(linearise(runs, Layout::Rows) == "TEXT");
    }

    SECTION("leading blank rows are kept") {
        const std::vector<TextRun> runs{
            run_at(0, 0, ""),
            run_at(1, 0, "TEXT"),
        };
        REQUIRE(linearise(runs, Layout::Rows) == "\nTEXT");
    }

    SECTION("lines are joined with LF and never CR") {
        const std::vector<TextRun> runs{
            run_at(0, 0, "ONE"),
            run_at(1, 0, "TWO"),
        };
        const std::string text = linearise(runs, Layout::Rows);
        REQUIRE(text.find('\r') == std::string::npos);
        REQUIRE(text.find('\n') != std::string::npos);
    }

    SECTION("no runs is no text") {
        REQUIRE(linearise({}, Layout::Rows).empty());
        REQUIRE(linearise({}, Layout::Flowed).empty());
    }
}

TEST_CASE("Flowed layout rejoins a line that wrapped", "[screen-text]") {
    SECTION("a run that reached the edge continues into the next") {
        const std::vector<TextRun> runs{
            run_at(0, 0, "WRAPPED", /*reached_right_edge=*/true),
            run_at(1, 0, "CONTINUATION"),
        };
        REQUIRE(linearise(runs, Layout::Flowed) == "WRAPPEDCONTINUATION");
    }

    SECTION("a run that stopped short ends its line") {
        const std::vector<TextRun> runs{
            run_at(0, 0, "SHORT", /*reached_right_edge=*/false),
            run_at(1, 0, "NEXT"),
        };
        REQUIRE(linearise(runs, Layout::Flowed) == "SHORT\nNEXT");
    }

    SECTION("Rows ignores the edge flag entirely") {
        const std::vector<TextRun> runs{
            run_at(0, 0, "WRAPPED", /*reached_right_edge=*/true),
            run_at(1, 0, "CONTINUATION"),
        };
        REQUIRE(linearise(runs, Layout::Rows) == "WRAPPED\nCONTINUATION");
    }
}

TEST_CASE("Merge orders bands top to bottom", "[screen-text]") {
    SECTION("runs come out in the order the bands were given") {
        Reading reading = merge(
            {
                readable({run_at(0, 0, "UPPER")}),
                readable({run_at(10, 0, "LOWER")}),
            },
            Layout::Rows);

        REQUIRE(reading.runs.size() == 2);
        REQUIRE(reading.runs[0].text == "UPPER");
        REQUIRE(reading.runs[1].text == "LOWER");
        REQUIRE(reading.text == "UPPER\nLOWER");
    }

    SECTION("within a band, runs sort by baseline then x") {
        Reading reading = merge(
            {
                readable({
                    run_at(1, 10, "SECOND-ROW-RIGHT"),
                    run_at(0, 20, "FIRST-ROW-RIGHT"),
                    run_at(0, 0, "FIRST-ROW-LEFT"),
                }),
            },
            Layout::Rows);

        REQUIRE(reading.runs.size() == 3);
        REQUIRE(reading.runs[0].text == "FIRST-ROW-LEFT");
        REQUIRE(reading.runs[1].text == "FIRST-ROW-RIGHT");
        REQUIRE(reading.runs[2].text == "SECOND-ROW-RIGHT");
    }
}

TEST_CASE("Merge reports what could be read", "[screen-text]") {
    SECTION("a band no strategy can read makes the whole request unsupported") {
        Reading reading = merge({unreadable_band()}, Layout::Rows);

        REQUIRE_FALSE(reading.supported);
        REQUIRE(reading.runs.empty());
        REQUIRE(reading.text.empty());
    }

    SECTION("one readable band is enough for the request to be supported") {
        // A split screen with a MODE 7 band reads that band and says so, even
        // while the band beside it contributes nothing.
        Reading reading = merge(
            {
                readable({run_at(0, 0, "READ ME")}),
                unreadable_band(),
            },
            Layout::Rows);

        REQUIRE(reading.supported);
        REQUIRE(reading.runs.size() == 1);
        REQUIRE(reading.text == "READ ME");
    }

    SECTION("a readable band that found nothing is still supported") {
        // Distinct from unreadable: the band was read and had no text on it.
        Reading reading = merge({readable({})}, Layout::Rows);

        REQUIRE(reading.supported);
        REQUIRE(reading.runs.empty());
    }

    SECTION("no bands at all is unsupported") {
        Reading reading = merge({}, Layout::Rows);
        REQUIRE_FALSE(reading.supported);
    }

    SECTION("uncertainty counts sum across bands") {
        BandReading first = readable({});
        first.unreadable_cells = 3;
        first.ambiguous_cells = 1;

        BandReading second = readable({});
        second.unreadable_cells = 4;
        second.ambiguous_cells = 2;

        Reading reading = merge({first, second}, Layout::Rows);

        REQUIRE(reading.unreadable_cells == 7);
        REQUIRE(reading.ambiguous_cells == 3);
    }
}

TEST_CASE("The teletext strategy reads a teletext band", "[screen-text]") {
    const TeletextGrid::Snapshot screen =
        grid_of({"BBC Computer 32K", "", "BASIC", "", ">"});

    SECTION("its runs are the grid rows") {
        const BandReading reading =
            read_band(teletext_band(), whole_screen(), Search::Both, screen);

        REQUIRE(reading.supported);
        REQUIRE(reading.runs.size() == TeletextGrid::ROWS);
        REQUIRE(reading.runs[0].text == "BBC Computer 32K");
        REQUIRE(reading.runs[2].text == "BASIC");
        REQUIRE(reading.runs[4].text == ">");
    }

    SECTION("runs carry the band's cell geometry so a selection can snap") {
        const BandReading reading =
            read_band(teletext_band(), whole_screen(), Search::Both, screen);

        REQUIRE(reading.runs[0].cell_width == 12);
        REQUIRE(reading.runs[0].cell_height == 20);
        REQUIRE(reading.runs[0].bounds.x == 0);
        REQUIRE(reading.runs[0].bounds.y == 0);
        REQUIRE(reading.runs[2].bounds.y == 2 * 20);
    }

    SECTION("teletext is never uncertain, its cells being exact codes") {
        const BandReading reading =
            read_band(teletext_band(), whole_screen(), Search::Both, screen);

        REQUIRE(reading.unreadable_cells == 0);
        REQUIRE(reading.ambiguous_cells == 0);
    }

    SECTION("the search mode does not change what teletext reads") {
        // Aligned and off-grid searching are choices a glyph recogniser makes.
        // The teletext grid is the grid.
        const BandReading both =
            read_band(teletext_band(), whole_screen(), Search::Both, screen);
        const BandReading aligned =
            read_band(teletext_band(), whole_screen(), Search::Aligned, screen);
        const BandReading offset =
            read_band(teletext_band(), whole_screen(), Search::Offset, screen);

        REQUIRE(both.runs == aligned.runs);
        REQUIRE(both.runs == offset.runs);
    }

    SECTION("a region reads only the cells inside it") {
        const PixelRect region{0, 0, 5 * 12, 1 * 20};
        const BandReading reading =
            read_band(teletext_band(), region, Search::Both, screen);

        REQUIRE(reading.runs.size() == 1);
        REQUIRE(reading.runs[0].text == "BBC C");
    }
}

TEST_CASE("A band with no strategy reports that it could not be read",
          "[screen-text]") {
    // Today that is every band the SAA5050 was not driving. When a strategy
    // that recognises glyphs in pixels is added behind this same dispatch,
    // these bands start returning runs and nothing above here changes.
    Band bitmap;
    bitmap.top = 0;
    bitmap.bottom = 256;
    bitmap.cell_width = 8;
    bitmap.cell_height = 8;
    bitmap.column_pitch = 8;
    bitmap.row_pitch = 8;
    bitmap.is_teletext = false;

    const TeletextGrid::Snapshot stale = grid_of({"STALE MODE 7 CELLS"});
    const BandReading reading =
        read_band(bitmap, {0, 0, 640, 256}, Search::Both, stale);

    REQUIRE_FALSE(reading.supported);
    REQUIRE(reading.runs.empty());
    REQUIRE(reading.unreadable_cells == 0);
}

TEST_CASE("Bands come from what the renderer recorded", "[screen-text]") {
    SECTION("one region is one band") {
        FrameMetadata meta;
        meta.height = 256;
        meta.regions.push_back({0, 256, 640, 8, false});

        const std::vector<Band> bands = bands_of(meta);

        REQUIRE(bands.size() == 1);
        REQUIRE(bands[0].top == 0);
        REQUIRE(bands[0].bottom == 256);
        REQUIRE(bands[0].cell_width == 8);
        REQUIRE(bands[0].cell_height == 8);
        REQUIRE(bands[0].row_pitch == 8);
        REQUIRE_FALSE(bands[0].is_teletext);
    }

    SECTION("a split screen is more than one band") {
        FrameMetadata meta;
        meta.height = 256;
        meta.regions.push_back({0, 192, 320, 8, false});
        meta.regions.push_back({192, 256, 160, 8, false});

        const std::vector<Band> bands = bands_of(meta);

        REQUIRE(bands.size() == 2);
        REQUIRE(bands[0].top == 0);
        REQUIRE(bands[0].bottom == 192);
        REQUIRE(bands[1].top == 192);
        REQUIRE(bands[1].bottom == 256);
    }

    SECTION("a ten-scanline character row has an eight-scanline cell") {
        // MODE 3 and MODE 6 blank the two spare scanlines rather than painting
        // them, so the cell is shorter than the pitch. Reporting the pitch as
        // the cell size is silent while the background is black and total once
        // it is not.
        FrameMetadata meta;
        meta.height = 250;
        meta.regions.push_back({0, 250, 640, 10, false});

        const std::vector<Band> bands = bands_of(meta);

        REQUIRE(bands.size() == 1);
        REQUIRE(bands[0].row_pitch == 10);
        REQUIRE(bands[0].cell_height == 8);
        REQUIRE(bands[0].cell_width == 8);
        REQUIRE(bands[0].column_pitch == 8);
    }

    SECTION("a teletext band is marked as one and has teletext geometry") {
        FrameMetadata meta;
        meta.height = 500;
        meta.interlaced = true;
        meta.regions.push_back({0, 500, 480, 20, true});

        const std::vector<Band> bands = bands_of(meta);

        REQUIRE(bands.size() == 1);
        REQUIRE(bands[0].is_teletext);
        REQUIRE(bands[0].cell_width == 12);
        REQUIRE(bands[0].column_pitch == 12);
        REQUIRE(bands[0].row_pitch == 20);
        REQUIRE(bands[0].cell_height == 20);
    }

    SECTION("a frame with no regions has no bands") {
        FrameMetadata meta;
        REQUIRE(bands_of(meta).empty());
    }
}
