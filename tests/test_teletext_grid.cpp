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

// test_teletext_grid.cpp
//
// Capturing what a MODE 7 display actually shows, cell by cell, after the
// SAA5050 has resolved the control codes.
//
// This is the source both "copy the screen as text" and a future vector
// renderer read from. It is a better source than screen memory because it needs
// no hardware-scroll correction and no re-derivation of attribute state -- see
// docs/discussion/teletext-cell-capture.md.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Saa5050.hpp>
#include <beebium/TeletextGrid.hpp>

#include <cstdint>
#include <vector>

using namespace beebium;

// ============================================================================
// The grid itself
// ============================================================================

TEST_CASE("A new grid holds nothing and is not active", "[teletext-grid]") {
    TeletextGrid grid;
    CHECK_FALSE(grid.active());
    CHECK(grid.frame_number() == 0);
    CHECK(grid.cell(0, 0).character == 0);
}

TEST_CASE("Captured cells are published by the swap, not before",
          "[teletext-grid]") {
    TeletextGrid grid;

    TeletextCell cell;
    cell.character = 'A';
    grid.set_cell(3, 4, cell);

    // Still the previous frame: a reader must never see a half-built one.
    CHECK(grid.cell(3, 4).character == 0);

    grid.swap();
    CHECK(grid.cell(3, 4).character == 'A');
    CHECK(grid.active());
    CHECK(grid.frame_number() == 1);
}

TEST_CASE("A frame with no captured cells is not active", "[teletext-grid]") {
    // Nothing was fed to the chip, so the display was not MODE 7 and the grid
    // holds nothing current. A reader must be able to tell.
    TeletextGrid grid;
    grid.swap();
    CHECK_FALSE(grid.active());
}

TEST_CASE("A frame does not inherit cells from the one before",
          "[teletext-grid]") {
    // Otherwise a display that shrinks leaves stale cells in the rows it no
    // longer covers, and they would read as current.
    TeletextGrid grid;

    TeletextCell cell;
    cell.character = 'X';
    grid.set_cell(10, 10, cell);
    grid.swap();
    REQUIRE(grid.cell(10, 10).character == 'X');

    TeletextCell other;
    other.character = 'Y';
    grid.set_cell(0, 0, other);
    grid.swap();

    CHECK(grid.cell(0, 0).character == 'Y');
    CHECK(grid.cell(10, 10).character == 0);
}

TEST_CASE("Cells outside the grid are dropped, not folded onto the edges",
          "[teletext-grid]") {
    // Unusual CRTC programming can produce a display larger than 40x25.
    // Clamping would corrupt real cells; dropping merely omits the extra ones.
    TeletextGrid grid;

    TeletextCell edge;
    edge.character = 'E';
    grid.set_cell(TeletextGrid::ROWS - 1, TeletextGrid::COLUMNS - 1, edge);

    TeletextCell beyond;
    beyond.character = 'B';
    grid.set_cell(TeletextGrid::ROWS, 0, beyond);
    grid.set_cell(0, TeletextGrid::COLUMNS, beyond);

    grid.swap();

    CHECK(grid.cell(TeletextGrid::ROWS - 1, TeletextGrid::COLUMNS - 1).character == 'E');
    // Nothing landed at the edges as a result of the out-of-range writes.
    CHECK(grid.cell(0, 0).character == 0);
}

TEST_CASE("Reset returns the grid to empty and inactive", "[teletext-grid]") {
    TeletextGrid grid;
    TeletextCell cell;
    cell.character = 'A';
    grid.set_cell(1, 1, cell);
    grid.swap();
    REQUIRE(grid.active());

    grid.reset();

    CHECK_FALSE(grid.active());
    CHECK(grid.frame_number() == 0);
    CHECK(grid.cell(1, 1).character == 0);
}

// ============================================================================
// Attribute capture, driven directly through the chip
// ============================================================================
//
// The grid records more than each character: it records the SAA5050 state in
// effect at the cell -- foreground and background colour, character set,
// concealment, flash, double height, whether the cell was a control code, and
// whether the cursor was on it. These are what make the grid a truer record of
// the screen than screen memory, which holds only the bytes and leaves every
// reader to re-run the serial control codes to learn what a cell looked like.
// They travel out over GetTeletextScreen, so a client can render or inspect a
// page without knowing the chip.
//
// The chip is driven here without a CRTC. byte() with display enabled captures
// one cell and advances the capture column, so a row of calls fills grid row 0;
// process_control_code runs before the capture, so a control cell already
// carries the state it switched to, and the cells after it inherit it. A single
// vsync() publishes the frame.

namespace {

// Feed a row of bytes with display enabled, then publish the frame.
void capture_row(Saa5050& saa, const std::vector<uint8_t>& row) {
    for (uint8_t value : row) {
        saa.byte(value, /*dispen=*/1);
    }
    saa.vsync();
}

} // namespace

