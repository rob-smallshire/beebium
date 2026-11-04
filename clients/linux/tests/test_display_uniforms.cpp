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
    CHECK(offsetof(DisplayUniforms, band_colour) == 192);
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
    CHECK(with.band_colour[0] == Approx(1.0f));
    CHECK(with.band_colour[1] == Approx(0.0f));
    CHECK(with.band_colour[2] == Approx(0.2f));
    CHECK(with.band_colour[3] == Approx(1.0f));
}

TEST_CASE("The edge margin is kept within its range", "[display][video]") {
    const auto frame = mode_frame(640, 256, false);
    VideoSettings settings = plain();
    settings.edge_margin_percent = 60;
    CHECK(make_display_uniforms(frame, settings, 100, 100, true).inner[0] == Approx(0.45f));
    settings.edge_margin_percent = -3;
    CHECK(make_display_uniforms(frame, settings, 100, 100, true).inner[0] == 0.0f);
}
