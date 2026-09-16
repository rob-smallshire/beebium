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

// test_frame_renderer_display_width.cpp
//
// Tests for FrameRenderer's physical display width and borders. The frame's
// display_width is its width in 16MHz pixel clocks -- the width the client
// stretches every band's logical pixels to. A batch carries how many 16MHz
// clocks it spans (8 at a 2MHz character clock, 16 at 1MHz), which lets a
// custom-width mode like Elite's 32-column screen report 512 rather than the
// old hardcoded 640, and puts the borders on the same uniform grid.

#include <catch2/catch_test_macros.hpp>
#include <beebium/PixelBatch.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>

using namespace beebium;

namespace {

// A displayed or blanking batch of a stated mode geometry. display_clocks is
// the physical width of the batch: 8 for a 2MHz character clock, 16 for 1MHz.
PixelBatch make_batch(uint8_t flags, uint8_t pixel_count, uint8_t display_clocks) {
    PixelBatch batch{};
    batch.clear();
    batch.set_type(PixelBatchType::Bitmap);
    for (int i = 0; i < pixel_count; ++i) {
        batch.pixels.pixels[i] = bbc_colors::WHITE;
    }
    batch.set_type(PixelBatchType::Bitmap);
    batch.set_flags(flags);
    batch.set_pixel_count(pixel_count);
    batch.set_display_clocks(display_clocks);
    return batch;
}

// Emit one scanline: `blanking_batches` of left blanking, `num_batches` of
// display, one end blank, then an HSYNC pulse. Every batch carries the same
// pixel_count and display_clocks, i.e. one character clock throughout the line.
void emit_scanline(FrameRenderer& renderer, OutputQueue<PixelBatch>& queue,
                   int num_batches, uint8_t pixel_count, uint8_t display_clocks,
                   int blanking_batches = 1) {
    for (int i = 0; i < blanking_batches; ++i) {
        queue.push(make_batch(0, pixel_count, display_clocks));
        renderer.process(queue);
    }
    for (int i = 0; i < num_batches; ++i) {
        queue.push(make_batch(VIDEO_FLAG_DISPLAY, pixel_count, display_clocks));
        renderer.process(queue);
    }
    queue.push(make_batch(0, pixel_count, display_clocks));
    renderer.process(queue);

    queue.push(make_batch(VIDEO_FLAG_HSYNC, pixel_count, display_clocks));
    renderer.process(queue);
    queue.push(make_batch(0, pixel_count, display_clocks));
    renderer.process(queue);
}

void emit_vsync(FrameRenderer& renderer, OutputQueue<PixelBatch>& queue) {
    queue.push(make_batch(0, 8, 8));
    renderer.process(queue);
    queue.push(make_batch(VIDEO_FLAG_VSYNC, 8, 8));
    renderer.process(queue);
    queue.push(make_batch(0, 8, 8));
    renderer.process(queue);
}

// Character clock in 16MHz units.
constexpr uint8_t FAST = 8;   // 2MHz: Modes 0-2
constexpr uint8_t SLOW = 16;  // 1MHz: Modes 3-6

} // anonymous namespace

// ============================================================================
// Standard modes still report 640 (regression guard)
// ============================================================================

TEST_CASE("display_width: standard modes report 640", "[frame-renderer][display-width]") {
    FrameBuffer fb(nullptr, 736, 576);
    FrameRenderer renderer(&fb);
    OutputQueue<PixelBatch> queue(8192);

    SECTION("MODE 0: 80 columns at 2MHz, 8 pixels/char") {
        for (int y = 0; y < 8; ++y) emit_scanline(renderer, queue, 80, 8, FAST);
        emit_vsync(renderer, queue);
        CHECK(fb.metadata().display_width == 640);
    }

    SECTION("MODE 2: 80 columns at 2MHz, 2 pixels/char") {
        for (int y = 0; y < 8; ++y) emit_scanline(renderer, queue, 80, 2, FAST);
        emit_vsync(renderer, queue);
        CHECK(fb.metadata().width == 160);
        CHECK(fb.metadata().display_width == 640);
    }

    SECTION("MODE 4: 40 columns at 1MHz, 8 pixels/char") {
        for (int y = 0; y < 8; ++y) emit_scanline(renderer, queue, 40, 8, SLOW);
        emit_vsync(renderer, queue);
        CHECK(fb.metadata().width == 320);
        CHECK(fb.metadata().display_width == 640);
    }

    SECTION("MODE 5: 40 columns at 1MHz, 4 pixels/char") {
        for (int y = 0; y < 8; ++y) emit_scanline(renderer, queue, 40, 4, SLOW);
        emit_vsync(renderer, queue);
        CHECK(fb.metadata().width == 160);
        CHECK(fb.metadata().display_width == 640);
    }
}

// ============================================================================
// Custom-width modes: the bug that started this
// ============================================================================

TEST_CASE("display_width: a 32-column mode reports 512, not 640",
          "[frame-renderer][display-width]") {
    FrameBuffer fb(nullptr, 736, 576);
    FrameRenderer renderer(&fb);
    OutputQueue<PixelBatch> queue(8192);

    // Elite's upper screen alone: MODE 4 (1MHz, 8 pixels/char) but R1=32.
    // 32 chars * 16 clocks = 512 physical, 32 * 8 = 256 logical.
    for (int y = 0; y < 8; ++y) emit_scanline(renderer, queue, 32, 8, SLOW);
    emit_vsync(renderer, queue);

    CHECK(fb.metadata().width == 256);
    CHECK(fb.metadata().display_width == 512);
}

