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
#include "beebium/ScreenText.hpp"
#include "beebium/TeletextText.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace beebium::service {

VideoServiceImpl::VideoServiceImpl(FrameBuffer& frame_buffer,
                                   TeletextGrid& teletext_grid,
                                   PeekByte peek_byte)
    : frame_buffer_(frame_buffer)
    , teletext_grid_(teletext_grid)
    , peek_byte_(std::move(peek_byte)) {
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

screen::Search to_screen_search(ScreenTextSearch search) {
    switch (search) {
        case SCREEN_TEXT_SEARCH_ALIGNED:
            return screen::Search::Aligned;
        case SCREEN_TEXT_SEARCH_ANYWHERE:
        default:
            return screen::Search::Anywhere;
    }
}

screen::Layout to_screen_layout(ScreenTextLayout layout) {
    return layout == SCREEN_TEXT_LAYOUT_FLOWED ? screen::Layout::Flowed
                                               : screen::Layout::Rows;
}

TeletextCharacters to_screen_characters(ScreenTextCharacters characters) {
    return characters == SCREEN_TEXT_CHARACTERS_DISPLAYED
               ? TeletextCharacters::Displayed
               : TeletextCharacters::Codes;
}

void to_proto_region(const screen::PixelRect& rect, PixelRegion* out) {
    out->set_x(rect.x);
    out->set_y(rect.y);
    out->set_width(rect.width);
    out->set_height(rect.height);
}

} // namespace

grpc::Status VideoServiceImpl::GetScreenText(
    grpc::ServerContext* /*context*/,
    const GetScreenTextRequest* request,
    ScreenText* response) {

    const auto& meta = frame_buffer_.metadata();

    // One consistent frame, taken without stalling the emulation thread for
    // longer than a single buffer copy: the teletext grid for the teletext
    // strategy, the pixels for the bitmap one.
    const auto teletext = teletext_grid_.snapshot();

    std::vector<uint32_t> pixels(frame_buffer_.capacity_pixels());
    frame_buffer_.copy_frame(pixels.data(), pixels.size());

    screen::FrameImage frame_image;
    frame_image.pixels = pixels.data();
    frame_image.stride = static_cast<uint32_t>(frame_buffer_.stride_pixels());
    frame_image.width = meta.width;
    frame_image.height = meta.height;

    // The glyph set the machine was drawing with: the ROM base font, overlaid
    // with the soft font read from RAM when the running MOS is recognised.
    // Without a machine to peek, the base font stands alone. Assembled once and
    // handed to every band.
    std::vector<screentext::GlyphSet> glyph_sets;
    if (peek_byte_) {
        glyph_sets = screen::assemble_glyph_sets(peek_byte_);
    } else {
        glyph_sets.push_back(screentext::builtin_glyph_set("acorn-mos-1.20"));
    }

    screen::BandSources sources;
    sources.teletext = &teletext;
    sources.image = frame_image;
    sources.glyph_sets = &glyph_sets;

    // The whole display when the caller named no region. A caller that has not
    // dragged anything wants everything.
    screen::PixelRect region{0, 0, meta.width, meta.height};
    if (request->has_region()) {
        const screen::PixelRect asked{request->region().x(), request->region().y(),
                                      request->region().width(),
                                      request->region().height()};
        // Clipped rather than rejected: a drag that ran off the edge of the
        // display selected as far as the edge, which is what the user meant.
        region = region.intersected(asked);
    }

    const screen::Search search = to_screen_search(request->search());
    const screen::Layout layout = to_screen_layout(request->layout());
    const TeletextCharacters characters = to_screen_characters(request->characters());

    std::vector<screen::BandReading> readings;
    for (const screen::Band& band : screen::bands_of(meta)) {
        // A band the region does not touch is not part of this request at all,
        // so it neither contributes runs nor votes on whether the request
        // could be read.
        const screen::PixelRect band_rect{0, band.top, meta.width,
                                          band.bottom - band.top};
        if (band_rect.intersected(region).empty()) {
            continue;
        }
        readings.push_back(
            screen::read_band(band, region, search, sources, characters));
    }

    const screen::Reading reading = screen::concatenate_bands_readings(std::move(readings), layout);

    response->set_supported(reading.supported);
    response->set_text(reading.text);
    response->set_unreadable_cells(reading.unreadable_cells);
    response->set_ambiguous_cells(reading.ambiguous_cells);
    response->set_frame_number(meta.frame_number);

    for (const screen::TextRun& run : reading.runs) {
        auto* out = response->add_runs();
        out->set_text(run.text);
        to_proto_region(run.bounds, out->mutable_bounds());
        out->set_cell_width(run.cell_width);
        out->set_cell_height(run.cell_height);
        for (const screen::TextCell& cell : run.cells) {
            auto* out_cell = out->add_cells();
            to_proto_region(cell.bounds, out_cell->mutable_bounds());
            out_cell->set_matched(cell.matched);
        }
    }

    return grpc::Status::OK;
}

grpc::Status VideoServiceImpl::GetScreenGeometry(
    grpc::ServerContext* /*context*/,
    const GetScreenGeometryRequest* /*request*/,
    ScreenGeometry* response) {

    const auto& meta = frame_buffer_.metadata();

    // Every band reports its grid, including one no strategy can read text
    // from. Where the cells are and what is in them are separate questions,
    // and snapping a drag needs only the first.
    for (const screen::Band& band : screen::bands_of(meta)) {
        auto* out = response->add_bands();
        out->set_top(band.top);
        out->set_bottom(band.bottom);
        out->set_cell_width(band.cell_width);
        out->set_cell_height(band.cell_height);
        out->set_column_pitch(band.column_pitch);
        out->set_row_pitch(band.row_pitch);
        out->set_origin_x(band.origin_x);
        out->set_origin_y(band.origin_y);
    }

    response->set_frame_number(meta.frame_number);
    return grpc::Status::OK;
}

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
