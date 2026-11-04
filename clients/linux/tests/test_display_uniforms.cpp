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

#include "display/DisplayUniforms.hpp"
#include "display/VideoSettings.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>

using namespace beebium::linux_client;
using Catch::Approx;

namespace {

// Square pixels, no margin: the geometry the older tests were written for.
VideoSettings plain() {
    VideoSettings settings;
    settings.pixels = PixelShape::crisp;
    settings.edge_margin_percent = 0;
    return settings;
}

DecodedFrame mode_frame(std::uint32_t width, std::uint32_t height, bool interlaced) {
    DecodedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.display_width = 640;
    frame.display_height = interlaced ? 512 : 256;
    frame.interlaced = interlaced;
    return frame;
}

}  // namespace

TEST_CASE("The uniform block has the std140 offsets the shaders declare", "[display]") {
    CHECK(offsetof(DisplayUniforms, quad) == 0);
    CHECK(offsetof(DisplayUniforms, texture_size) == 16);
    CHECK(offsetof(DisplayUniforms, region_info) == 32);
    CHECK(offsetof(DisplayUniforms, regions) == 48);
    CHECK(offsetof(DisplayUniforms, inner) == 176);
    CHECK(offsetof(DisplayUniforms, band_colours) == 192);
    CHECK(sizeof(DisplayUniforms) == 256);
}

TEST_CASE("The quad is the aspect-fitted picture", "[display]") {
    const auto frame = mode_frame(320, 256, false);
    const auto uniforms = make_display_uniforms(frame, plain(), 2000, 1000, true);
    CHECK(uniforms.quad[0] == Approx(0.625f));
    CHECK(uniforms.quad[1] == Approx(1.0f));
    CHECK(uniforms.texture_size[0] == 320.0f);
    CHECK(uniforms.texture_size[1] == 256.0f);
}

TEST_CASE("The NDC y direction follows the backend", "[display]") {
    const auto frame = mode_frame(640, 256, false);
    CHECK(make_display_uniforms(frame, plain(), 100, 100, true).quad[2] == 1.0f);
    CHECK(make_display_uniforms(frame, plain(), 100, 100, false).quad[2] == -1.0f);
}

TEST_CASE("Split-screen regions are passed to the shader, at most eight", "[display]") {
    auto frame = mode_frame(320, 256, false);
    for (std::uint32_t i = 0; i < 10; ++i) {
        frame.regions.push_back({i * 20, i * 20 + 20, 100 + i});
    }
    const auto uniforms = make_display_uniforms(frame, plain(), 640, 512, false);
    CHECK(uniforms.region_info[0] == 8);
    CHECK(uniforms.regions[0][0] == 0);
    CHECK(uniforms.regions[0][1] == 20);
    CHECK(uniforms.regions[0][2] == 100);
    CHECK(uniforms.regions[7][2] == 107);
}

TEST_CASE("Video settings default to the macOS client's", "[display][video]") {
    const VideoSettings settings;
    CHECK(settings.style == DisplayStyle::standard);
    CHECK(settings.pixels == PixelShape::authentic);
    CHECK(settings.boxing == Rgb{15, 15, 15});
    CHECK(settings.edge_margin_percent == 2);
    CHECK(settings.edge_margin_colour == Rgb{0, 0, 0});
    CHECK(pixel_aspect(PixelShape::authentic) == 0.96);
    CHECK(pixel_aspect(PixelShape::crisp) == 1.0);
}

TEST_CASE("Authentic pixels make the picture about 4% narrower than crisp ones",
          "[display][video]") {
    const auto frame = mode_frame(640, 256, false);
    VideoSettings settings = plain();
    const float crisp = make_display_uniforms(frame, settings, 8000, 1000, true).quad[0];
    settings.pixels = PixelShape::authentic;
    const float authentic = make_display_uniforms(frame, settings, 8000, 1000, true).quad[0];
    CHECK(authentic / crisp == Approx(0.96f));
}