TEST_CASE("display_width: Elite's MODE 4/5 split reports 512 with 256/128 bands",
          "[frame-renderer][display-width][regions]") {
    FrameBuffer fb(nullptr, 736, 576);
    FrameRenderer renderer(&fb);
    OutputQueue<PixelBatch> queue(8192);

    // Upper: R1=32 MODE 4 (1MHz, 8 pixels/char) -> 256 logical, 512 physical.
    for (int y = 0; y < 24; ++y) emit_scanline(renderer, queue, 32, 8, SLOW);
    // Dashboard: R1=32 MODE 5 (1MHz, 4 pixels/char) -> 128 logical, 512 physical.
    for (int y = 0; y < 8; ++y) emit_scanline(renderer, queue, 32, 4, SLOW);
    emit_vsync(renderer, queue);

    const auto& meta = fb.metadata();
    REQUIRE(meta.regions.size() == 2);
    CHECK(meta.regions[0].pixel_width == 256);
    CHECK(meta.regions[1].pixel_width == 128);
    // Both bands are physically 512 clocks wide, so the frame is 512.
    CHECK(meta.display_width == 512);
}

// ============================================================================
// The max reduction is explicit, and its one limitation documented
// ============================================================================

TEST_CASE("display_width: a mid-frame clock switch at one R1 reduces to the widest band",
          "[frame-renderer][display-width][regions]") {
    FrameBuffer fb(nullptr, 736, 576);
    FrameRenderer renderer(&fb);
    OutputQueue<PixelBatch> queue(8192);

    // The one case a single display_width cannot render per band: the same R1
    // (40 columns, 8 pixels/char, so 320 logical throughout) drawn first at
    // 2MHz -- 40 * 8 = 320 clocks -- then at 1MHz -- 40 * 16 = 640 clocks. The
    // two bands are genuinely different physical widths; we report the wider.
    for (int y = 0; y < 12; ++y) emit_scanline(renderer, queue, 40, 8, FAST);
    for (int y = 0; y < 12; ++y) emit_scanline(renderer, queue, 40, 8, SLOW);
    emit_vsync(renderer, queue);

    const auto& meta = fb.metadata();
    // Both bands are 320 logical pixels, so the pixel width alone does not split
    // them; the physical widths (320 vs 640) do differ, and the max wins.
    CHECK(meta.display_width == 640);
}

// ============================================================================
// Borders live in the same 16MHz grid
// ============================================================================

TEST_CASE("display_width: 1MHz borders are twice the 2MHz borders for the same blanking",
          "[frame-renderer][display-width][borders]") {
    // Four blanking character periods before display. At 2MHz that is 4*8 = 32
    // clocks of left border; at 1MHz the same four periods are 4*16 = 64. The
    // active area fills the line the same way in both, so the borders -- not the
    // logical pixel count -- carry the character-clock difference.
    const int blanking = 4;

    uint32_t fast_left = 0;
    uint32_t slow_left = 0;

    {
        FrameBuffer fb(nullptr, 736, 576);
        FrameRenderer renderer(&fb);
        OutputQueue<PixelBatch> queue(8192);
        for (int y = 0; y < 8; ++y)
            emit_scanline(renderer, queue, 80, 8, FAST, blanking);
        emit_vsync(renderer, queue);
        fast_left = fb.metadata().left_border;
    }
    {
        FrameBuffer fb(nullptr, 736, 576);
        FrameRenderer renderer(&fb);
        OutputQueue<PixelBatch> queue(8192);
        for (int y = 0; y < 8; ++y)
            emit_scanline(renderer, queue, 40, 8, SLOW, blanking);
        emit_vsync(renderer, queue);
        slow_left = fb.metadata().left_border;
    }

    // The two frames have identical batch structure, differing only in the
    // character clock, so the 1MHz border is exactly twice the 2MHz one. (The
    // absolute values also fold in the HSYNC and post-HSYNC blanking batches the
    // harness emits, which is why the invariant to assert is the ratio.)
    CHECK(fast_left > 0);
    CHECK(slow_left == 2 * fast_left);
}

// ============================================================================
// The clocks datum must not leak into the rendered pixels
// ============================================================================

TEST_CASE("display_width: the display-clocks nibbles do not disturb pixel colour",
          "[frame-renderer][display-width][pixel-batch]") {
    // display_clocks is stored in pixels[6].x / pixels[7].x, nibbles shared with
    // real pixel data. As with char_scanlines, the renderer must ignore them
    // when converting to BGRA: a batch marked 16 clocks must render the same
    // white as one marked 8.
    for (uint8_t clocks : {FAST, SLOW}) {
        FrameBuffer fb(nullptr, 736, 576);
        FrameRenderer renderer(&fb);
        OutputQueue<PixelBatch> queue(8192);
        for (int y = 0; y < 4; ++y) emit_scanline(renderer, queue, 40, 8, clocks);
        emit_vsync(renderer, queue);

        auto frame = fb.read_frame();
        const size_t stride = fb.capacity_width();
        // First displayed pixel of the first scanline is opaque white.
        CHECK(frame[0 * stride + 0] == 0xFFFFFFFFu);
    }
}
