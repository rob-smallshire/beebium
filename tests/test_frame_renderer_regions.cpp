// Copyright © 2025-2026 Robert Smallshire <robert@smallshire.org.uk>
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

// test_frame_renderer_regions.cpp
//
// Tests for FrameRenderer display region tracking.
// Verifies that split-screen modes (e.g., Elite's MODE 4/5 split)
// produce correct per-region metadata.

#include <catch2/catch_test_macros.hpp>
#include <beebium/PixelBatch.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/ScreenText.hpp>

using namespace beebium;

namespace {

// Helper to create a PixelBatch with specified flags and pixel count
PixelBatch make_batch(uint8_t flags, uint8_t pixel_count = 8) {
    PixelBatch batch{};
    batch.clear();
    batch.set_type(PixelBatchType::Bitmap);
    batch.set_flags(flags);
    batch.set_pixel_count(pixel_count);
    // Set some non-black pixels so the batch writes something
    for (int i = 0; i < pixel_count; ++i) {
        batch.pixels.pixels[i] = bbc_colors::WHITE;
    }
    // Restore metadata fields that pixel writes may have overwritten
    batch.set_type(PixelBatchType::Bitmap);
    batch.set_flags(flags);
    batch.set_pixel_count(pixel_count);
    return batch;
}

// As above, but for a band drawn with a stated character geometry.
//
// The renderer records the geometry per scanline alongside the pixel width and
// breaks a region on a change to either, so a split screen carries one region
// per geometry rather than one per frame.
PixelBatch make_batch_with_geometry(uint8_t flags, uint8_t pixel_count,
                                    uint8_t char_scanlines,
                                    PixelBatchType type) {
    PixelBatch batch = make_batch(flags, pixel_count);
    batch.set_type(type);
    batch.set_char_scanlines(char_scanlines);
    return batch;
}

// Simulate a complete scanline of displayed pixel batches.
// Each call emits `num_batches` display batches followed by an HSYNC pulse.
// Returns total logical pixels written (num_batches * pixel_count).
void emit_scanline(FrameRenderer& renderer, OutputQueue<PixelBatch>& queue,
                   int num_batches, uint8_t pixel_count,
                   uint8_t extra_flags = 0) {
    // Blanking before display (creates left border)
    auto blanking = make_batch(extra_flags, pixel_count);
    blanking.set_flags(extra_flags);  // No DISPLAY flag
    queue.push(blanking);
    renderer.process(queue);

    // Display batches
    for (int i = 0; i < num_batches; ++i) {
        auto batch = make_batch(VIDEO_FLAG_DISPLAY | extra_flags, pixel_count);
        queue.push(batch);
        renderer.process(queue);
    }

    // End-of-line blanking (no DISPLAY)
    auto end_blank = make_batch(extra_flags, pixel_count);
    end_blank.set_flags(extra_flags);
    queue.push(end_blank);
    renderer.process(queue);

    // HSYNC pulse (rising edge then falling edge)
    auto hsync = make_batch(VIDEO_FLAG_HSYNC | extra_flags, pixel_count);
    queue.push(hsync);
    renderer.process(queue);

    // Clear HSYNC (so next line gets a rising edge)
    auto post_hsync = make_batch(extra_flags, pixel_count);
    post_hsync.set_flags(extra_flags);
    queue.push(post_hsync);
    renderer.process(queue);
}

// Emit a VSYNC pulse to finish a frame and trigger buffer swap.
void emit_vsync(FrameRenderer& renderer, OutputQueue<PixelBatch>& queue) {
    // Gap before VSYNC (ensure rising edge)
    auto gap = make_batch(0);
    gap.set_flags(0);
    queue.push(gap);
    renderer.process(queue);

    // VSYNC
    auto vsync = make_batch(VIDEO_FLAG_VSYNC);
    queue.push(vsync);
    renderer.process(queue);

    // Clear VSYNC
    auto post_vsync = make_batch(0);
    post_vsync.set_flags(0);
    queue.push(post_vsync);
    renderer.process(queue);
}

} // anonymous namespace

// ============================================================================
// Uniform mode region tests
// ============================================================================

TEST_CASE("FrameRenderer regions: uniform mode produces one region", "[video][regions]") {
    FrameBuffer fb(nullptr, 736, 576);
    FrameRenderer renderer(&fb);
    OutputQueue<PixelBatch> queue(4096);

    // Simulate a uniform MODE 4 frame: 40 batches per line, 8 pixels/batch = 320 px/line
    const int num_scanlines = 32;
    const int batches_per_line = 40;
    const uint8_t pixel_count = 8;

    for (int y = 0; y < num_scanlines; ++y) {
        emit_scanline(renderer, queue, batches_per_line, pixel_count);
    }

    emit_vsync(renderer, queue);

    const auto& meta = fb.metadata();
    REQUIRE(meta.regions.size() == 1);
    CHECK(meta.regions[0].start_line == 0);
    CHECK(meta.regions[0].end_line == num_scanlines);
    CHECK(meta.regions[0].pixel_width == batches_per_line * pixel_count);
}

