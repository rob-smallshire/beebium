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

// The screen text calls' answers as the client uses them (client
// specification 6.5): a hold's grid and still, a reading's runs and cells,
// and the requests each interpretation sends.

#include "connection/ScreenReading.hpp"

#include "video.pb.h"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

void set_region(beebium::PixelRegion* region, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                std::uint32_t height) {
    region->set_x(x);
    region->set_y(y);
    region->set_width(width);
    region->set_height(height);
}

beebium::ScreenGeometry wire_geometry() {
    beebium::ScreenGeometry wire;
    wire.set_frame_number(7);
    auto* band = wire.add_bands();
    band->set_top(0);
    band->set_bottom(250);
    band->set_cell_width(8);
    band->set_cell_height(8);
    band->set_column_pitch(8);
    band->set_row_pitch(10);
    band->set_origin_x(0);
    band->set_origin_y(0);
    return wire;
}

}  // namespace

TEST_CASE("A grid keeps every band's cell and pitch", "[connection][screen-text]") {
    const ScreenGrid grid = screen_grid(wire_geometry());
    CHECK(grid.frame_number == 7);
    REQUIRE(grid.bands.size() == 1);
    const ScreenBand& band = grid.bands[0];
    CHECK(band.top == 0);
    CHECK(band.bottom == 250);
    CHECK(band.cell_width == 8);
    CHECK(band.cell_height == 8);
    CHECK(band.column_pitch == 8);
    CHECK(band.row_pitch == 10);
}

TEST_CASE("A reading keeps its runs, their cells and the counts", "[connection][screen-text]") {
    beebium::ScreenText wire;
    wire.set_supported(true);
    wire.set_text("HI");
    wire.set_unreadable_cells(1);
    wire.set_ambiguous_cells(2);
    wire.set_frame_number(9);
    auto* run = wire.add_runs();
    run->set_text("HI?");
    set_region(run->mutable_bounds(), 16, 20, 24, 8);
    run->set_cell_width(8);
    run->set_cell_height(8);
    for (std::uint32_t i = 0; i < 3; ++i) {
        auto* cell = run->add_cells();
        set_region(cell->mutable_bounds(), 16 + 8 * i, 20, 8, 8);
        cell->set_matched(i < 2);
    }

    const ScreenReading reading = screen_reading(wire);
    CHECK(reading.supported);
    CHECK(reading.text == "HI");
    CHECK(reading.unreadable_cells == 1);
    CHECK(reading.ambiguous_cells == 2);
    CHECK(reading.frame_number == 9);
    REQUIRE(reading.runs.size() == 1);
    const TextRun& read = reading.runs[0];
    CHECK(read.text == "HI?");
    CHECK(read.bounds == PixelRect{16, 20, 24, 8});
    CHECK(read.cell_width == 8);
    REQUIRE(read.cells.size() == 3);
    CHECK(read.cells[2].bounds == PixelRect{32, 20, 8, 8});
    CHECK(read.cells[1].matched);
    CHECK_FALSE(read.cells[2].matched);
}

TEST_CASE("A hold brings its id, its grid and its still", "[connection][screen-text]") {
    beebium::ScreenHold wire;
    wire.set_hold_id(41);
    *wire.mutable_geometry() = wire_geometry();
    auto* frame = wire.mutable_frame();
    frame->set_frame_number(7);
    frame->set_width(2);
    frame->set_height(1);
    frame->set_pixels(std::string(8, '\x10'));

    auto result = held_screen(wire, {});
    auto* held = std::get_if<HeldScreen>(&result);
    REQUIRE(held != nullptr);
    CHECK(held->hold_id == 41);
    CHECK(held->grid.bands.size() == 1);
    REQUIRE(held->frame);
    CHECK(held->frame->width == 2);
    CHECK(held->frame->pixels.size() == 8);
}

TEST_CASE("A hold whose still is malformed is refused", "[connection][screen-text]") {
    beebium::ScreenHold wire;
    wire.set_hold_id(41);
    auto* frame = wire.mutable_frame();
    frame->set_width(2);
    frame->set_height(1);
    frame->set_pixels(std::string(3, '\x10'));
    auto result = held_screen(wire, {});
    CHECK(std::holds_alternative<FrameDecodeError>(result));
}

TEST_CASE("A hold asked without its still has none", "[connection][screen-text]") {
    beebium::ScreenHold wire;
    wire.set_hold_id(3);
    auto result = held_screen(wire, {});
    auto* held = std::get_if<HeldScreen>(&result);
    REQUIRE(held != nullptr);
    CHECK_FALSE(held->frame);
}

TEST_CASE("Rows and columns read the grid; anywhere reads everything",
          "[connection][screen-text]") {
    CHECK(screen_text_request(Interpretation::rows, Mode7Characters::codes, {}, {}).search() ==
          beebium::SCREEN_TEXT_SEARCH_ALIGNED);
    CHECK(screen_text_request(Interpretation::columns, Mode7Characters::codes, {}, {}).search() ==
          beebium::SCREEN_TEXT_SEARCH_ALIGNED);
    CHECK(screen_text_request(Interpretation::anywhere, Mode7Characters::codes, {}, {}).search() ==
          beebium::SCREEN_TEXT_SEARCH_ANYWHERE);
}

TEST_CASE("A request carries the Mode 7 choice, the region and the hold",
          "[connection][screen-text]") {
    const auto request = screen_text_request(Interpretation::rows, Mode7Characters::displayed,
                                             PixelRect{8, 10, 320, 20}, std::uint64_t{41});
    CHECK(request.characters() == beebium::SCREEN_TEXT_CHARACTERS_DISPLAYED);
    CHECK(request.layout() == beebium::SCREEN_TEXT_LAYOUT_ROWS);
    REQUIRE(request.has_region());
    CHECK(request.region().x() == 8);
    CHECK(request.region().y() == 10);
    CHECK(request.region().width() == 320);
    CHECK(request.region().height() == 20);
    REQUIRE(request.has_hold_id());
    CHECK(request.hold_id() == 41);
}

TEST_CASE("The whole live screen is asked with no region and no hold",
          "[connection][screen-text]") {
    const auto request = screen_text_request(Interpretation::rows, Mode7Characters::codes, {}, {});
    CHECK(request.characters() == beebium::SCREEN_TEXT_CHARACTERS_CODES);
    CHECK_FALSE(request.has_region());
    CHECK_FALSE(request.has_hold_id());
}
