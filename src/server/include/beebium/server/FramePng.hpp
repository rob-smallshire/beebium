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

// A machine's screen as a PNG: the one capture path for capture-screenshot,
// preset thumbnails and a saved machine's thumbnail.png.

#pragma once

#include "beebium/FrameBuffer.hpp"
#include "beebium/server/AtomicFile.hpp"
#include "beebium/server/ImageResample.hpp"
#include "beebium/server/ScreenshotCrop.hpp"
#include "stb_image_write.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace beebium::server {

// Writes the frame `frame_buffer` holds to `filepath` as a PNG, atomically:
// scaled from the logical frame to the display size, inside a black border
// of `border_pixels`. With `crop_auto` the screen's content is found and
// enlarged to the same size, keeping the frame's aspect ratio and resampling
// with a filter (Lanczos-3) rather than replicating pixels; without it the
// whole frame is scaled nearest-neighbour. Returns false, writing nothing,
// when the buffer holds no frame yet; throws when the file cannot be written.
inline bool write_frame_png(const FrameBuffer& frame_buffer, const std::filesystem::path& filepath,
                            std::uint32_t border_pixels, bool crop_auto) {
    if (frame_buffer.version() == 0) return false;

    FrameMetadata metadata;
    std::vector<std::uint32_t> frame_copy(frame_buffer.capacity_pixels());
    frame_buffer.read_frame(metadata, frame_copy);
    const std::uint32_t frame_width = metadata.width;
    const std::uint32_t frame_height = metadata.height;
    const std::size_t stride_pixels = frame_buffer.stride_pixels();

    const std::uint32_t display_width = metadata.display_width;
    const std::uint32_t display_height = metadata.display_height;
    const std::uint32_t border = border_pixels;
    const std::uint32_t output_width = display_width + 2 * border;
    const std::uint32_t output_height = display_height + 2 * border;

    // Black, opaque: the border stays so.
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(output_width) * output_height * 4, 0);
    for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 0xFF;

    // One display-sized content pixel (BGRA32) into the bordered region.
    auto put_pixel = [&](std::uint32_t x, std::uint32_t y, std::uint32_t bgra) {
        const std::size_t dst_idx =
            (static_cast<std::size_t>(y + border) * output_width + (x + border)) * 4;
        rgba[dst_idx + 0] = static_cast<std::uint8_t>((bgra >> 16) & 0xFF);  // R
        rgba[dst_idx + 1] = static_cast<std::uint8_t>((bgra >> 8) & 0xFF);   // G
        rgba[dst_idx + 2] = static_cast<std::uint8_t>(bgra & 0xFF);          // B
        rgba[dst_idx + 3] = static_cast<std::uint8_t>((bgra >> 24) & 0xFF);  // A
    };

    if (crop_auto) {
        // See ScreenshotCrop.hpp / ImageResample.hpp.
        const CropRect src_rect =
            compute_auto_crop(frame_copy.data(), frame_width, frame_height, stride_pixels);
        const std::vector<std::uint32_t> content = resample_bgra_lanczos3(
            frame_copy.data(), stride_pixels, src_rect.x, src_rect.y, src_rect.width,
            src_rect.height, display_width, display_height);
        for (std::uint32_t y = 0; y < display_height; ++y) {
            for (std::uint32_t x = 0; x < display_width; ++x) {
                put_pixel(x, y, content[static_cast<std::size_t>(y) * display_width + x]);
            }
        }
    } else {
        for (std::uint32_t y = 0; y < display_height; ++y) {
            std::uint32_t src_y = y * frame_height / display_height;
            if (src_y >= frame_height) src_y = frame_height - 1;
            for (std::uint32_t x = 0; x < display_width; ++x) {
                std::uint32_t src_x = x * frame_width / display_width;
                if (src_x >= frame_width) src_x = frame_width - 1;
                put_pixel(x, y, frame_copy[src_y * stride_pixels + src_x]);
            }
        }
    }

    std::string png;
    const int encoded = stbi_write_png_to_func(
        [](void* context, void* data, int size) {
            static_cast<std::string*>(context)->append(static_cast<const char*>(data),
                                                       static_cast<std::size_t>(size));
        },
        &png, static_cast<int>(output_width), static_cast<int>(output_height), 4, rgba.data(),
        static_cast<int>(output_width * 4));
    if (!encoded) throw std::runtime_error("cannot encode the screen as PNG");
    write_text_atomically(filepath, png);
    return true;
}

}  // namespace beebium::server
