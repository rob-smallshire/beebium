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

#ifndef BEEBIUM_LINUX_CONNECTION_DECODED_FRAME_HPP
#define BEEBIUM_LINUX_CONNECTION_DECODED_FRAME_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace beebium {
class Frame;
}

namespace beebium::linux_client {

// A horizontal band of scanlines sharing one logical pixel width
// (video.proto DisplayRegion): how split-screen modes are described.
struct FrameRegion {
    std::uint32_t start_line = 0;  // inclusive
    std::uint32_t end_line = 0;    // exclusive
    std::uint32_t pixel_width = 0;
};

// One video frame as the renderer needs it, taken from the wire Frame with
// its pixels moved rather than copied.
struct DecodedFrame {
    std::uint64_t frame_number = 0;
    std::uint32_t width = 0;   // logical texels
    std::uint32_t height = 0;  // scanlines
    std::uint32_t display_width = 0;
    std::uint32_t display_height = 0;
    bool interlaced = false;   // field_order != PROGRESSIVE
    // The CRTC's blanking around the active area, in the same units as
    // display_width and display_height; the frame's pixels are the active
    // area alone.
    std::uint32_t left_border = 0;
    std::uint32_t right_border = 0;
    std::uint32_t top_border = 0;
    std::uint32_t bottom_border = 0;
    std::vector<FrameRegion> regions;

    // BGRA32, width * height * 4 bytes, rows top to bottom.
    std::string pixels;

    // When the client received it: for frame pacing measurements.
    std::chrono::steady_clock::time_point received_at{};
};

// Why a wire frame was refused.
struct FrameDecodeError {
    std::string reason;
};

using FrameDecodeResult = std::variant<std::unique_ptr<DecodedFrame>, FrameDecodeError>;

// Validate and convert a wire frame, consuming its pixel buffer. A frame with
// no area, or whose pixel buffer does not hold width * height * 4 bytes, is
// refused rather than drawn from out of bounds. A display size of zero falls
// back to the logical size so geometry never divides by zero.
FrameDecodeResult decode_frame(beebium::Frame& wire,
                               std::chrono::steady_clock::time_point received_at);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_DECODED_FRAME_HPP
