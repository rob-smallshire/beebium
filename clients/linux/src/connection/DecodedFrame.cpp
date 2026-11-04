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

namespace beebium::linux_client {

FrameDecodeResult decode_frame(beebium::Frame& wire,
                               std::chrono::steady_clock::time_point received_at) {
    if (wire.width() == 0 || wire.height() == 0) {
        return FrameDecodeError{"frame has no area"};
    }
    const std::uint64_t expected_bytes =
        std::uint64_t{wire.width()} * wire.height() * 4;
    if (wire.pixels().size() != expected_bytes) {
        return FrameDecodeError{"pixel buffer holds " + std::to_string(wire.pixels().size()) +
                                " bytes, expected " + std::to_string(expected_bytes)};
    }

    auto frame = std::make_unique<DecodedFrame>();
    frame->frame_number = wire.frame_number();
    frame->width = wire.width();
    frame->height = wire.height();
    frame->display_width = wire.display_width() != 0 ? wire.display_width() : wire.width();
    frame->display_height = wire.display_height() != 0 ? wire.display_height() : wire.height();
    frame->interlaced = wire.field_order() != beebium::PROGRESSIVE;
    frame->left_border = wire.left_border();
    frame->right_border = wire.right_border();
    frame->top_border = wire.top_border();
    frame->bottom_border = wire.bottom_border();
    frame->regions.reserve(static_cast<std::size_t>(wire.regions_size()));
    for (const auto& region : wire.regions()) {
        frame->regions.push_back({region.start_line(), region.end_line(), region.pixel_width()});
    }
    frame->pixels = std::move(*wire.mutable_pixels());
    frame->received_at = received_at;
    return frame;
}

}  // namespace beebium::linux_client
