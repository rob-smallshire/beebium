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

#include "connection/ScreenReading.hpp"

#include "video.pb.h"

namespace beebium::linux_client {

namespace {

PixelRect pixel_rect(const beebium::PixelRegion& wire) {
    return {wire.x(), wire.y(), wire.width(), wire.height()};
}

}  // namespace

ScreenGrid screen_grid(const beebium::ScreenGeometry& wire) {
    ScreenGrid grid;
    grid.frame_number = wire.frame_number();
    grid.bands.reserve(static_cast<std::size_t>(wire.bands_size()));
    for (const auto& band : wire.bands()) {
        grid.bands.push_back({band.top(), band.bottom(), band.cell_width(), band.cell_height(),
                              band.column_pitch(), band.row_pitch(), band.origin_x(),
                              band.origin_y()});
    }
    return grid;
}

ScreenReading screen_reading(const beebium::ScreenText& wire) {
    ScreenReading reading;
    reading.supported = wire.supported();
    reading.text = wire.text();
    reading.unreadable_cells = wire.unreadable_cells();
    reading.ambiguous_cells = wire.ambiguous_cells();
    reading.frame_number = wire.frame_number();
    reading.runs.reserve(static_cast<std::size_t>(wire.runs_size()));
    for (const auto& run : wire.runs()) {
        TextRun& read = reading.runs.emplace_back();
        read.text = run.text();
        read.bounds = pixel_rect(run.bounds());
        read.cell_width = run.cell_width();
        read.cell_height = run.cell_height();
        read.cells.reserve(static_cast<std::size_t>(run.cells_size()));
        for (const auto& cell : run.cells()) {
            read.cells.push_back({pixel_rect(cell.bounds()), cell.matched()});
        }
    }
    return reading;
}

HeldScreenResult held_screen(beebium::ScreenHold& wire,
                             std::chrono::steady_clock::time_point received_at) {
    HeldScreen held;
    held.hold_id = wire.hold_id();
    held.grid = screen_grid(wire.geometry());
    if (wire.has_frame()) {
        auto decoded = decode_frame(*wire.mutable_frame(), received_at);
        if (auto* error = std::get_if<FrameDecodeError>(&decoded)) {
            return std::move(*error);
        }
        held.frame = std::move(std::get<std::unique_ptr<DecodedFrame>>(decoded));
    }
    return held;
}

beebium::GetScreenTextRequest screen_text_request(Interpretation interpretation,
                                                  Mode7Characters characters,
                                                  std::optional<PixelRect> region,
                                                  std::optional<std::uint64_t> hold_id) {
    beebium::GetScreenTextRequest request;
    request.set_search(interpretation == Interpretation::anywhere
                           ? beebium::SCREEN_TEXT_SEARCH_ANYWHERE
                           : beebium::SCREEN_TEXT_SEARCH_ALIGNED);
    request.set_layout(beebium::SCREEN_TEXT_LAYOUT_ROWS);
    request.set_characters(characters == Mode7Characters::displayed
                               ? beebium::SCREEN_TEXT_CHARACTERS_DISPLAYED
                               : beebium::SCREEN_TEXT_CHARACTERS_CODES);
    if (region) {
        auto* wire = request.mutable_region();
        wire->set_x(region->x);
        wire->set_y(region->y);
        wire->set_width(region->width);
        wire->set_height(region->height);
    }
    if (hold_id) {
        request.set_hold_id(*hold_id);
    }
    return request;
}

}  // namespace beebium::linux_client
