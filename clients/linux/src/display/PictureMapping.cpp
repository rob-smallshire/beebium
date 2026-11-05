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

#include "display/PictureMapping.hpp"

#include <algorithm>
#include <cmath>

namespace beebium::linux_client {

ScreenContent screen_content(const DecodedFrame& frame) {
    return {frame.width,       frame.height,       frame.display_width, frame.display_height,
            frame.left_border, frame.right_border, frame.top_border,    frame.bottom_border,
            frame.interlaced,  frame.regions};
}

namespace {

DecodedFrame shape_only(const ScreenContent& content) {
    DecodedFrame frame;
    frame.width = content.width;
    frame.height = content.height;
    frame.display_width = content.display_width;
    frame.display_height = content.display_height;
    frame.left_border = content.left_border;
    frame.right_border = content.right_border;
    frame.top_border = content.top_border;
    frame.bottom_border = content.bottom_border;
    frame.interlaced = content.interlaced;
    frame.regions = content.regions;
    return frame;
}

}  // namespace

PictureMapping::PictureMapping(const ScreenContent& content,
                               const ScreenPresentation& presentation)
    : content_(content),
      uniforms_(make_display_uniforms(shape_only(content), presentation.settings,
                                      presentation.width, presentation.height, false)) {
    // The quad is centred, its half-extents a fraction of the view's.
    quad_.width = presentation.width * uniforms_.quad[0];
    quad_.height = presentation.height * uniforms_.quad[1];
    quad_.x = (presentation.width - quad_.width) / 2.0;
    quad_.y = (presentation.height - quad_.height) / 2.0;
}

double PictureMapping::pixel_width(double frame_y) const {
    const auto last = static_cast<long>(content_.height) - 1;
    const auto scanline =
        static_cast<std::uint32_t>(std::clamp(static_cast<long>(std::floor(frame_y)), 0L,
                                              std::max(0L, last)));
    return pixel_width_for_scanline(content_.regions, scanline, content_.width);
}

std::optional<FrameHit> PictureMapping::frame_hit(ViewPoint point) const {
    if (quad_.width <= 0.0 || quad_.height <= 0.0 || point.x < quad_.x ||
        point.y < quad_.y || point.x > quad_.x + quad_.width ||
        point.y > quad_.y + quad_.height) {
        return std::nullopt;
    }
    // As the fragment shader: the quad's uv, then the active picture's.
    const double quad_u = (point.x - quad_.x) / quad_.width;
    const double quad_v = (point.y - quad_.y) / quad_.height;
    const double u = (quad_u - uniforms_.inner[0]) / (uniforms_.inner[2] - uniforms_.inner[0]);
    const double v = (quad_v - uniforms_.inner[1]) / (uniforms_.inner[3] - uniforms_.inner[1]);
    FrameHit hit;
    hit.inside_active = u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0;
    hit.pixel.y = std::clamp(v, 0.0, 1.0) * content_.height;
    hit.pixel.x = std::clamp(u, 0.0, 1.0) * pixel_width(hit.pixel.y);
    return hit;
}

ViewPoint PictureMapping::view_point(FramePoint pixel) const {
    const double width = pixel_width(pixel.y);
    const double u = width > 0.0 ? pixel.x / width : 0.0;
    const double v = content_.height > 0 ? pixel.y / content_.height : 0.0;
    const double quad_u = uniforms_.inner[0] + u * (uniforms_.inner[2] - uniforms_.inner[0]);
    const double quad_v = uniforms_.inner[1] + v * (uniforms_.inner[3] - uniforms_.inner[1]);
    return {quad_.x + quad_u * quad_.width, quad_.y + quad_v * quad_.height};
}

ViewRect PictureMapping::view_rect(const PixelRect& rect) const {
    // Both corners by the band the rect's top is in, so its far edge is not
    // read against the next band down.
    const double width = pixel_width(rect.y);
    const ViewPoint top_left = view_point({static_cast<double>(rect.x), static_cast<double>(rect.y)});
    const double scale_x =
        width > 0.0 ? quad_.width * (uniforms_.inner[2] - uniforms_.inner[0]) / width : 0.0;
    const double scale_y = content_.height > 0 ? quad_.height *
                                                     (uniforms_.inner[3] - uniforms_.inner[1]) /
                                                     content_.height
                                               : 0.0;
    return {top_left.x, top_left.y, rect.width * scale_x, rect.height * scale_y};
}

}  // namespace beebium::linux_client