TEST_CASE("The edge margin insets the picture by its fraction of each edge, the quad unchanged",
          "[display][video]") {
    const auto frame = mode_frame(640, 256, false);
    VideoSettings settings = plain();
    const auto without = make_display_uniforms(frame, settings, 2000, 1000, true);
    CHECK(without.inner[0] == 0.0f);
    CHECK(without.inner[1] == 0.0f);
    CHECK(without.inner[2] == 1.0f);
    CHECK(without.inner[3] == 1.0f);

    settings.edge_margin_percent = 2;
    settings.edge_margin_colour = Rgb{255, 0, 51};
    const auto with = make_display_uniforms(frame, settings, 2000, 1000, true);
    // The quad is the same aspect-fitted rectangle; the picture keeps its
    // aspect inside it, scaled by 1 - 2m.
    CHECK(with.quad[0] == without.quad[0]);
    CHECK(with.quad[1] == without.quad[1]);
    CHECK(with.inner[0] == Approx(0.02f));
    CHECK(with.inner[1] == Approx(0.02f));
    CHECK(with.inner[2] == Approx(0.98f));
    CHECK(with.inner[3] == Approx(0.98f));
    // Every side of the band is the margin's colour.
    for (const auto& side : with.band_colours) {
        CHECK(side[0] == Approx(1.0f));
        CHECK(side[1] == Approx(0.0f));
        CHECK(side[2] == Approx(0.2f));
        CHECK(side[3] == Approx(1.0f));
    }
}

TEST_CASE("The edge margin is kept within its range", "[display][video]") {
    const auto frame = mode_frame(640, 256, false);
    VideoSettings settings = plain();
    settings.edge_margin_percent = 60;
    CHECK(make_display_uniforms(frame, settings, 100, 100, true).inner[0] == Approx(0.45f));
    settings.edge_margin_percent = -3;
    CHECK(make_display_uniforms(frame, settings, 100, 100, true).inner[0] == 0.0f);
}

TEST_CASE("Debug fits the active area inside its CRTC borders, each side its own colour",
          "[display][video]") {
    auto frame = mode_frame(320, 256, false);
    frame.left_border = 64;
    frame.right_border = 64;
    frame.top_border = 32;
    frame.bottom_border = 32;
    VideoSettings settings = plain();
    settings.style = DisplayStyle::debug;
    settings.edge_margin_percent = 5;  // Standard's alone
    const auto uniforms = make_display_uniforms(frame, settings, 8000, 1000, true);
    // 768 x 320 line-doubled to 640 high, fitted to the height.
    CHECK(uniforms.quad[1] == Approx(1.0f));
    CHECK(uniforms.quad[0] == Approx((768.0f / 640.0f) * 1000.0f / 8000.0f));
    CHECK(uniforms.inner[0] == Approx(64.0f / 768.0f));
    CHECK(uniforms.inner[1] == Approx(32.0f / 320.0f));
    CHECK(uniforms.inner[2] == Approx(704.0f / 768.0f));
    CHECK(uniforms.inner[3] == Approx(288.0f / 320.0f));
    // Top, bottom, left, right: the macOS client's colours.
    const float expected[4][3] = {{0, 0, 0.5f}, {0.5f, 0.5f, 0}, {0.5f, 0, 0}, {0, 0.5f, 0}};
    for (int side = 0; side < 4; ++side) {
        for (int c = 0; c < 3; ++c) {
            CHECK(uniforms.band_colours[side][c] == Approx(expected[side][c]));
        }
    }
}

TEST_CASE("Debug with no borders draws as Standard with no margin", "[display][video]") {
    const auto frame = mode_frame(320, 256, false);
    VideoSettings debug = plain();
    debug.style = DisplayStyle::debug;
    const auto drawn = make_display_uniforms(frame, debug, 1500, 900, false);
    const auto standard = make_display_uniforms(frame, plain(), 1500, 900, false);
    for (int i = 0; i < 4; ++i) {
        CHECK(drawn.quad[i] == standard.quad[i]);
        CHECK(drawn.inner[i] == standard.inner[i]);
    }
}
