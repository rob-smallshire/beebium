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

// Unit tests for the capture-screenshot auto-crop geometry (pure functions, no
// machine): content bounding box, padding, aspect expansion, edge clamping,
// degenerate cases, and the outward integer-scale snap with its fallback.

#include <catch2/catch_test_macros.hpp>

#include <beebium/server/ScreenshotCrop.hpp>

#include <cstdint>
#include <vector>

using beebium::server::CropRect;
using beebium::server::compute_auto_crop;
using beebium::server::content_bounding_box;

namespace {

constexpr uint32_t kWhite = 0xFFFFFFFFu;  // opaque white (BGRA)

struct Frame {
    std::vector<uint32_t> pixels;
    uint32_t w = 0;
    uint32_t h = 0;
};

Frame make_frame(uint32_t w, uint32_t h) {
    return Frame{std::vector<uint32_t>(static_cast<size_t>(w) * h, 0u), w, h};
}

void fill(Frame& f, CropRect r, uint32_t colour = kWhite) {
    for (uint32_t y = r.y; y < r.y + r.height; ++y) {
        for (uint32_t x = r.x; x < r.x + r.width; ++x) {
            f.pixels[static_cast<size_t>(y) * f.w + x] = colour;
        }
    }
}

CropRect crop(const Frame& f) {
    return compute_auto_crop(f.pixels.data(), f.w, f.h, f.w);
}

// The crop is a single uniform integer zoom of the frame: width and height are
// each a whole multiple of the crop's, by the SAME factor >= 2.
bool integer_zoom(const CropRect& r, uint32_t w, uint32_t h) {
    return r.width > 0 && r.height > 0 && w % r.width == 0 && h % r.height == 0 &&
           w / r.width == h / r.height && w / r.width >= 2;
}

// Does `outer` fully contain `inner`?
bool contains(const CropRect& outer, const CropRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

bool within(const CropRect& r, uint32_t w, uint32_t h) {
    return r.x + r.width <= w && r.y + r.height <= h && r.width > 0 && r.height > 0;
}

// Crop carries the frame's aspect ratio to within a couple of pixels of rounding.
bool aspect_matches(const CropRect& r, uint32_t w, uint32_t h) {
    const int64_t lhs = static_cast<int64_t>(r.width) * h;
    const int64_t rhs = static_cast<int64_t>(r.height) * w;
    const int64_t tol = static_cast<int64_t>(w) + h;  // ~1px each way
    return std::abs(lhs - rhs) <= tol;
}

}  // namespace

TEST_CASE("content_bounding_box finds the tight box of non-black pixels",
          "[screenshot][crop]") {
    Frame f = make_frame(100, 100);
    fill(f, {30, 40, 20, 10});
    auto box = content_bounding_box(f.pixels.data(), f.w, f.h, f.w);
    REQUIRE(box.has_value());
    CHECK(*box == CropRect{30, 40, 20, 10});
}

TEST_CASE("content_bounding_box ignores near-black noise and empty frames",
          "[screenshot][crop]") {
    Frame black = make_frame(64, 64);
    CHECK_FALSE(content_bounding_box(black.pixels.data(), black.w, black.h, black.w).has_value());

    // A pixel just at the threshold (channel 16) is not content...
    Frame noise = make_frame(64, 64);
    noise.pixels[10 * 64 + 10] = 0xFF101010u;  // all channels 16
    CHECK_FALSE(content_bounding_box(noise.pixels.data(), noise.w, noise.h, noise.w).has_value());

    // ...but one level above (17) is.
    Frame lit = make_frame(64, 64);
    lit.pixels[10 * 64 + 10] = 0xFF000011u;  // blue channel 17
    auto box = content_bounding_box(lit.pixels.data(), lit.w, lit.h, lit.w);
    REQUIRE(box.has_value());
    CHECK(*box == CropRect{10, 10, 1, 1});
}

TEST_CASE("compute_auto_crop: ordinary case pads, expands and snaps to an integer zoom",
          "[screenshot][crop]") {
    // 100x100 square-pixel frame. Content 20x20 at (40,40).
    // pad 100/40=2, 100/25=4 -> padded (38,36,24,28); aspect-expand to 28x28
    // about centre (50,50) -> (36,36,28,28); snap outward to the tightest common
    // divisor of 100x100 that still covers: z=2 -> 50x50 centred -> (25,25,50,50).
    Frame f = make_frame(100, 100);
    fill(f, {40, 40, 20, 20});
    CropRect r = crop(f);
    CHECK(r == CropRect{25, 25, 50, 50});
    // Exactly a 2x integer zoom of the frame.
    CHECK(integer_zoom(r, 100, 100));
    CHECK(contains(r, CropRect{40, 40, 20, 20}));
}

TEST_CASE("compute_auto_crop: rectangular square-pixel frame keeps aspect and integer zoom",
          "[screenshot][crop]") {
    // 640x512 frame (fx == fy == 1 against a 640x512 display).
    Frame f = make_frame(640, 512);
    fill(f, {100, 100, 40, 50});
    CropRect r = crop(f);
    CHECK(within(r, 640, 512));
    CHECK(contains(r, CropRect{100, 100, 40, 50}));
    CHECK(aspect_matches(r, 640, 512));
    CHECK(integer_zoom(r, 640, 512));  // one integer zoom on both axes
}

TEST_CASE("compute_auto_crop: all-black frame returns the whole frame",
          "[screenshot][crop]") {
    Frame f = make_frame(320, 256);
    CHECK(crop(f) == CropRect{0, 0, 320, 256});
}

TEST_CASE("compute_auto_crop: near-full content returns the whole frame",
          "[screenshot][crop]") {
    // Content almost fills the frame; the expanded box exceeds 90% -> no crop.
    Frame f = make_frame(100, 100);
    fill(f, {5, 5, 90, 90});
    CHECK(crop(f) == CropRect{0, 0, 100, 100});
}

TEST_CASE("compute_auto_crop: wide and tall content both expand to the frame aspect",
          "[screenshot][crop]") {
    // Content much wider than tall.
    Frame wide = make_frame(100, 100);
    fill(wide, {10, 45, 80, 10});
    CropRect rw = crop(wide);
    CHECK(within(rw, 100, 100));
    CHECK(contains(rw, CropRect{10, 45, 80, 10}));
    CHECK(aspect_matches(rw, 100, 100));

    // Content much taller than wide.
    Frame tall = make_frame(100, 100);
    fill(tall, {45, 10, 10, 80});
    CropRect rt = crop(tall);
    CHECK(within(rt, 100, 100));
    CHECK(contains(rt, CropRect{45, 10, 10, 80}));
    CHECK(aspect_matches(rt, 100, 100));
}

TEST_CASE("compute_auto_crop: content at each edge stays within the frame and covers it",
          "[screenshot][crop]") {
    const std::vector<CropRect> corners = {
        {0, 0, 20, 20},     // top-left
        {80, 0, 20, 20},    // top-right
        {0, 80, 20, 20},    // bottom-left
        {80, 80, 20, 20},   // bottom-right
    };
    for (const auto& c : corners) {
        Frame f = make_frame(100, 100);
        fill(f, c);
        CropRect r = crop(f);
        INFO("content at (" << c.x << "," << c.y << ")");
        CHECK(within(r, 100, 100));
        CHECK(contains(r, c));
        CHECK(aspect_matches(r, 100, 100));
    }
}

TEST_CASE("compute_auto_crop: snaps outward to an integer zoom when one fits",
          "[screenshot][crop][snap]") {
    // Small content whose expanded box (~20x20) is not itself an integer divisor
    // of the frame, but the tightest common divisor that covers (z=5 -> 20x20)
    // fits.
    Frame f = make_frame(100, 100);
    fill(f, {44, 44, 12, 12});
    CropRect r = crop(f);
    CHECK(integer_zoom(r, 100, 100));  // exact integer zoom, factor >= 2
    CHECK(contains(r, CropRect{44, 44, 12, 12}));
}

TEST_CASE("compute_auto_crop: falls back to the non-integer box when no integer zoom fits",
          "[screenshot][crop][fallback]") {
    // Wide content forces the aspect-expanded box near the frame width; no common
    // divisor of 100x100 with factor >= 2 (crop = frame/z) is large enough to
    // cover it, so the result is the expanded box itself, at a non-integer zoom.
    Frame f = make_frame(100, 100);
    fill(f, {8, 46, 84, 8});
    CropRect r = crop(f);
    CHECK(within(r, 100, 100));
    CHECK(contains(r, CropRect{8, 46, 84, 8}));
    CHECK(aspect_matches(r, 100, 100));
    // Non-integer zoom: the frame width is not a whole multiple of the crop.
    CHECK(100 % r.width != 0);
    // And it is genuinely a crop, not the whole frame.
    CHECK(r.width < 100);
}

TEST_CASE("compute_auto_crop: MODE 1-shaped frame (non-square pixels) keeps the frame aspect",
          "[screenshot][crop][nonsquare]") {
    // MODE 1 logical frame is 320x256; it scales to a 640x256 display, so pixels
    // are non-square (fx = 2, fy = 1). The crop must carry the FRAME's 320:256
    // aspect and one integer zoom, NOT the display's 640:256 -- otherwise the
    // thumbnail is stretched relative to the uncropped image.
    Frame f = make_frame(320, 256);
    fill(f, {140, 110, 40, 36});
    CropRect r = crop(f);
    CHECK(within(r, 320, 256));
    CHECK(contains(r, CropRect{140, 110, 40, 36}));
    CHECK(aspect_matches(r, 320, 256));   // frame aspect, within rounding
    CHECK(integer_zoom(r, 320, 256));     // 320/cw == 256/ch, one factor >= 2
}

TEST_CASE("compute_auto_crop: MODE 2-shaped frame (non-square pixels) keeps the frame aspect",
          "[screenshot][crop][nonsquare]") {
    // MODE 2 logical frame is 160x256, scaled to a 640x256 display (fx = 4,
    // fy = 1) -- the widest pixels. Same requirement as MODE 1.
    Frame f = make_frame(160, 256);
    fill(f, {70, 110, 20, 36});
    CropRect r = crop(f);
    CHECK(within(r, 160, 256));
    CHECK(contains(r, CropRect{70, 110, 20, 36}));
    CHECK(aspect_matches(r, 160, 256));
    CHECK(integer_zoom(r, 160, 256));
}
