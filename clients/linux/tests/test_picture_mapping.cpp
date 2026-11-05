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

// Mapping the picture's view points to frame pixels and back (client
// specification 6.5), as the exact inverse of the display's fit: the held
// frame's content laid out at the live widget's presentation.

#include "display/PictureMapping.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using Catch::Approx;

namespace {

// A progressive 640 by 256 frame at square pixels with no margin fills a
// 1250 by 1000 view exactly: 640 / (256 * 2) is 1.25.
ScreenContent filling_content() {
    ScreenContent content;
    content.width = 640;
    content.height = 256;
    content.display_width = 640;
    content.display_height = 256;
    content.regions = {{0, 256, 640}};
    return content;
}

ScreenPresentation presentation(double width, double height, int margin_percent = 0,
                                DisplayStyle style = DisplayStyle::standard) {
    ScreenPresentation presentation;
    presentation.width = width;
    presentation.height = height;
    presentation.settings.style = style;
    presentation.settings.pixels = PixelShape::crisp;
    presentation.settings.edge_margin_percent = margin_percent;
    return presentation;
}

const ScreenPresentation FILLING = presentation(1250, 1000);

void check_pixel(const std::optional<FrameHit>& hit, double x, double y) {
    REQUIRE(hit);
    CHECK(hit->pixel.x == Approx(x).margin(0.5));
    CHECK(hit->pixel.y == Approx(y).margin(0.5));
}

void check_round_trip(const PictureMapping& mapping, FramePoint pixel) {
    check_pixel(mapping.frame_hit(mapping.view_point(pixel)), pixel.x, pixel.y);
}

}  // namespace

TEST_CASE("The view's centre, corners and far corner map to the frame's",
          "[picture-mapping]") {
    const PictureMapping mapping(filling_content(), FILLING);
    const auto centre = mapping.frame_hit({625, 500});
    check_pixel(centre, 320, 128);
    CHECK(centre->inside_active);
    check_pixel(mapping.frame_hit({0, 0}), 0, 0);
    check_pixel(mapping.frame_hit({1250, 1000}), 640, 256);
}

TEST_CASE("A frame pixel maps to the view and back to itself", "[picture-mapping]") {
    const PictureMapping mapping(filling_content(), FILLING);
    for (const FramePoint pixel : {FramePoint{100, 50}, FramePoint{0, 0}, FramePoint{639, 255},
                                   FramePoint{313, 201}}) {
        check_round_trip(mapping, pixel);
    }
}

TEST_CASE("Inside an edge margin the picture still round-trips, from its own origin",
          "[picture-mapping]") {
    const PictureMapping mapping(filling_content(), presentation(1250, 1000, 2));
    check_round_trip(mapping, {100, 50});
    check_round_trip(mapping, {320, 128});
    check_pixel(mapping.frame_hit({0.02 * 1250, 0.02 * 1000}), 0, 0);
    check_pixel(mapping.frame_hit({625, 500}), 320, 128);
}

TEST_CASE("No point outside the picture maps to the frame", "[picture-mapping]") {
    // Wider than the picture: pillarboxed.
    const PictureMapping mapping(filling_content(), presentation(2000, 1000));
    CHECK_FALSE(mapping.frame_hit({0.02 * 2000, 500}));
    check_pixel(mapping.frame_hit({1000, 500}), 320, 128);
}

TEST_CASE("A point in a border clamps to the active edge and says so", "[picture-mapping]") {
    ScreenContent content = filling_content();
    content.left_border = 32;
    content.right_border = 32;
    content.top_border = 16;
    content.bottom_border = 16;
    const PictureMapping mapping(content,
                                 presentation(704, 576, 0, DisplayStyle::debug));
    const auto hit = mapping.frame_hit({0, 0});
    check_pixel(hit, 0, 0);
    CHECK_FALSE(hit->inside_active);
    // And the active area's own corner is inside.
    const auto corner = mapping.frame_hit({32.5, 32.5});
    REQUIRE(corner);
    CHECK(corner->inside_active);
    check_round_trip(mapping, {100, 50});
}

TEST_CASE("Each split-screen band maps by its own pixel width", "[picture-mapping]") {
    ScreenContent content = filling_content();
    content.regions = {{0, 128, 640}, {128, 256, 320}};
    const PictureMapping mapping(content, FILLING);
    check_pixel(mapping.frame_hit({625, 750}), 160, 192);
    check_pixel(mapping.frame_hit({625, 250}), 320, 64);
    // Round trips in either band.
    check_round_trip(mapping, {600, 60});
    check_round_trip(mapping, {300, 200});
    // A whole lower band row spans the picture's width.
    const ViewRect lower = mapping.view_rect({0, 200, 320, 8});
    CHECK(lower.x == Approx(0.0));
    CHECK(lower.width == Approx(1250.0));
}

TEST_CASE("A half-width texture is stretched to the display", "[picture-mapping]") {
    ScreenContent content = filling_content();
    content.width = 320;
    content.regions = {{0, 256, 320}};
    const PictureMapping mapping(content, FILLING);
    check_pixel(mapping.frame_hit({625, 500}), 160, 128);
}

TEST_CASE("The whole frame covers the fitted picture whatever the view's shape",
          "[picture-mapping]") {
    for (const auto& [width, height] : {std::pair{1250.0, 1000.0}, std::pair{2000.0, 800.0},
                                        std::pair{700.0, 1200.0}}) {
        const PictureMapping mapping(filling_content(), presentation(width, height));
        const ViewRect whole = mapping.view_rect({0, 0, 640, 256});
        const ViewRect picture = mapping.picture_rect();
        CHECK(whole.x == Approx(picture.x).margin(0.5));
        CHECK(whole.y == Approx(picture.y).margin(0.5));
        CHECK(whole.width == Approx(picture.width).margin(0.5));
        CHECK(whole.height == Approx(picture.height).margin(0.5));
    }
}

TEST_CASE("A mapping laid out for an earlier view size is out of register",
          "[picture-mapping]") {
    const PictureMapping stale(filling_content(), presentation(1250, 1000));
    const PictureMapping current(filling_content(), presentation(2000, 800));
    CHECK(std::abs(stale.view_rect({0, 0, 640, 256}).width -
                   current.view_rect({0, 0, 640, 256}).width) > 1.0);
}

TEST_CASE("A frame rect spans its part of the view", "[picture-mapping]") {
    const PictureMapping mapping(filling_content(), FILLING);
    const ViewRect quarter = mapping.view_rect({0, 0, 320, 128});
    CHECK(quarter.x == Approx(0.0).margin(1));
    CHECK(quarter.y == Approx(0.0).margin(1));
    CHECK(quarter.width == Approx(625.0).margin(1));
    CHECK(quarter.height == Approx(500.0).margin(1));
}

TEST_CASE("Authentic pixels and interlaced teletext fit as the display draws them",
          "[picture-mapping]") {
    // Teletext: interlaced, not line-doubled, so its picture is wider.
    ScreenContent content = filling_content();
    content.width = 480;
    content.height = 500;
    content.display_width = 480;
    content.display_height = 500;
    content.interlaced = true;
    content.regions = {{0, 500, 480}};
    ScreenPresentation authentic = presentation(1000, 1000);
    authentic.settings.pixels = PixelShape::authentic;
    const PictureMapping mapping(content, authentic);
    const ViewRect picture = mapping.picture_rect();
    CHECK(picture.width / picture.height == Approx(480 * AUTHENTIC_PIXEL_ASPECT / 500.0));
    check_round_trip(mapping, {240, 250});
    check_round_trip(mapping, {12, 490});
}