// ============================================================================
// Two-region split (Elite pattern)
// ============================================================================

TEST_CASE("FrameRenderer regions: two-region split (MODE 4 upper, MODE 5 lower)", "[video][regions]") {
    FrameBuffer fb(nullptr, 736, 576);
    FrameRenderer renderer(&fb);
    OutputQueue<PixelBatch> queue(8192);

    // Upper region: MODE 4 (8 pixels/batch, 40 batches = 320 px)
    const int upper_scanlines = 24;
    for (int y = 0; y < upper_scanlines; ++y) {
        emit_scanline(renderer, queue, 40, 8);
    }

    // Lower region: MODE 5 (4 pixels/batch, 40 batches = 160 px)
    const int lower_scanlines = 8;
    for (int y = 0; y < lower_scanlines; ++y) {
        emit_scanline(renderer, queue, 40, 4);
    }

    emit_vsync(renderer, queue);

    const auto& meta = fb.metadata();
    REQUIRE(meta.regions.size() == 2);

    CHECK(meta.regions[0].start_line == 0);
    CHECK(meta.regions[0].end_line == upper_scanlines);
    CHECK(meta.regions[0].pixel_width == 320);

    CHECK(meta.regions[1].start_line == upper_scanlines);
    CHECK(meta.regions[1].end_line == upper_scanlines + lower_scanlines);
    CHECK(meta.regions[1].pixel_width == 160);
}

// ============================================================================
// Three-region split
// ============================================================================

TEST_CASE("FrameRenderer regions: three-region split", "[video][regions]") {
    FrameBuffer fb(nullptr, 736, 576);
    FrameRenderer renderer(&fb);
    OutputQueue<PixelBatch> queue(8192);

    // Region 1: MODE 0 (8 pixels/batch, 80 batches = 640 px)
    const int region1_lines = 10;
    for (int y = 0; y < region1_lines; ++y) {
        emit_scanline(renderer, queue, 80, 8);
    }

    // Region 2: MODE 1 (4 pixels/batch, 80 batches = 320 px)
    const int region2_lines = 10;
    for (int y = 0; y < region2_lines; ++y) {
        emit_scanline(renderer, queue, 80, 4);
    }

    // Region 3: MODE 2 (2 pixels/batch, 80 batches = 160 px)
    const int region3_lines = 10;
    for (int y = 0; y < region3_lines; ++y) {
        emit_scanline(renderer, queue, 80, 2);
    }

    emit_vsync(renderer, queue);

    const auto& meta = fb.metadata();
    REQUIRE(meta.regions.size() == 3);

    CHECK(meta.regions[0].start_line == 0);
    CHECK(meta.regions[0].end_line == 10);
    CHECK(meta.regions[0].pixel_width == 640);

    CHECK(meta.regions[1].start_line == 10);
    CHECK(meta.regions[1].end_line == 20);
    CHECK(meta.regions[1].pixel_width == 320);

    CHECK(meta.regions[2].start_line == 20);
    CHECK(meta.regions[2].end_line == 30);
    CHECK(meta.regions[2].pixel_width == 160);
}

// ============================================================================
// Second frame resets regions correctly
// ============================================================================

TEST_CASE("FrameRenderer regions: regions reset between frames", "[video][regions]") {
    FrameBuffer fb(nullptr, 736, 576);
    FrameRenderer renderer(&fb);
    OutputQueue<PixelBatch> queue(8192);

    // Frame 1: split-screen (2 regions)
    for (int y = 0; y < 16; ++y) {
        emit_scanline(renderer, queue, 40, 8);
    }
    for (int y = 0; y < 8; ++y) {
        emit_scanline(renderer, queue, 40, 4);
    }
    emit_vsync(renderer, queue);

    REQUIRE(fb.metadata().regions.size() == 2);

    // Frame 2: uniform (1 region)
    for (int y = 0; y < 24; ++y) {
        emit_scanline(renderer, queue, 40, 8);
    }
    emit_vsync(renderer, queue);

    REQUIRE(fb.metadata().regions.size() == 1);
    CHECK(fb.metadata().regions[0].pixel_width == 320);
}

// ============================================================================
// Character geometry per band
// ============================================================================

