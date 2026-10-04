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

// test_saa5050_line_counter.cpp
//
// The SAA5050 decides for itself which of a character's ten lines it is drawing
// and when a character row ends. It never sees the 6845's row address except
// RA0, which arrives on CRS to choose between a glyph line and its rounded
// neighbour. Its line counter is cleared by DEW (the 6845's VSYNC) and advanced
// by LOSE (the 6845's DISPTMG) at the end of each displayed line.
//
// So Mode 7 draws whole glyphs however the 6845 is programmed. The standard
// mode is interlace sync and video (R8=&93, R9=18), where the row address runs
// 0,2,..18 on one field and 1,3,..19 on the other; a program that turns the
// interlace off (R8=&92, R9=9, as *TV ,1 does and as Ant Attack does for its
// title page) gets a row address of 0..9 and must still see every glyph line.

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/Saa5050.hpp>
#include <beebium/TeletextGrid.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/ModelBHardware.hpp>
#include <beebium/VideoRenderer.hpp>
#include <beebium/devices/Crtc6845.hpp>

#include "test_mode7_helpers.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace beebium;
using namespace beebium::test;

// DEW and LOSE are wired from the 6845 to the SAA5050 on the board, not routed
// through the Video ULA, so the chip keeps counting displayed lines while the
// ULA shows a bitmap. A screen split into a bitmap band above a teletext band
// (Ant Attack's playing screen) therefore starts its teletext with the count
// left by the bitmap lines, as on the real machine.
TEST_CASE("The SAA5050 counts displayed lines whatever the Video ULA shows",
          "[saa5050][mode7][split-screen]") {
    ModelBHardware hardware;
    hardware.enable_video_output();
    VideoRenderer<ModelBHardware> renderer(hardware);

    hardware.video_ula.write(0, 0x88);  // MODE 4: teletext not selected
    REQUIRE_FALSE(hardware.video_ula.teletext_mode());

    auto clock = [&](bool vsync, bool display, uint8_t raster) {
        Crtc6845::Output output{};
        output.vsync = vsync;
        output.display = display;
        output.raster = raster;
        renderer.render(output);
    };

    // A field begins at the trailing edge of VSYNC...
    for (int i = 0; i < 3; ++i) {
        clock(false, false, 0);
        clock(true, false, 0);
    }
    clock(false, false, 0);

    // ...then seven displayed lines of bitmap.
    for (uint8_t line = 0; line < 7; ++line) {
        clock(false, true, line);
        clock(false, false, line);
    }

    CHECK(hardware.saa5050.line() == 7);
}

#ifdef BEEBIUM_ROM_DIR

namespace {

constexpr uint16_t CRTC_ADDRESS = 0xFE00;
constexpr uint16_t CRTC_DATA = 0xFE01;
constexpr uint16_t MODE7_SCREEN = 0x7C00;
constexpr int MODE7_COLUMNS = 40;
constexpr int GLYPH_LINES = 10;
constexpr int FRAME_WIDTH = 640;

// Run until `count` more frames have been published.
void run_frames(Mode7TestContext<ModelB>& ctx, int count) {
    const uint64_t target = ctx.framebuffer.version() + static_cast<uint64_t>(count);
    for (uint64_t i = 0; i < static_cast<uint64_t>(count) * 200'000 &&
                         ctx.framebuffer.version() < target; ++i) {
        ctx.machine.step();
        if (ctx.machine.memory().video_output.has_value()) {
            ctx.renderer.process(ctx.machine.memory().video_output.value());
        }
    }
    REQUIRE(ctx.framebuffer.version() >= target);
}

struct CapturedFrame {
    std::vector<uint32_t> pixels;
    size_t stride = 0;
    FrameMetadata metadata;

    const uint32_t* row(size_t y) const { return pixels.data() + y * stride; }
};

CapturedFrame capture(Mode7TestContext<ModelB>& ctx) {
    CapturedFrame frame;
    const auto published = ctx.framebuffer.read_frame();
    frame.pixels.assign(published.begin(), published.end());
    frame.stride = ctx.framebuffer.capacity_width();
    frame.metadata = ctx.framebuffer.metadata();
    return frame;
}

void put_text(Mode7TestContext<ModelB>& ctx, int row, int column, const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        ctx.machine.write(static_cast<uint16_t>(MODE7_SCREEN + row * MODE7_COLUMNS + column + i),
                          static_cast<uint8_t>(text[i]));
    }
}

void write_crtc(Mode7TestContext<ModelB>& ctx, uint8_t reg, uint8_t value) {
    ctx.machine.write(CRTC_ADDRESS, reg);
    ctx.machine.write(CRTC_DATA, value);
}

// Ant Attack's title page: Mode 7 with the interlace turned off.
void turn_interlace_off(Mode7TestContext<ModelB>& ctx) {
    write_crtc(ctx, 8, 0x92);  // normal sync, one character display delay
    write_crtc(ctx, 9, 9);     // ten scan lines per character row
}

