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

// test_teletext_band_geometry.cpp
//
// Where a teletext band's character rows lie on the picture. The SAA5050 ends a
// character row every ten displayed lines counted from VSYNC, whatever the Video
// ULA shows, so a teletext band below a bitmap band need not begin on a row
// boundary: below 128 bitmap lines (Ant Attack's playing screen) the chip is on
// line 8 of a row, the band opens with that row's last two lines, and the first
// whole row starts two lines down. The band's grid must sit where the chip put
// its rows -- a selection over a line of text must read that line -- rather than
// be assumed to start at the band's top.
//
// The frames are built from synthetic 6845 outputs driven through the real
// VideoRenderer, FrameRenderer, SAA5050 capture and ScreenText, so no ROM or
// disc is needed.

#include <catch2/catch_test_macros.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/ModelBHardware.hpp>
#include <beebium/ScreenText.hpp>
#include <beebium/TeletextGrid.hpp>
#include <beebium/VideoRenderer.hpp>
#include <beebium/devices/Crtc6845.hpp>

#include <cstdint>
#include <string>

using namespace beebium;

namespace {

constexpr int COLUMNS = 40;
constexpr int ROW_LINES = 10;
constexpr uint8_t ULA_MODE4 = 0x88;     // bitmap, 1MHz, 40 columns
constexpr uint8_t ULA_TELETEXT = 0x4B;  // MODE 7

// A machine's video path without a CPU: the test plays the 6845's part.
struct VideoRig {
    ModelBHardware hardware;
    VideoRenderer<ModelBHardware> renderer{hardware};
    HeapFrameAllocator allocator;
    FrameBuffer framebuffer{&allocator, 640, 512};
    FrameRenderer frame_renderer{&framebuffer};
    TeletextGrid grid;

    VideoRig() {
        hardware.enable_video_output(1 << 16);
        hardware.saa5050.set_teletext_grid(&grid);
    }

    void clock(bool vsync, bool hsync, bool display, uint16_t address, uint8_t raster) {
        Crtc6845::Output output{};
        output.vsync = vsync;
        output.hsync = hsync;
        output.display = display;
        output.address = address;
        output.raster = raster;
        renderer.render(output);
        frame_renderer.process(*hardware.video_output, 1 << 16);
    }

    // One scan line: a little blanking, `COLUMNS` displayed characters from
    // `address`, more blanking, then the horizontal sync.
    void line(bool vsync, bool display, uint16_t address, uint8_t raster) {
        for (int i = 0; i < 4; ++i) clock(vsync, false, false, 0, raster);
        for (int c = 0; c < COLUMNS; ++c) {
            clock(vsync, false, display, static_cast<uint16_t>(address + c), raster);
        }
        for (int i = 0; i < 4; ++i) clock(vsync, false, false, 0, raster);
        for (int i = 0; i < 4; ++i) clock(vsync, true, false, 0, raster);
    }

    void vertical_sync() {
        for (int i = 0; i < 3; ++i) line(false, false, 0, 0);
        for (int i = 0; i < 2; ++i) line(true, false, 0, 0);
        for (int i = 0; i < 3; ++i) line(false, false, 0, 0);
    }

    void put_text(int memory_row, const std::string& text) {
        for (size_t i = 0; i < text.size(); ++i) {
            hardware.write(static_cast<uint16_t>(0x7C00 + memory_row * COLUMNS + i),
                           static_cast<uint8_t>(text[i]));
        }
    }

    // A frame of `bitmap_lines` of MODE 4 above `teletext_lines` of MODE 7.
    // The teletext band's memory rows follow the chip's character rows, as a
    // program that splits the screen arranges: memory row n is drawn on the
    // chip's n-th character row of the band, counting the partial row the
    // band opens with as row 0.
    void split_frame(int bitmap_lines, int teletext_lines) {
        vertical_sync();
        hardware.video_ula.write(0, ULA_MODE4);
        for (int y = 0; y < bitmap_lines; ++y) {
            line(false, true, 0x0600, static_cast<uint8_t>(y % 8));
        }
        hardware.video_ula.write(0, ULA_TELETEXT);
        const int phase = bitmap_lines % ROW_LINES;
        for (int k = 0; k < teletext_lines; ++k) {
            const int chip_line = (phase + k) % ROW_LINES;
            const int memory_row = (phase + k) / ROW_LINES;
            line(false, true, static_cast<uint16_t>(0x2000 + memory_row * COLUMNS),
                 static_cast<uint8_t>(chip_line));
        }
    }

