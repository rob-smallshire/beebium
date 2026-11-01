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

#include "connection/PictureGeometry.hpp"

#include <algorithm>

namespace beebium::linux_client {

double picture_aspect(std::uint32_t display_width, std::uint32_t display_height,
                      bool interlaced, double pixel_aspect) {
    if (display_width == 0 || display_height == 0) {
        return 0.0;
    }
    const double width = static_cast<double>(display_width) * pixel_aspect;
    const double height = static_cast<double>(display_height) * (interlaced ? 1.0 : 2.0);
    return width / height;
}

PictureFit fit_picture(double aspect, double drawable_width, double drawable_height) {
    PictureFit fit;
    if (aspect <= 0.0 || drawable_width <= 0.0 || drawable_height <= 0.0) {
        return fit;
    }
    const double drawable_aspect = drawable_width / drawable_height;
    if (drawable_aspect > aspect) {
        // Wider than the picture: pillarbox.
        fit.scale_x = aspect / drawable_aspect;
        fit.scale_y = 1.0;
    } else {
        // Taller than the picture: letterbox.
        fit.scale_x = 1.0;
        fit.scale_y = drawable_aspect / aspect;
    }
    fit.width = drawable_width * fit.scale_x;
    fit.height = drawable_height * fit.scale_y;
    fit.x = (drawable_width - fit.width) / 2.0;
    fit.y = (drawable_height - fit.height) / 2.0;
    return fit;
}

std::uint32_t pixel_width_for_scanline(std::span<const FrameRegion> regions,
                                       std::uint32_t scanline,
                                       std::uint32_t texture_width) {
    if (regions.size() <= 1) {
        return texture_width;
    }
    const auto count = std::min(regions.size(), MAX_DISPLAY_REGIONS);
    for (std::size_t i = 0; i < count; ++i) {
        if (scanline >= regions[i].start_line && scanline < regions[i].end_line) {
            return regions[i].pixel_width;
        }
    }
    return texture_width;
}

}  // namespace beebium::linux_client
