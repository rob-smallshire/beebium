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

// Picture geometry against the scenarios of client specification 6.2 and 6.3.

#include "connection/PictureGeometry.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace beebium::linux_client;
using Catch::Approx;

TEST_CASE("Progressive modes are line-doubled", "[geometry]") {
    // 640 x 256 shown as 640 x 512 with square pixels.
    CHECK(picture_aspect(640, 256, false, SQUARE_PIXEL_ASPECT) == Approx(1.25));
}

TEST_CASE("Interlaced teletext is used as delivered", "[geometry]") {
    CHECK(picture_aspect(640, 512, true, SQUARE_PIXEL_ASPECT) == Approx(1.25));
}

TEST_CASE("Switching between teletext and a bitmap mode keeps the picture size",
          "[geometry]") {
    CHECK(picture_aspect(640, 512, true, AUTHENTIC_PIXEL_ASPECT) ==
          Approx(picture_aspect(640, 256, false, AUTHENTIC_PIXEL_ASPECT)));
}

TEST_CASE("Authentic pixels make the picture about 4% narrower than crisp", "[geometry]") {
    const double authentic = picture_aspect(640, 256, false, AUTHENTIC_PIXEL_ASPECT);
    const double crisp = picture_aspect(640, 256, false, SQUARE_PIXEL_ASPECT);
    CHECK(authentic / crisp == Approx(0.96));
}

TEST_CASE("A wide drawable pillarboxes the picture", "[geometry]") {
    const auto fit = fit_picture(1.25, 2000, 1000);
    CHECK(fit.height == Approx(1000));
    CHECK(fit.width == Approx(1250));
    CHECK(fit.x == Approx(375));
    CHECK(fit.y == Approx(0));
    CHECK(fit.scale_x == Approx(0.625));
    CHECK(fit.scale_y == Approx(1.0));
}

TEST_CASE("A tall drawable letterboxes the picture", "[geometry]") {
    const auto fit = fit_picture(1.25, 1000, 1000);
    CHECK(fit.width == Approx(1000));
    CHECK(fit.height == Approx(800));
    CHECK(fit.x == Approx(0));
    CHECK(fit.y == Approx(100));
    CHECK(fit.scale_y == Approx(0.8));
}

TEST_CASE("The picture is never stretched: its aspect survives any drawable",
          "[geometry]") {
    for (auto [w, h] : {std::pair{640.0, 480.0}, {1920.0, 1080.0}, {300.0, 900.0}}) {
        const auto fit = fit_picture(1.2, w, h);
        CHECK(fit.width / fit.height == Approx(1.2));
        CHECK(fit.width <= w + 1e-9);
        CHECK(fit.height <= h + 1e-9);
    }
}

TEST_CASE("Degenerate inputs fit to nothing rather than dividing by zero", "[geometry]") {
    CHECK(picture_aspect(0, 256, false, 1.0) == 0.0);
    const auto fit = fit_picture(1.25, 0, 100);
    CHECK(fit.width == 0.0);
    CHECK(fit.height == 0.0);
}

TEST_CASE("Without split regions every scanline has the texture's width", "[geometry]") {
    const std::vector<FrameRegion> one{{0, 256, 320}};
    CHECK(pixel_width_for_scanline(one, 100, 320) == 320);
    CHECK(pixel_width_for_scanline({}, 100, 640) == 640);
}

TEST_CASE("A split screen gives each band its own pixel width", "[geometry]") {
    // Elite: MODE 4 above, MODE 5 below.
    const std::vector<FrameRegion> split{{0, 192, 320}, {192, 256, 160}};
    CHECK(pixel_width_for_scanline(split, 0, 320) == 320);
    CHECK(pixel_width_for_scanline(split, 191, 320) == 320);
    CHECK(pixel_width_for_scanline(split, 192, 320) == 160);
    CHECK(pixel_width_for_scanline(split, 255, 320) == 160);
}

TEST_CASE("A scanline outside every region falls back to the texture width",
          "[geometry]") {
    const std::vector<FrameRegion> split{{0, 100, 320}, {100, 200, 160}};
    CHECK(pixel_width_for_scanline(split, 250, 640) == 640);
}

TEST_CASE("Only the first eight regions are honoured", "[geometry]") {
    std::vector<FrameRegion> many;
    for (std::uint32_t i = 0; i < 10; ++i) {
        many.push_back({i * 10, i * 10 + 10, 100 + i});
    }
    CHECK(pixel_width_for_scanline(many, 75, 640) == 107);
    CHECK(pixel_width_for_scanline(many, 85, 640) == 640);
}
