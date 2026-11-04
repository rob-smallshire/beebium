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

#include "connection/DecodedFrame.hpp"

#include "video.pb.h"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

beebium::Frame wire_frame(std::uint32_t width, std::uint32_t height) {
    beebium::Frame wire;
    wire.set_frame_number(42);
    wire.set_width(width);
    wire.set_height(height);
    wire.set_display_width(640);
    wire.set_display_height(256);
    wire.set_field_order(beebium::PROGRESSIVE);
    wire.set_pixels(std::string(std::size_t{width} * height * 4, '\x7f'));
    return wire;
}

const auto RECEIVED_AT = std::chrono::steady_clock::time_point{std::chrono::seconds{5}};

}  // namespace

TEST_CASE("A well-formed frame decodes with its geometry and pixels", "[connection][decode]") {
    auto wire = wire_frame(320, 256);
    auto* region = wire.add_regions();
    region->set_start_line(0);
    region->set_end_line(256);
    region->set_pixel_width(320);

    auto result = decode_frame(wire, RECEIVED_AT);
    auto* frame = std::get_if<std::unique_ptr<DecodedFrame>>(&result);
    REQUIRE(frame != nullptr);
    CHECK((*frame)->frame_number == 42);
    CHECK((*frame)->width == 320);
    CHECK((*frame)->height == 256);
    CHECK((*frame)->display_width == 640);
    CHECK((*frame)->display_height == 256);
    CHECK_FALSE((*frame)->interlaced);
    CHECK((*frame)->pixels.size() == 320u * 256u * 4u);
    REQUIRE((*frame)->regions.size() == 1);
    CHECK((*frame)->regions[0].pixel_width == 320);
    CHECK((*frame)->received_at == RECEIVED_AT);
}

TEST_CASE("A frame keeps the CRTC border sizes around its active area", "[connection][decode]") {
    auto wire = wire_frame(320, 256);
    wire.set_left_border(64);
    wire.set_right_border(48);
    wire.set_top_border(30);
    wire.set_bottom_border(26);
    const auto decoded = decode_frame(wire, {});
    REQUIRE(std::holds_alternative<std::unique_ptr<DecodedFrame>>(decoded));
    const auto& frame = *std::get<std::unique_ptr<DecodedFrame>>(decoded);
    CHECK(frame.left_border == 64);
    CHECK(frame.right_border == 48);
    CHECK(frame.top_border == 30);
    CHECK(frame.bottom_border == 26);
}

TEST_CASE("Decoding moves the pixel buffer out of the wire frame", "[connection][decode]") {
    auto wire = wire_frame(8, 8);
    auto result = decode_frame(wire, RECEIVED_AT);
    REQUIRE(std::holds_alternative<std::unique_ptr<DecodedFrame>>(result));
    CHECK(wire.pixels().empty());
}

TEST_CASE("An interlaced field order marks the frame interlaced", "[connection][decode]") {
    for (auto order : {beebium::EVEN_FIRST, beebium::ODD_FIRST}) {
        auto wire = wire_frame(480, 500);
        wire.set_field_order(order);
        auto result = decode_frame(wire, RECEIVED_AT);
        REQUIRE(std::holds_alternative<std::unique_ptr<DecodedFrame>>(result));
        CHECK(std::get<std::unique_ptr<DecodedFrame>>(result)->interlaced);
    }
}

TEST_CASE("A frame whose pixels do not fill it is refused", "[connection][decode]") {
    auto wire = wire_frame(640, 256);
    wire.mutable_pixels()->resize(100);
    auto result = decode_frame(wire, RECEIVED_AT);
    REQUIRE(std::holds_alternative<FrameDecodeError>(result));
    CHECK_FALSE(std::get<FrameDecodeError>(result).reason.empty());
}

TEST_CASE("A frame with no area is refused", "[connection][decode]") {
    auto wire = wire_frame(640, 256);
    wire.set_height(0);
    CHECK(std::holds_alternative<FrameDecodeError>(decode_frame(wire, RECEIVED_AT)));
}

TEST_CASE("A missing display size falls back to the logical size", "[connection][decode]") {
    auto wire = wire_frame(640, 256);
    wire.set_display_width(0);
    wire.set_display_height(0);
    auto result = decode_frame(wire, RECEIVED_AT);
    REQUIRE(std::holds_alternative<std::unique_ptr<DecodedFrame>>(result));
    const auto& frame = std::get<std::unique_ptr<DecodedFrame>>(result);
    CHECK(frame->display_width == 640);
    CHECK(frame->display_height == 256);
}