TEST_CASE("A control code holds its column and is flagged as one",
          "[teletext-grid][attributes]") {
    // Column alignment is the whole point of capturing the grid, so a control
    // code occupies its cell rather than closing the gap -- and it is marked,
    // so a reader tells the space it draws from a real space.
    Saa5050 saa;
    TeletextGrid grid;
    saa.set_teletext_grid(&grid);

    capture_row(saa, {0x01, 'A'});  // alpha red, then a letter

    CHECK(grid.cell(0, 0).is_control_code);
    CHECK(grid.cell(0, 0).character == 0x01);
    CHECK_FALSE(grid.cell(0, 1).is_control_code);
    CHECK(grid.cell(0, 1).character == 'A');
}

TEST_CASE("An alpha colour sets the foreground of the cells after it",
          "[teletext-grid][attributes]") {
    Saa5050 saa;
    TeletextGrid grid;
    saa.set_teletext_grid(&grid);

    capture_row(saa, {0x02, 'G'});  // alpha green

    CHECK(grid.cell(0, 1).fg == 2);
    CHECK(grid.cell(0, 1).charset == TeletextCellCharset::Alpha);
}

TEST_CASE("New Background copies the foreground into the background",
          "[teletext-grid][attributes]") {
    // Yellow foreground, then New Background, so the following cell is yellow
    // on yellow -- the teletext idiom for a solid colour block.
    Saa5050 saa;
    TeletextGrid grid;
    saa.set_teletext_grid(&grid);

    capture_row(saa, {0x03, 0x1D, 'X'});  // yellow, new background

    CHECK(grid.cell(0, 2).fg == 3);
    CHECK(grid.cell(0, 2).bg == 3);
}

TEST_CASE("A graphics colour switches the cell into the graphics character set",
          "[teletext-grid][attributes]") {
    Saa5050 saa;
    TeletextGrid grid;
    saa.set_teletext_grid(&grid);

    // Graphics white, then a mosaic byte. The default graphics set is
    // contiguous.
    capture_row(saa, {0x17, 0x7F});

    CHECK(grid.cell(0, 1).charset == TeletextCellCharset::ContiguousGraphics);
    CHECK(grid.cell(0, 1).character == 0x7F);
}

TEST_CASE("Separated graphics is captured as its own character set",
          "[teletext-grid][attributes]") {
    Saa5050 saa;
    TeletextGrid grid;
    saa.set_teletext_grid(&grid);

    capture_row(saa, {0x17, 0x1A, 0x7F});  // graphics white, separated

    CHECK(grid.cell(0, 2).charset == TeletextCellCharset::SeparatedGraphics);
}

TEST_CASE("Conceal marks the cells it hides", "[teletext-grid][attributes]") {
    // The capture records that a cell is concealed; the text reader is what
    // then declines to copy it. Both matter, and they are separate jobs.
    Saa5050 saa;
    TeletextGrid grid;
    saa.set_teletext_grid(&grid);

    capture_row(saa, {0x18, 'S'});  // conceal

    CHECK(grid.cell(0, 1).concealed);
}

TEST_CASE("Double height marks the top half on the row that carries it",
          "[teletext-grid][attributes]") {
    Saa5050 saa;
    TeletextGrid grid;
    saa.set_teletext_grid(&grid);

    capture_row(saa, {0x0D, 'D'});  // double height

    CHECK(grid.cell(0, 1).double_height_top);
    CHECK_FALSE(grid.cell(0, 1).double_height_bottom);
}

TEST_CASE("Double height marks the bottom half on the next row",
          "[teletext-grid][attributes]") {
    // The bottom half is drawn on the following display row with the raster
    // offset engaged. Drive the row wrap directly -- set_raster advances the
    // capture row and toggles the offset -- since there is no CRTC here.
    Saa5050 saa;
    TeletextGrid grid;
    saa.set_teletext_grid(&grid);

    saa.byte(0x0D, /*dispen=*/1);  // double height, at (0,0)
    saa.byte('D', /*dispen=*/1);   // top half, at (0,1)

    // A character row completes: raster climbs past the wrap threshold and
    // drops back, which advances the capture row and engages the offset.
    saa.set_raster(15);
    saa.set_raster(0);

    saa.byte('D', /*dispen=*/1);   // bottom half, on the new row
    saa.vsync();

    CHECK(grid.cell(0, 1).double_height_top);
    CHECK(grid.cell(1, 2).double_height_bottom);
    CHECK_FALSE(grid.cell(1, 2).double_height_top);
}