    void run_split_frames(int bitmap_lines, int teletext_lines) {
        for (int frame = 0; frame < 3; ++frame) {
            split_frame(bitmap_lines, teletext_lines);
        }
        vertical_sync();
    }

    std::string read(const screen::Band& band, screen::PixelRect region) {
        const TeletextGrid::Snapshot snapshot = grid.snapshot();
        screen::BandSources sources;
        sources.teletext = &snapshot;
        return screen::concatenate_bands_readings(
                   {screen::read_band(band, region, screen::Search::Aligned, sources)},
                   screen::Layout::Rows)
            .text;
    }
};

const screen::Band* teletext_band(const std::vector<screen::Band>& bands) {
    for (const auto& band : bands) {
        if (band.is_teletext) return &band;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("A teletext band below a bitmap band starts where the chip's rows are",
          "[teletext][screen-text][split-screen]") {
    VideoRig rig;
    rig.put_text(0, "PARTIAL");  // only its last two lines are shown
    rig.put_text(1, "SCORE :00000");
    rig.put_text(2, "AMMO BOY GIRL");
    rig.run_split_frames(128, 62);

    const FrameMetadata metadata = rig.framebuffer.metadata();
    const TeletextGrid::Snapshot snapshot = rig.grid.snapshot();
    REQUIRE(snapshot.active);
    REQUIRE(snapshot.row_origin.has_value());

    const auto bands = screen::bands_of(metadata, snapshot.row_origin);
    const screen::Band* band = teletext_band(bands);
    REQUIRE(band != nullptr);
    REQUIRE(band->top == 128);

    // 128 displayed lines leave the chip on line 8: the band opens with the
    // last two lines of a row that began on line 120, so the grid's origin is
    // there, above the band, and the first whole row (SCORE) begins on 130.
    CHECK(band->row_pitch == ROW_LINES);
    CHECK(band->origin_y == 120);
    CHECK(band->first_grid_row == 0);

    // A selection over the pixels of the SCORE line reads the SCORE line,
    // however little of it is dragged over.
    CHECK(rig.read(*band, {0, 130, 640, 10}) == "SCORE :00000");
    CHECK(rig.read(*band, {0, 133, 640, 3}) == "SCORE :00000");
    CHECK(rig.read(*band, {0, 141, 640, 5}) == "AMMO BOY GIRL");

    // The partial row is part of the band: its characters are on screen, only
    // their last two lines showing, and a selection takes a partly covered
    // cell whole. So a copy of the whole band starts with it, and a selection
    // of just its two lines reads it.
    CHECK(rig.read(*band, {0, 128, 640, 62}) == "PARTIAL\nSCORE :00000\nAMMO BOY GIRL");
    CHECK(rig.read(*band, {0, 128, 640, 2}) == "PARTIAL");
}

TEST_CASE("A whole teletext screen keeps its grid at the top of the picture",
          "[teletext][screen-text]") {
    VideoRig rig;
    rig.put_text(0, "TOP ROW");
    rig.put_text(1, "SECOND ROW");
    rig.run_split_frames(0, 250);

    const FrameMetadata metadata = rig.framebuffer.metadata();
    const TeletextGrid::Snapshot snapshot = rig.grid.snapshot();
    REQUIRE(snapshot.row_origin.has_value());
    CHECK(*snapshot.row_origin == 0);

    const auto bands = screen::bands_of(metadata, snapshot.row_origin);
    const screen::Band* band = teletext_band(bands);
    REQUIRE(band != nullptr);
    CHECK(band->origin_y == 0);
    CHECK(band->first_grid_row == 0);
    CHECK(rig.read(*band, {0, 12, 640, 4}) == "SECOND ROW");
}