namespace {

// A scanline drawn with a stated character geometry.
void emit_scanline_with_geometry(FrameRenderer& renderer,
                                 OutputQueue<PixelBatch>& queue,
                                 int num_batches, uint8_t pixel_count,
                                 uint8_t char_scanlines,
                                 PixelBatchType type) {
    auto blanking = make_batch_with_geometry(0, pixel_count, char_scanlines, type);
    queue.push(blanking);
    renderer.process(queue);

    for (int i = 0; i < num_batches; ++i) {
        auto batch = make_batch_with_geometry(VIDEO_FLAG_DISPLAY, pixel_count,
                                              char_scanlines, type);
        queue.push(batch);
        renderer.process(queue);
    }

    auto end_blank = make_batch_with_geometry(0, pixel_count, char_scanlines, type);
    queue.push(end_blank);
    renderer.process(queue);

    auto hsync = make_batch_with_geometry(VIDEO_FLAG_HSYNC, pixel_count,
                                          char_scanlines, type);
    queue.push(hsync);
    renderer.process(queue);

    auto post_hsync = make_batch_with_geometry(0, pixel_count, char_scanlines, type);
    queue.push(post_hsync);
    renderer.process(queue);
}

} // anonymous namespace

TEST_CASE("Regions carry the character geometry of their scanlines",
          "[frame-renderer][regions][screen-text]") {
    FrameBuffer fb(nullptr, 736, 576);
    FrameRenderer renderer(&fb);
    OutputQueue<PixelBatch> queue(4096);

    SECTION("a ten-scanline character row is recorded as such") {
        // MODE 3 and MODE 6 put an eight-scanline glyph on a ten-scanline
        // pitch. The pitch cannot be recovered from the pixels afterwards, so
        // it has to be recorded as they are drawn.
        for (int y = 0; y < 20; ++y) {
            emit_scanline_with_geometry(renderer, queue, 80, 8, 10,
                                        PixelBatchType::Bitmap);
        }
        emit_vsync(renderer, queue);

        const auto& meta = fb.metadata();
        REQUIRE(meta.regions.size() == 1);
        CHECK(meta.regions[0].char_scanlines == 10);
        CHECK_FALSE(meta.regions[0].is_teletext);
    }

    SECTION("a teletext band is marked as one") {
        for (int y = 0; y < 20; ++y) {
            emit_scanline_with_geometry(renderer, queue, 40, 6, 19,
                                        PixelBatchType::Teletext);
        }
        emit_vsync(renderer, queue);

        const auto& meta = fb.metadata();
        REQUIRE(meta.regions.size() == 1);
        CHECK(meta.regions[0].is_teletext);
    }

    SECTION("a change of pitch alone splits a region") {
        // Two bands of the same pixel width but different character heights
        // are two bands: a reader cannot treat them alike, which is the whole
        // point of the split.
        for (int y = 0; y < 10; ++y) {
            emit_scanline_with_geometry(renderer, queue, 80, 8, 8,
                                        PixelBatchType::Bitmap);
        }
        for (int y = 0; y < 10; ++y) {
            emit_scanline_with_geometry(renderer, queue, 80, 8, 10,
                                        PixelBatchType::Bitmap);
        }
        emit_vsync(renderer, queue);

        const auto& meta = fb.metadata();
        REQUIRE(meta.regions.size() == 2);
        CHECK(meta.regions[0].pixel_width == meta.regions[1].pixel_width);
        CHECK(meta.regions[0].char_scanlines == 8);
        CHECK(meta.regions[1].char_scanlines == 10);
        CHECK(meta.regions[0].end_line == meta.regions[1].start_line);
    }

    SECTION("a split screen becomes two bands with their own geometry") {
        // The shape Elite produces: a wider band above a narrower one, the
        // CRTC reprogrammed part way down the frame.
        for (int y = 0; y < 12; ++y) {
            emit_scanline_with_geometry(renderer, queue, 40, 8, 8,
                                        PixelBatchType::Bitmap);
        }
        for (int y = 0; y < 8; ++y) {
            emit_scanline_with_geometry(renderer, queue, 20, 8, 8,
                                        PixelBatchType::Bitmap);
        }
        emit_vsync(renderer, queue);

        const auto& meta = fb.metadata();
        REQUIRE(meta.regions.size() == 2);
        CHECK(meta.regions[0].pixel_width == 320);
        CHECK(meta.regions[1].pixel_width == 160);

        // And each becomes a band a client can snap a drag to.
        const auto bands = screen::bands_of(meta);
        REQUIRE(bands.size() == 2);
        CHECK(bands[0].top == meta.regions[0].start_line);
        CHECK(bands[1].top == meta.regions[1].start_line);
        CHECK(bands[0].bottom == bands[1].top);
        for (const auto& band : bands) {
            CHECK(band.cell_width == 8);
            CHECK(band.row_pitch == 8);
        }
    }
}
