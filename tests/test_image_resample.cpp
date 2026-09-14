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

// Unit tests for the separable Lanczos-3 resampler (pure, no machine):
// constant-preservation, symmetric upscale of an impulse, low-pass on a
// downscaled checkerboard, and anisotropic (non-square-pixel) MODE 1 / MODE 2
// scaling that turns an on-screen square into a square in the output.

#include <catch2/catch_test_macros.hpp>

#include <beebium/server/ImageResample.hpp>

#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

using beebium::server::resample_bgra_lanczos3;

namespace {

// Pack channels into a BGRA32 pixel (as the framebuffer stores them).
constexpr uint32_t bgra(uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    return static_cast<uint32_t>(b) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(a) << 24);
}

uint8_t chan_r(uint32_t p) { return static_cast<uint8_t>((p >> 16) & 0xFF); }

struct Image {
    std::vector<uint32_t> pixels;
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t at(uint32_t x, uint32_t y) const { return pixels[static_cast<size_t>(y) * w + x]; }
    void set(uint32_t x, uint32_t y, uint32_t c) { pixels[static_cast<size_t>(y) * w + x] = c; }
};

Image solid(uint32_t w, uint32_t h, uint32_t colour) {
    return Image{std::vector<uint32_t>(static_cast<size_t>(w) * h, colour), w, h};
}

Image resample(const Image& src, uint32_t dw, uint32_t dh) {
    auto out = resample_bgra_lanczos3(src.pixels.data(), src.w, 0, 0, src.w, src.h, dw, dh);
    return Image{std::move(out), dw, dh};
}

// Bounding box of pixels whose red channel is at least `thresh`.
struct Box { int min_x, min_y, max_x, max_y; };
Box red_box(const Image& img, uint8_t thresh) {
    Box b{static_cast<int>(img.w), static_cast<int>(img.h), -1, -1};
    for (uint32_t y = 0; y < img.h; ++y) {
        for (uint32_t x = 0; x < img.w; ++x) {
            if (chan_r(img.at(x, y)) >= thresh) {
                if (static_cast<int>(x) < b.min_x) b.min_x = static_cast<int>(x);
                if (static_cast<int>(x) > b.max_x) b.max_x = static_cast<int>(x);
                if (static_cast<int>(y) < b.min_y) b.min_y = static_cast<int>(y);
                if (static_cast<int>(y) > b.max_y) b.max_y = static_cast<int>(y);
            }
        }
    }
    return b;
}

}  // namespace

TEST_CASE("resample: a constant image stays exactly constant at any scale",
          "[resample]") {
    const uint32_t c = bgra(40, 173, 220, 255);
    Image src = solid(20, 12, c);

    for (auto dims : std::vector<std::pair<uint32_t, uint32_t>>{
             {50, 40}, {6, 4}, {20, 12}, {640, 256}}) {
        Image out = resample(src, dims.first, dims.second);
        bool all_equal = true;
        for (uint32_t p : out.pixels) all_equal = all_equal && (p == c);
        INFO("scaled to " << dims.first << "x" << dims.second);
        CHECK(all_equal);
    }
}

TEST_CASE("resample: a 2x upscale of a single bright pixel is a centred symmetric blob",
          "[resample]") {
    // Impulse at the centre of a 9x9 black frame. A 2x upscale maps the source
    // centre to output x = 8.5, so the blob is mirror-symmetric about columns
    // 8/9 and rows 8/9.
    Image src = solid(9, 9, bgra(0, 0, 0, 255));
    src.set(4, 4, bgra(255, 255, 255, 255));
    Image out = resample(src, 18, 18);

    bool h_symmetric = true, v_symmetric = true;
    for (uint32_t y = 0; y < 18; ++y) {
        for (uint32_t x = 0; x < 18; ++x) {
            if (chan_r(out.at(x, y)) != chan_r(out.at(17 - x, y))) h_symmetric = false;
            if (chan_r(out.at(x, y)) != chan_r(out.at(x, 17 - y))) v_symmetric = false;
        }
    }
    CHECK(h_symmetric);
    CHECK(v_symmetric);

    // The peak sits on the central columns/rows, not off to a side.
    Box b = red_box(out, 200);
    REQUIRE(b.max_x >= 0);
    const int cx = (b.min_x + b.max_x);   // == 17 when centred on 8/9
    const int cy = (b.min_y + b.max_y);
    CHECK(cx == 17);
    CHECK(cy == 17);
}

TEST_CASE("resample: a downscaled 1-pixel checkerboard low-passes to a uniform mid-grey",
          "[resample]") {
    // Highest-frequency signal: alternating black/white pixels, DC = 127.5. A
    // strong downscale must average it out, not alias to black or white.
    Image src = solid(16, 16, 0);
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            const uint8_t v = ((x + y) & 1u) ? 255 : 0;
            src.set(x, y, bgra(v, v, v, 255));
        }
    }
    Image out = resample(src, 2, 2);

    uint8_t lo = 255, hi = 0;
    for (uint32_t p : out.pixels) {
        const uint8_t v = chan_r(p);
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
        INFO("cell value " << static_cast<int>(v));
        CHECK(v >= 113);
        CHECK(v <= 142);
    }
    CHECK(hi - lo <= 8);  // uniform across the cells
}

TEST_CASE("resample: MODE 1-shaped anisotropic scale keeps an on-screen square square",
          "[resample][nonsquare]") {
    // MODE 1 logical frame 320x256 scales to a 640x256 display: fx = 2, fy = 1.
    // An 8(w) x 16(h) logical rectangle is a square on screen, so it must come
    // out ~16 x 16 in the output -- equal extents on both axes.
    Image src = solid(320, 256, bgra(0, 0, 0, 255));
    for (uint32_t y = 120; y < 136; ++y) {
        for (uint32_t x = 156; x < 164; ++x) src.set(x, y, bgra(255, 255, 255, 255));
    }
    Image out = resample(src, 640, 256);

    Box b = red_box(out, 128);
    REQUIRE(b.max_x >= 0);
    const int width = b.max_x - b.min_x + 1;
    const int height = b.max_y - b.min_y + 1;
    INFO("output feature " << width << "x" << height);
    CHECK(width >= 13);
    CHECK(width <= 19);
    CHECK(height >= 13);
    CHECK(height <= 19);
    CHECK(std::abs(width - height) <= 2);
}

TEST_CASE("resample: MODE 2-shaped anisotropic scale keeps an on-screen square square",
          "[resample][nonsquare]") {
    // MODE 2 logical frame 160x256 scales to a 640x256 display: fx = 4, fy = 1.
    // A 4(w) x 16(h) logical rectangle is a square on screen -> ~16 x 16 output.
    Image src = solid(160, 256, bgra(0, 0, 0, 255));
    for (uint32_t y = 120; y < 136; ++y) {
        for (uint32_t x = 78; x < 82; ++x) src.set(x, y, bgra(255, 255, 255, 255));
    }
    Image out = resample(src, 640, 256);

    Box b = red_box(out, 128);
    REQUIRE(b.max_x >= 0);
    const int width = b.max_x - b.min_x + 1;
    const int height = b.max_y - b.min_y + 1;
    INFO("output feature " << width << "x" << height);
    CHECK(width >= 13);
    CHECK(width <= 19);
    CHECK(height >= 13);
    CHECK(height <= 19);
    CHECK(std::abs(width - height) <= 2);
}