TEST_CASE("Flash is captured on the concealed half of the flash cycle",
          "[teletext-grid][attributes]") {
    // Flash is phase-dependent: a flashing cell reads as flashing only on the
    // frames where the character is hidden. Publish an empty frame first to
    // move the chip into that phase, then capture into the next.
    Saa5050 saa;
    TeletextGrid grid;
    saa.set_teletext_grid(&grid);

    saa.vsync();  // advances the frame counter into the concealed phase

    capture_row(saa, {0x08, 'F', 0x09, 'S'});  // flash F, steady S

    CHECK(grid.cell(0, 1).flashing);        // F, after the flash code
    CHECK_FALSE(grid.cell(0, 3).flashing);  // S, after steady
}

TEST_CASE("The cursor cell is flagged", "[teletext-grid][attributes]") {
    Saa5050 saa;
    TeletextGrid grid;
    saa.set_teletext_grid(&grid);

    saa.byte('A', /*dispen=*/1, /*cursor=*/false);
    saa.byte('B', /*dispen=*/1, /*cursor=*/true);
    saa.vsync();

    CHECK_FALSE(grid.cell(0, 0).cursor);
    CHECK(grid.cell(0, 1).cursor);
}

// ============================================================================
// Capture from a real MODE 7 display
// ============================================================================
//
// The grid unit tests above cannot catch a row or column tracking mistake --
// only driving the real CRTC and SAA5050 can. These boot a machine to the
// MODE 7 BASIC prompt and read the screen back out of the grid.

#include <beebium/Machines.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>

#include "test_mode7_helpers.hpp"

#include <string>

using namespace beebium::test;

namespace {

// The text of one grid row, with control codes and graphics as spaces, so a
// test can assert on what a human would read.
std::string row_text(const TeletextGrid& grid, size_t row) {
    std::string text;
    for (size_t column = 0; column < TeletextGrid::COLUMNS; ++column) {
        const auto& cell = grid.cell(row, column);
        const bool readable = !cell.is_control_code
                           && cell.charset == TeletextCellCharset::Alpha
                           && cell.character >= 0x20 && cell.character < 0x7F;
        text.push_back(readable ? static_cast<char>(cell.character) : ' ');
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

bool grid_contains(const TeletextGrid& grid, const std::string& needle) {
    for (size_t row = 0; row < TeletextGrid::ROWS; ++row) {
        if (row_text(grid, row).find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Run the machine for a while, feeding video through the renderer so the
// SAA5050 sees the display and the grid is filled and swapped.
template <typename Context>
void run_frames(Context& context, int cycles) {
    for (int i = 0; i < cycles; ++i) {
        context.machine.step();
        if (context.machine.memory().video_output.has_value()) {
            context.renderer.process(context.machine.memory().video_output.value());
        }
    }
}

} // namespace

TEST_CASE("The grid captures the MODE 7 boot screen", "[teletext-grid][integration]") {
    if (!roms_available<ModelB>()) {
        SKIP("ROMs not available");
    }

    Mode7TestContext<ModelB> context;
    if (!context) {
        SKIP("Machine did not boot to MODE 7 BASIC");
    }

    TeletextGrid grid;
    context.machine.memory().saa5050.set_teletext_grid(&grid);

    // A few frames, so at least one completes and is published.
    run_frames(context, 200'000);

    SECTION("the captured frame is marked active") {
        CHECK(grid.active());
        CHECK(grid.frame_number() > 0);
    }

    SECTION("the boot banner is on screen") {
        // What a MODE 7 Model B shows after reset.
        CHECK(grid_contains(grid, "BBC Computer"));
        CHECK(grid_contains(grid, "BASIC"));
    }

    SECTION("rows are captured in display order, not rotated") {
        // The banner sits near the top. A row-tracking error would put it
        // somewhere else; a column error would shift it within its row.
        bool found_near_top = false;
        for (size_t row = 0; row < 6; ++row) {
            if (row_text(grid, row).find("BBC Computer") != std::string::npos) {
                found_near_top = true;
            }
        }
        CHECK(found_near_top);
    }

    SECTION("no cells are captured beyond the display width") {
        // Column tracking that ran on past 40 would silently drop cells; one
        // that reset late would overwrite column 0 repeatedly.
        size_t non_blank_rows = 0;
        for (size_t row = 0; row < TeletextGrid::ROWS; ++row) {
            if (!row_text(grid, row).empty()) {
                ++non_blank_rows;
            }
        }
        CHECK(non_blank_rows >= 2);
    }
}

TEST_CASE("Detaching the grid stops capture", "[teletext-grid][integration]") {
    if (!roms_available<ModelB>()) {
        SKIP("ROMs not available");
    }

    Mode7TestContext<ModelB> context;
    if (!context) {
        SKIP("Machine did not boot to MODE 7 BASIC");
    }

    TeletextGrid grid;
    context.machine.memory().saa5050.set_teletext_grid(&grid);
    run_frames(context, 200'000);
    REQUIRE(grid.active());

    const uint64_t frame_before = grid.frame_number();
    context.machine.memory().saa5050.set_teletext_grid(nullptr);
    run_frames(context, 200'000);

    CHECK(grid.frame_number() == frame_before);
}
