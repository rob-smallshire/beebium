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

// The band-concatenation and linearisation rules behind GetScreenText.
//
// Exercised off synthetic runs rather than a booted machine, so the reading
// order and the layouts are pinned without any timing in the way. The strategy
// dispatch is tested here too, against a hand-built teletext grid.

#include <catch2/catch_test_macros.hpp>

#include "beebium/ScreenText.hpp"
#include "beebium/TeletextGrid.hpp"

#include <screentext/Glyph.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace beebium;
using namespace beebium::screen;

namespace {

// The teletext strategy needs only the snapshot; wrap it in the sources the
// dispatch now takes.
BandSources teletext_sources(const TeletextGrid::Snapshot& snapshot) {
    BandSources sources;
    sources.teletext = &snapshot;
    return sources;
}

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
    band.cell_width = 16;
    band.cell_height = 20;
    band.column_pitch = 16;
    band.row_pitch = 20;
    band.is_teletext = true;
    return band;
}

PixelRect whole_screen() {
    // A teletext character is sixteen framebuffer pixels wide, so a 40-column
    // screen is 640 across, the same width the renderer reports.
    return {0, 0, 640, 500};
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

    SECTION("leading blank rows are kept, unlike trailing ones") {
        // Deliberately asymmetric. Trailing blanks are the empty bottom of a
        // screen that is 25 rows whatever is on it, and carry nothing. Leading
        // blanks position everything after them: a caller that finds a line in
        // this text and then reads that row back with a row-indexed call --
        // which is what the row parameter means -- would be off by however many
        // were dropped. A GUI is free to trim them for presentation, and does;
        // the API stays faithful.
        const std::vector<TextRun> runs{
            run_at(0, 0, ""),
            run_at(1, 0, "TEXT"),
        };
        REQUIRE(linearise(runs, Layout::Rows) == "\nTEXT");
    }

    SECTION("blank rows between text survive the trailing trim") {
        const std::vector<TextRun> runs{
            run_at(0, 0, "ABOVE"),
            run_at(1, 0, ""),
            run_at(2, 0, "BELOW"),
            run_at(3, 0, ""),
            run_at(4, 0, ""),
        };
        REQUIRE(linearise(runs, Layout::Rows) == "ABOVE\n\nBELOW");
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

TEST_CASE("Concatenation orders bands top to bottom", "[screen-text]") {
    SECTION("runs come out in the order the bands were given") {
        Reading reading = concatenate_bands_readings(
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
        Reading reading = concatenate_bands_readings(
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

TEST_CASE("Concatenation reports what could be read", "[screen-text]") {
    SECTION("a band no strategy can read makes the whole request unsupported") {
        Reading reading = concatenate_bands_readings({unreadable_band()}, Layout::Rows);

        REQUIRE_FALSE(reading.supported);
        REQUIRE(reading.runs.empty());
        REQUIRE(reading.text.empty());
    }

    SECTION("one readable band is enough for the request to be supported") {
        // A split screen with a MODE 7 band reads that band and says so, even
        // while the band beside it contributes nothing.
        Reading reading = concatenate_bands_readings(
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
        Reading reading = concatenate_bands_readings({readable({})}, Layout::Rows);

        REQUIRE(reading.supported);
        REQUIRE(reading.runs.empty());
    }

    SECTION("no bands at all is unsupported") {
        Reading reading = concatenate_bands_readings({}, Layout::Rows);
        REQUIRE_FALSE(reading.supported);
    }

    SECTION("uncertainty counts sum across bands") {
        BandReading first = readable({});
        first.unreadable_cells = 3;
        first.ambiguous_cells = 1;

        BandReading second = readable({});
        second.unreadable_cells = 4;
        second.ambiguous_cells = 2;

        Reading reading = concatenate_bands_readings({first, second}, Layout::Rows);

        REQUIRE(reading.unreadable_cells == 7);
        REQUIRE(reading.ambiguous_cells == 3);
    }
}

TEST_CASE("A mosaic reads as the block it drew, through read_band",
          "[screen-text]") {
    // The path a copy actually takes. teletext_text() and this band reader
    // once each carried their own copy of "what a cell copies as", kept in
    // step by a comment; both now ask teletext_cell_codepoint(), and this
    // pins that they agree.
    TeletextGrid grid;
    TeletextCell mosaic;
    mosaic.character = 0x21;  // graphics marker plus the top-left block
    mosaic.charset = TeletextCellCharset::ContiguousGraphics;
    grid.set_cell(0, 0, mosaic);
    grid.swap();
    const TeletextGrid::Snapshot screen = grid.snapshot();

    const BandReading as_displayed =
        read_band(teletext_band(), whole_screen(), Search::Anywhere,
                  teletext_sources(screen), TeletextCharacters::Displayed);
    REQUIRE(as_displayed.supported);
    CHECK(as_displayed.runs[0].text == "\xF0\x9F\xAC\x80");  // U+1FB00

    const BandReading as_codes =
        read_band(teletext_band(), whole_screen(), Search::Anywhere,
                  teletext_sources(screen), TeletextCharacters::Codes);
    CHECK(as_codes.runs[0].text.empty());
}

TEST_CASE("The teletext strategy reads a teletext band", "[screen-text]") {
    const TeletextGrid::Snapshot screen =
        grid_of({"BBC Computer 32K", "", "BASIC", "", ">"});

    SECTION("its runs are the grid rows") {
        const BandReading reading =
            read_band(teletext_band(), whole_screen(), Search::Anywhere, teletext_sources(screen));

        REQUIRE(reading.supported);
        REQUIRE(reading.runs.size() == TeletextGrid::ROWS);
        REQUIRE(reading.runs[0].text == "BBC Computer 32K");
        REQUIRE(reading.runs[2].text == "BASIC");
        REQUIRE(reading.runs[4].text == ">");
    }

    SECTION("runs carry the band's cell geometry so a selection can snap") {
        const BandReading reading =
            read_band(teletext_band(), whole_screen(), Search::Anywhere, teletext_sources(screen));

        REQUIRE(reading.runs[0].cell_width == 16);
        REQUIRE(reading.runs[0].cell_height == 20);
        REQUIRE(reading.runs[0].bounds.x == 0);
        REQUIRE(reading.runs[0].bounds.y == 0);
        REQUIRE(reading.runs[2].bounds.y == 2 * 20);
    }

    SECTION("teletext runs carry no cells, their characters being exact") {
        // Every teletext cell is a known character, so there is nothing per
        // cell to distinguish; a client highlights the whole run's bounds.
        const BandReading reading =
            read_band(teletext_band(), whole_screen(), Search::Anywhere, teletext_sources(screen));

        REQUIRE(reading.runs[0].cells.empty());
    }

    SECTION("teletext is never uncertain, its cells being exact codes") {
        const BandReading reading =
            read_band(teletext_band(), whole_screen(), Search::Anywhere, teletext_sources(screen));

        REQUIRE(reading.unreadable_cells == 0);
        REQUIRE(reading.ambiguous_cells == 0);
    }

    SECTION("the search mode does not change what teletext reads") {
        // Reading everywhere versus only the grid is a choice a glyph
        // recogniser makes. The teletext grid is the grid.
        const BandReading anywhere =
            read_band(teletext_band(), whole_screen(), Search::Anywhere, teletext_sources(screen));
        const BandReading aligned =
            read_band(teletext_band(), whole_screen(), Search::Aligned, teletext_sources(screen));

        REQUIRE(anywhere.runs == aligned.runs);
    }

    SECTION("a region reads only the cells inside it") {
        const PixelRect region{0, 0, 5 * 16, 1 * 20};
        const BandReading reading =
            read_band(teletext_band(), region, Search::Anywhere, teletext_sources(screen));

        REQUIRE(reading.runs.size() == 1);
        REQUIRE(reading.runs[0].text == "BBC C");
    }
}

namespace {

constexpr uint32_t BLACK = 0xFF000000;
constexpr uint32_t WHITE = 0xFFFFFFFF;

// A framebuffer-shaped canvas of logical pixels, painted with glyphs the way
// the renderer would, so the bitmap strategy can be exercised without a
// machine. Tightly packed, so the stride is the width.
struct Canvas {
    uint32_t width;
    uint32_t height;
    std::vector<uint32_t> pixels;

    Canvas(uint32_t w, uint32_t h, uint32_t fill = BLACK)
        : width(w), height(h),
          pixels(static_cast<size_t>(w) * h, fill) {}

    FrameImage image() const {
        return FrameImage{pixels.data(), width, width, height};
    }

    void set(uint32_t x, uint32_t y, uint32_t colour) {
        pixels[static_cast<size_t>(y) * width + x] = colour;
    }
};

const screentext::Bitmap& glyph_bitmap(const screentext::GlyphSet& set,
                                       char32_t codepoint) {
    for (const screentext::Glyph& glyph : set.glyphs) {
        if (glyph.codepoint == codepoint) {
            return glyph.bitmap;
        }
    }
    throw std::runtime_error("no glyph for codepoint");
}

// Stamp an 8x8 bitmap at a pixel origin: set bits foreground, clear bits left
// as whatever was there, matching how a glyph is drawn over a background.
void stamp(Canvas& canvas, uint32_t x0, uint32_t y0,
           const screentext::Bitmap& bitmap, uint32_t fg = WHITE) {
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            if (bitmap.pixel(x, y)) {
                canvas.set(x0 + x, y0 + y, fg);
            }
        }
    }
}

// Paint a string from the built-in glyphs, starting at a pixel origin and
// stepping by the pitch.
void paint_text(Canvas& canvas, const screentext::GlyphSet& set,
                uint32_t x0, uint32_t y0, const std::string& text,
                uint32_t fg = WHITE, uint32_t pitch = 8) {
    for (size_t i = 0; i < text.size(); ++i) {
        stamp(canvas, x0 + static_cast<uint32_t>(i) * pitch, y0,
              glyph_bitmap(set, static_cast<char32_t>(text[i])), fg);
    }
}

Band bitmap_band(uint32_t top, uint32_t bottom, uint32_t row_pitch = 8) {
    Band band;
    band.top = top;
    band.bottom = bottom;
    band.cell_width = 8;
    band.cell_height = 8;
    band.column_pitch = 8;
    band.row_pitch = row_pitch;
    band.is_teletext = false;
    return band;
}

const screentext::GlyphSet& acorn() {
    return screentext::builtin_glyph_set("acorn-mos-1.20");
}

BandSources bitmap_sources(const Canvas& canvas,
                           const std::vector<screentext::GlyphSet>& sets) {
    BandSources sources;
    sources.image = canvas.image();
    sources.glyph_sets = &sets;
    return sources;
}

} // namespace

TEST_CASE("The bitmap strategy reads a band the SAA5050 was not driving",
          "[screen-text]") {
    const std::vector<screentext::GlyphSet> sets{acorn()};

    SECTION("a blank screen is supported and contributes no text") {
        // Read, and read as having no text on it -- distinct from a band no
        // strategy could read, which is what this used to report.
        Canvas canvas(8 * 10, 8);
        const BandReading reading = read_band(
            bitmap_band(0, 8), {0, 0, canvas.width, canvas.height},
            Search::Aligned, bitmap_sources(canvas, sets));

        REQUIRE(reading.supported);
        REQUIRE(reading.runs.empty());
        REQUIRE(reading.unreadable_cells == 0);
        REQUIRE(reading.ambiguous_cells == 0);
    }

    SECTION("a line of text reads back as itself") {
        Canvas canvas(8 * 10, 8);
        paint_text(canvas, acorn(), 0, 0, "HELLO");
        const BandReading reading = read_band(
            bitmap_band(0, 8), {0, 0, canvas.width, canvas.height},
            Search::Aligned, bitmap_sources(canvas, sets));

        REQUIRE(reading.supported);
        REQUIRE(reading.runs.size() == 1);
        REQUIRE(reading.runs[0].text == "HELLO");
        REQUIRE(reading.runs[0].cell_width == 8);
        REQUIRE(reading.runs[0].cell_height == 8);
    }

    SECTION("a run carries its cells so a client highlights only what was read") {
        Canvas canvas(8 * 10, 8);
        paint_text(canvas, acorn(), 0, 0, "HELLO");
        const BandReading reading = read_band(
            bitmap_band(0, 8), {0, 0, canvas.width, canvas.height},
            Search::Aligned, bitmap_sources(canvas, sets));

        REQUIRE(reading.runs.size() == 1);
        const std::vector<TextCell>& cells = reading.runs[0].cells;
        REQUIRE(cells.size() == 5);
        for (uint32_t i = 0; i < cells.size(); ++i) {
            REQUIRE(cells[i].matched);
            REQUIRE(cells[i].bounds.x == i * 8);
            REQUIRE(cells[i].bounds.width == 8);
        }
    }

    SECTION("a cell the font cannot read is carried as unmatched") {
        // A diagonal stroke is no Acorn glyph, so the middle cell matches
        // nothing. It still occupies its column -- as a space in the text -- but
        // is flagged unmatched so a client can leave it dark.
        Canvas canvas(8 * 3, 8);
        paint_text(canvas, acorn(), 0, 0, "A");
        paint_text(canvas, acorn(), 16, 0, "B");
        for (uint32_t i = 0; i < 8; ++i) {
            canvas.set(8 + i, i, WHITE);
        }
        const BandReading reading = read_band(
            bitmap_band(0, 8), {0, 0, canvas.width, canvas.height},
            Search::Aligned, bitmap_sources(canvas, sets));

        REQUIRE(reading.runs.size() == 1);
        REQUIRE(reading.unreadable_cells >= 1);
        const std::vector<TextCell>& cells = reading.runs[0].cells;
        REQUIRE(cells.size() == 3);
        REQUIRE(cells[0].matched);
        REQUIRE_FALSE(cells[1].matched);
        REQUIRE(cells[2].matched);
    }

    SECTION("runs are placed in frame pixels, offset by the band top") {
        Canvas canvas(8 * 6, 200);
        paint_text(canvas, acorn(), 8, 96, "HI");
        const BandReading reading = read_band(
            bitmap_band(64, 128), {0, 0, canvas.width, canvas.height},
            Search::Aligned, bitmap_sources(canvas, sets));

        REQUIRE(reading.runs.size() == 1);
        // The text was painted one cell in, so the run opens with the blank
        // cell that puts it there and starts at the row's left edge. What this
        // section is about is the y: the band's own top lifts it into frame
        // pixels.
        REQUIRE(reading.runs[0].text == " HI");
        REQUIRE(reading.runs[0].bounds.x == 0);
        REQUIRE(reading.runs[0].bounds.y == 96);
    }

    SECTION("a run that fills the width is marked as reaching the edge") {
        Canvas canvas(8 * 4, 8);
        paint_text(canvas, acorn(), 0, 0, "FULL");
        const BandReading reading = read_band(
            bitmap_band(0, 8), {0, 0, canvas.width, canvas.height},
            Search::Aligned, bitmap_sources(canvas, sets));

        REQUIRE(reading.runs.size() == 1);
        REQUIRE(reading.runs[0].reached_right_edge);
    }

    SECTION("a ten-scanline pitch reads its eight-scanline glyphs") {
        // MODE 3 and MODE 6: the glyph is eight tall on a ten-tall pitch, the
        // two spare lines blanked. Sampling the gap would break the match.
        Canvas canvas(8 * 6, 20);
        paint_text(canvas, acorn(), 0, 0, "MODE6");
        const BandReading reading = read_band(
            bitmap_band(0, 20, /*row_pitch=*/10),
            {0, 0, canvas.width, canvas.height}, Search::Aligned,
            bitmap_sources(canvas, sets));

        REQUIRE(reading.runs.size() == 1);
        REQUIRE(reading.runs[0].text == "MODE6");
    }
}

TEST_CASE("Indentation survives a bitmap read, as it does in MODE 7",
          "[screen-text]") {
    // A table of right-aligned figures, as a BASIC program prints it. The
    // leading spaces put the columns under one another; without them each row
    // left-justifies and the table stops lining up, even though every glyph was
    // read correctly. MODE 7 has always kept them, because the teletext reader
    // reports a whole row; a bitmap read must agree.
    const std::vector<screentext::GlyphSet> sets{acorn()};

    Canvas canvas(8 * 16, 8 * 2);
    paint_text(canvas, acorn(), 8 * 3, 0, "12   16");
    paint_text(canvas, acorn(), 8 * 4, 8, "9   17");

    const BandReading reading = read_band(
        bitmap_band(0, 16), {0, 0, canvas.width, canvas.height},
        Search::Aligned, bitmap_sources(canvas, sets));

    REQUIRE(reading.runs.size() == 2);
    CHECK(reading.runs[0].text == "   12   16");
    CHECK(reading.runs[1].text == "    9   17");

    // One character per cell still holds, which the client relies on to map a
    // character back to the cell it came from.
    CHECK(reading.runs[0].cells.size() == reading.runs[0].text.size());

    // And the whole thing linearises with its shape intact.
    const Reading joined =
        concatenate_bands_readings({reading}, Layout::Rows);
    CHECK(joined.text == "   12   16\n    9   17");
}

TEST_CASE("The bitmap strategy declines a glyph it was not given, never guesses",
          "[screen-text]") {
    // A shape in no supplied set: three set bits that spell nothing. Supplied,
    // it reads; unsupplied, the cell is unreadable rather than a wrong guess.
    const screentext::Bitmap shape = screentext::Bitmap::from_rows(
        {0x18, 0x24, 0x42, 0x99, 0x99, 0x42, 0x24, 0x18});

    Canvas canvas(8 * 3, 8);
    stamp(canvas, 0, 0, shape);

    SECTION("unsupplied, it is counted unreadable and copies as a space") {
        const std::vector<screentext::GlyphSet> sets{acorn()};
        const BandReading reading = read_band(
            bitmap_band(0, 8), {0, 0, canvas.width, canvas.height},
            Search::Aligned, bitmap_sources(canvas, sets));

        REQUIRE(reading.supported);
        REQUIRE(reading.unreadable_cells == 1);
        // Not mistaken for any ROM glyph.
        REQUIRE(reading.runs.size() == 1);
        REQUIRE(reading.runs[0].text[0] == ' ');
    }

    SECTION("supplied as a redefined glyph, it reads") {
        screentext::GlyphSet soft;
        soft.name = "soft-font";
        soft.glyphs.emplace_back(U'\xA9', shape); // arbitrary codepoint
        const std::vector<screentext::GlyphSet> sets{acorn(), soft};

        const BandReading reading = read_band(
            bitmap_band(0, 8), {0, 0, canvas.width, canvas.height},
            Search::Aligned, bitmap_sources(canvas, sets));

        REQUIRE(reading.unreadable_cells == 0);
        REQUIRE(reading.runs.size() == 1);
        REQUIRE(reading.runs[0].text == "\xC2\xA9"); // U+00A9, UTF-8
    }
}

TEST_CASE("Anywhere reads what Aligned does and the off-grid text besides",
          "[screen-text]") {
    // Grid text at the origin, and VDU 5-style text a few pixels off it.
    Canvas canvas(8 * 10, 8 * 4);
    paint_text(canvas, acorn(), 0, 0, "GRID");
    paint_text(canvas, acorn(), 3, 19, "AWAY");

    const std::vector<screentext::GlyphSet> sets{acorn()};
    const PixelRect whole{0, 0, canvas.width, canvas.height};

    const BandReading aligned = read_band(
        bitmap_band(0, canvas.height), whole, Search::Aligned,
        bitmap_sources(canvas, sets));
    const BandReading anywhere = read_band(
        bitmap_band(0, canvas.height), whole, Search::Anywhere,
        bitmap_sources(canvas, sets));

    const Reading aligned_reading =
        concatenate_bands_readings({aligned}, Layout::Rows);
    const Reading anywhere_reading =
        concatenate_bands_readings({anywhere}, Layout::Rows);

    // Aligned finds the grid text and not the off-grid text.
    REQUIRE(aligned_reading.text.find("GRID") != std::string::npos);
    REQUIRE(aligned_reading.text.find("AWAY") == std::string::npos);

    // Anywhere is a strict superset: both.
    REQUIRE(anywhere_reading.text.find("GRID") != std::string::npos);
    REQUIRE(anywhere_reading.text.find("AWAY") != std::string::npos);

    // The off-grid run carries no cell geometry to snap to.
    bool found_offset_run = false;
    for (const TextRun& run : anywhere_reading.runs) {
        if (run.text == "AWAY") {
            found_offset_run = true;
            REQUIRE(run.cell_width == 0);
            REQUIRE(run.cell_height == 0);
        }
    }
    REQUIRE(found_offset_run);
}

TEST_CASE("The soft font is read from the VDU driver's font workspace",
          "[screen-text]") {
    std::vector<uint8_t> memory(0x10000, 0);
    auto peek = [&memory](uint16_t address) { return memory[address]; };

    // The imploded default: zones 4-7 (characters 128-255) are soft, all four
    // pointing at page $0C, so those codes alias the single $0C00 block.
    memory[0x0367] = 0x0F;
    for (int zone = 0; zone < 7; ++zone) {
        memory[0x0368 + zone] = 0x0C;
    }

    SECTION("a redefined character in RAM becomes an overriding glyph") {
        // Character 128 is zone 4, at $0C00 + (128 & 31) * 8 = $0C00.
        const std::vector<uint8_t> rows{0x81, 0x42, 0x24, 0x18,
                                        0x18, 0x24, 0x42, 0x81};
        for (int i = 0; i < 8; ++i) {
            memory[0x0C00 + i] = rows[i];
        }

        const screentext::GlyphSet soft = read_soft_font(peek);

        // One glyph, not four: codes 128, 160, 192 and 224 share the $0C00
        // address, so it is emitted once. The pixels cannot say which alias was
        // printed, so the highest -- 224, the range the User Guide teaches --
        // is taken.
        REQUIRE(soft.glyphs.size() == 1);
        REQUIRE(soft.glyphs[0].codepoint == 0xE0);
        REQUIRE(soft.glyphs[0].bitmap == screentext::Bitmap::from_rows(rows));
    }

    SECTION("a character whose zone is in ROM is left to the base font") {
        // 'A' (65) is in zone 2, whose soft bit is clear in the imploded flags,
        // so it is never emitted even though it sits below the RAM ranges.
        for (int i = 0; i < 8; ++i) {
            memory[0x0C00 + i] = 0xFF;
        }
        const screentext::GlyphSet soft = read_soft_font(peek);
        for (const screentext::Glyph& glyph : soft.glyphs) {
            REQUIRE(glyph.codepoint != U'A');
        }
    }

    SECTION("a blank redefinition is declined, not emitted as a space") {
        const screentext::GlyphSet soft = read_soft_font(peek);
        REQUIRE(soft.glyphs.empty());
    }

    SECTION("assemble gates the soft font on recognising the MOS") {
        // Paint the ROM font at $C000 so the machine is recognised as the MOS
        // family whose workspace we know, then redefine character 128.
        const screentext::GlyphSet& acorn_set =
            screentext::builtin_glyph_set("acorn-mos-1.20");
        for (const screentext::Glyph& glyph : acorn_set.glyphs) {
            const uint8_t code =
                glyph.codepoint == 0x00A3 ? 0x60
                                          : static_cast<uint8_t>(glyph.codepoint);
            const uint16_t address = 0xC000 + (code - 0x20) * 8;
            for (int i = 0; i < 8; ++i) {
                memory[address + i] = glyph.bitmap.bytes()[i];
            }
        }
        for (int i = 0; i < 8; ++i) {
            memory[0x0C00 + i] = 0x3C;
        }

        const std::vector<screentext::GlyphSet> recognised =
            assemble_glyph_sets(peek);
        REQUIRE(recognised.size() == 2);
        REQUIRE(recognised[1].name == "soft-font");
        REQUIRE(recognised[1].glyphs.size() == 1);

        // Corrupt one probe byte: the MOS is no longer recognised, so the soft
        // font is declined and only the base set stands.
        memory[0xC000 + (0x41 - 0x20) * 8] ^= 0xFF;
        const std::vector<screentext::GlyphSet> unrecognised =
            assemble_glyph_sets(peek);
        REQUIRE(unrecognised.size() == 1);
    }

    SECTION("code 96 in an exploded zone carries the pound sign") {
        // Fully explode: every zone soft. Zone 3 (96-127) moves to RAM; code
        // 96 is the Acorn font's pound sign, so that is the text it carries.
        memory[0x0367] = 0x7F;
        memory[0x036A] = 0x1A; // zone 3 page, arbitrary RAM
        const uint16_t base = 0x1A00 + ((0x60 & 0x1F) << 3);
        for (int i = 0; i < 8; ++i) {
            memory[base + i] = static_cast<uint8_t>(0x10 + i);
        }
        const screentext::GlyphSet soft = read_soft_font(peek);

        bool found = false;
        for (const screentext::Glyph& glyph : soft.glyphs) {
            if (glyph.codepoint == 0x00A3) {
                found = true;
            }
        }
        REQUIRE(found);
    }
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
        meta.regions.push_back({0, 500, 640, 20, true});

        const std::vector<Band> bands = bands_of(meta);

        REQUIRE(bands.size() == 1);
        REQUIRE(bands[0].is_teletext);
        REQUIRE(bands[0].cell_width == 16);
        REQUIRE(bands[0].column_pitch == 16);
        REQUIRE(bands[0].row_pitch == 20);
        REQUIRE(bands[0].cell_height == 20);
        // 25 whole rows, snapped to the grid rather than the raw end_line.
        REQUIRE(bands[0].bottom - bands[0].top == 20 * 25);
    }

    SECTION("a teletext band derives to 25 rows despite capture jitter") {
        // A captured frame whose scanline count is not a clean multiple of the
        // 25-row grid -- interlace jitter, or a frame grabbed mid-reprogram
        // during boot -- must still report 25 rows. Height 520 (one extra
        // teletext row's worth of scanlines over the 500-scanline grid)
        // previously made a client deriving floor((bottom-top)/row_pitch)
        // read 26.
        FrameMetadata meta;
        meta.height = 520;
        meta.interlaced = true;
        meta.regions.push_back({0, 520, 640, 20, true});

        const std::vector<Band> bands = bands_of(meta);

        REQUIRE(bands.size() == 1);
        REQUIRE(bands[0].is_teletext);
        const uint32_t rows =
            (bands[0].bottom - bands[0].top) / bands[0].row_pitch;
        CHECK(rows == 25);
    }

    SECTION("a frame with no regions has no bands") {
        FrameMetadata meta;
        REQUIRE(bands_of(meta).empty());
    }
}