// Text with descenders, a line of capitals, and a double-height pair, on
// rows clear of the boot messages and the cursor.
constexpr int GIRL_OR_BOY_ROW = 10;
constexpr int DESCENDER_ROW = 11;
constexpr int DOUBLE_TOP_ROW = 13;
constexpr int DOUBLE_BOTTOM_ROW = 14;
const std::string GIRL_OR_BOY = "Girl or Boy (g/b) ?";
const std::string DESCENDERS = "jumpy quay gjpqy";
const std::string DOUBLE_TEXT = std::string(1, '\x0D') + "DOUBLE";

void put_test_page(Mode7TestContext<ModelB>& ctx) {
    put_text(ctx, GIRL_OR_BOY_ROW, 2, GIRL_OR_BOY);
    put_text(ctx, DESCENDER_ROW, 2, DESCENDERS);
    put_text(ctx, DOUBLE_TOP_ROW, 2, DOUBLE_TEXT);
    put_text(ctx, DOUBLE_BOTTOM_ROW, 2, DOUBLE_TEXT);
}

}  // namespace

TEST_CASE("Mode 7 draws whole glyphs with the interlace turned off",
          "[saa5050][mode7][interlace]") {
    Mode7TestContext<ModelB> ctx;
    REQUIRE(ctx.booted);

    put_test_page(ctx);
    run_frames(ctx, 4);
    const CapturedFrame interlaced = capture(ctx);
    REQUIRE(interlaced.metadata.interlaced);
    REQUIRE(interlaced.metadata.height == 25 * 2 * GLYPH_LINES);

    turn_interlace_off(ctx);
    run_frames(ctx, 6);
    const CapturedFrame progressive = capture(ctx);
    REQUIRE_FALSE(progressive.metadata.interlaced);
    REQUIRE(progressive.metadata.height == 25 * GLYPH_LINES);

    // In the interlaced frame, line j of a character row is drawn from glyph
    // line j of the twenty (ten font lines, each with its rounded partner).
    // With the interlace off the chip still steps through all ten font lines,
    // one per scan line, and RA0 on CRS picks the rounded partner on odd lines:
    // scan line L of the row shows glyph line 2L + (L & 1). Drawing glyph line
    // L instead -- the row address read as a glyph line -- shows only the top
    // half of every character, stretched to fill the row.
    for (int row : {GIRL_OR_BOY_ROW, DESCENDER_ROW, DOUBLE_TOP_ROW, DOUBLE_BOTTOM_ROW}) {
        for (int line = 0; line < GLYPH_LINES; ++line) {
            const size_t progressive_y = static_cast<size_t>(row * GLYPH_LINES + line);
            const size_t interlaced_y =
                static_cast<size_t>(row * 2 * GLYPH_LINES + 2 * line + (line & 1));
            INFO("character row " << row << ", scan line " << line);
            const uint32_t* drawn = progressive.row(progressive_y);
            const uint32_t* expected = interlaced.row(interlaced_y);
            int mismatched_pixels = 0;
            for (int x = 0; x < FRAME_WIDTH; ++x) {
                mismatched_pixels += drawn[x] != expected[x] ? 1 : 0;
            }
            CHECK(mismatched_pixels == 0);
        }
    }
}

TEST_CASE("Mode 7 text is captured row by row with the interlace turned off",
          "[saa5050][mode7][interlace][teletext-grid]") {
    Mode7TestContext<ModelB> ctx;
    REQUIRE(ctx.booted);

    TeletextGrid grid;
    ctx.machine.memory().saa5050.set_teletext_grid(&grid);

    put_test_page(ctx);
    turn_interlace_off(ctx);
    run_frames(ctx, 6);

    // The chip ends a character row after ten displayed lines, so the
    // captured page has all twenty-five rows and each line of text sits on
    // its own row -- not every row piled onto the first.
    REQUIRE(grid.active());
    CHECK(grid.rows() == 25);

    auto text_at = [&](int row, int column, size_t length) {
        std::string text;
        for (size_t i = 0; i < length; ++i) {
            text += static_cast<char>(grid.cell(row, column + i).character);
        }
        return text;
    };
    CHECK(text_at(GIRL_OR_BOY_ROW, 2, GIRL_OR_BOY.size()) == GIRL_OR_BOY);
    CHECK(text_at(DESCENDER_ROW, 2, DESCENDERS.size()) == DESCENDERS);

    // Double height runs over two rows: the top half on the first, the
    // bottom half on the next.
    CHECK(grid.cell(DOUBLE_TOP_ROW, 3).double_height_top);
    CHECK(grid.cell(DOUBLE_BOTTOM_ROW, 3).double_height_bottom);
}

#endif  // BEEBIUM_ROM_DIR
