// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include "beebium/service/VideoService.hpp"
#include "beebium/FrameBuffer.hpp"
#include "beebium/TeletextText.hpp"

#include <algorithm>

namespace beebium::service {

VideoServiceImpl::VideoServiceImpl(FrameBuffer& frame_buffer,
                                   TeletextGrid& teletext_grid)
    : frame_buffer_(frame_buffer)
    , teletext_grid_(teletext_grid) {
}

namespace {

TeletextCharacterSet to_proto_charset(beebium::TeletextCellCharset charset) {
    switch (charset) {
        case beebium::TeletextCellCharset::ContiguousGraphics:
            return TELETEXT_CONTIGUOUS_GRAPHICS;
        case beebium::TeletextCellCharset::SeparatedGraphics:
            return TELETEXT_SEPARATED_GRAPHICS;
        case beebium::TeletextCellCharset::Alpha:
        default:
            return TELETEXT_ALPHA;
    }
}

} // namespace

grpc::Status VideoServiceImpl::GetTeletextScreen(
    grpc::ServerContext* /*context*/,
    const GetTeletextScreenRequest* request,
    TeletextScreen* response) {

    // One consistent frame, taken without stalling the emulation thread for
    // longer than a single buffer copy.
    const auto screen = teletext_grid_.snapshot();

    beebium::TeletextRegion region = beebium::TeletextRegion::whole_screen();
    if (request->has_region()) {
        region.row = request->region().row();
        region.column = request->region().column();
        region.rows = request->region().rows();
        region.columns = request->region().columns();
    }

    // Clip to the grid, so the reported dimensions describe what is actually
    // returned rather than what was asked for.
    const size_t first_row = std::min<size_t>(region.row, TeletextGrid::ROWS);
    const size_t first_column = std::min<size_t>(region.column, TeletextGrid::COLUMNS);
    const size_t last_row =
        std::min<size_t>(first_row + region.rows, TeletextGrid::ROWS);
    const size_t last_column =
        std::min<size_t>(first_column + region.columns, TeletextGrid::COLUMNS);

    response->set_active(screen.active);
    response->set_frame_number(screen.frame_number);
    response->set_rows(static_cast<uint32_t>(last_row - first_row));
    response->set_columns(static_cast<uint32_t>(last_column - first_column));

    for (size_t row = first_row; row < last_row; ++row) {
        for (size_t column = first_column; column < last_column; ++column) {
            const auto& cell = screen.cell(row, column);
            auto* out = response->add_cells();
            out->set_character(cell.character);
            out->set_fg(cell.fg);
            out->set_bg(cell.bg);
            out->set_charset(to_proto_charset(cell.charset));
            out->set_double_height_top(cell.double_height_top);
            out->set_double_height_bottom(cell.double_height_bottom);
            out->set_concealed(cell.concealed);
            out->set_flashing(cell.flashing);
            out->set_cursor(cell.cursor);
            out->set_is_control_code(cell.is_control_code);
        }
    }

    const auto layout = request->layout() == TELETEXT_LAYOUT_FLOWED
                            ? beebium::TeletextLinearisation::Flowed
                            : beebium::TeletextLinearisation::Rows;
    response->set_text(teletext_text(screen, region, layout));

    return grpc::Status::OK;
}

VideoServiceImpl::~VideoServiceImpl() = default;

grpc::Status VideoServiceImpl::SubscribeFrames(
    grpc::ServerContext* context,
    const SubscribeFramesRequest* /*request*/,
    grpc::ServerWriter<Frame>* writer) {

    uint64_t last_version = 0;

    // Pre-allocate buffers at capacity to handle any frame size
    std::vector<uint32_t> raw_buffer(frame_buffer_.capacity_pixels());
    std::vector<uint32_t> packed_buffer(frame_buffer_.capacity_pixels());

    while (!context->IsCancelled()) {
        uint64_t current_version = frame_buffer_.version();

        if (current_version != last_version) {
            // Get logical dimensions and metadata
            size_t width = frame_buffer_.width();
            size_t height = frame_buffer_.height();
            size_t stride = frame_buffer_.stride_pixels();
            const auto& meta = frame_buffer_.metadata();

            // Copy raw frame data (with stride padding)
            frame_buffer_.copy_frame(raw_buffer.data(), raw_buffer.size());

            // Pack pixels by removing stride padding (if any)
            size_t packed_size = width * height;
            if (width == stride) {
                // No padding, direct copy
                std::copy(raw_buffer.begin(), raw_buffer.begin() + packed_size, packed_buffer.begin());
            } else {
                // Remove padding by copying row by row
                for (size_t y = 0; y < height; ++y) {
                    std::copy(raw_buffer.begin() + y * stride,
                              raw_buffer.begin() + y * stride + width,
                              packed_buffer.begin() + y * width);
                }
            }

            // Build frame message
            Frame frame;
            frame.set_frame_number(current_version);
            frame.set_width(static_cast<uint32_t>(width));
            frame.set_height(static_cast<uint32_t>(height));
            frame.set_pixels(packed_buffer.data(), packed_size * sizeof(uint32_t));

            // Set field order based on interlace metadata
            if (meta.interlaced) {
                frame.set_field_order(FieldOrder::EVEN_FIRST);  // Our renderer writes even lines first
            } else {
                frame.set_field_order(FieldOrder::PROGRESSIVE);
            }

            // Set border dimensions from CRTC timing
            frame.set_left_border(meta.left_border);
            frame.set_right_border(meta.right_border);
            frame.set_top_border(meta.top_border);
            frame.set_bottom_border(meta.bottom_border);

            // Set target display resolution for client-side scaling
            frame.set_display_width(meta.display_width);
            frame.set_display_height(meta.display_height);

            // Set per-region geometry for split-screen modes
            for (const auto& region : meta.regions) {
                auto* proto_region = frame.add_regions();
                proto_region->set_start_line(region.start_line);
                proto_region->set_end_line(region.end_line);
                proto_region->set_pixel_width(region.pixel_width);
            }

            if (!writer->Write(frame)) {
                // Client disconnected
                break;
            }

            last_version = current_version;
        }

        // Brief sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return grpc::Status::OK;
}

grpc::Status VideoServiceImpl::GetConfig(
    grpc::ServerContext* /*context*/,
    const GetConfigRequest* /*request*/,
    VideoConfig* response) {

    response->set_width(static_cast<uint32_t>(frame_buffer_.width()));
    response->set_height(static_cast<uint32_t>(frame_buffer_.height()));
    response->set_framerate_hz(50);  // PAL

    return grpc::Status::OK;
}

} // namespace beebium::service
