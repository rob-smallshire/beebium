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

#include <algorithm>

namespace beebium::linux_client {

namespace {

// The shader's own safety net; the controls stop at MAX_EDGE_MARGIN_PERCENT.
constexpr double MAX_EDGE_MARGIN = 0.45;

// Debug's borders, top, bottom, left, right, as the macOS client paints
// them: dark blue, dark yellow, dark red, dark green.
constexpr float DEBUG_BORDER_COLOURS[4][4] = {
    {0.0f, 0.0f, 0.5f, 1.0f}, {0.5f, 0.5f, 0.0f, 1.0f}, {0.5f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.5f, 0.0f, 1.0f}};

void set_colour(float (&target)[4], const Rgb& colour) {
    target[0] = static_cast<float>(colour.r) / 255.0f;
    target[1] = static_cast<float>(colour.g) / 255.0f;
    target[2] = static_cast<float>(colour.b) / 255.0f;
    target[3] = 1.0f;
}

}  // namespace

DisplayUniforms make_display_uniforms(const DecodedFrame& frame, const VideoSettings& settings,
                                      double drawable_width, double drawable_height,
                                      bool ndc_y_up) {
    DisplayUniforms uniforms;
    const bool debug = settings.style == DisplayStyle::debug;
    const std::uint32_t total_width =
        debug ? frame.left_border + frame.display_width + frame.right_border : frame.display_width;
    const std::uint32_t total_height = debug
                                           ? frame.top_border + frame.display_height +
                                                 frame.bottom_border
                                           : frame.display_height;
    const double aspect = picture_aspect(total_width, total_height, frame.interlaced,
                                         pixel_aspect(settings.pixels));
    const PictureFit fit = fit_picture(aspect, drawable_width, drawable_height);
    uniforms.quad[0] = static_cast<float>(fit.scale_x);
    uniforms.quad[1] = static_cast<float>(fit.scale_y);
    uniforms.quad[2] = ndc_y_up ? 1.0f : -1.0f;

    if (debug) {
        if (total_width != 0 && total_height != 0) {
            const auto w = static_cast<float>(total_width);
            const auto h = static_cast<float>(total_height);
            uniforms.inner[0] = static_cast<float>(frame.left_border) / w;
            uniforms.inner[1] = static_cast<float>(frame.top_border) / h;
            uniforms.inner[2] = static_cast<float>(frame.left_border + frame.display_width) / w;
            uniforms.inner[3] = static_cast<float>(frame.top_border + frame.display_height) / h;
        }
        std::copy(&DEBUG_BORDER_COLOURS[0][0], &DEBUG_BORDER_COLOURS[0][0] + 16,
                  &uniforms.band_colours[0][0]);
    } else {
        // The margin is the same fraction of each edge, so the picture inside
        // it keeps the quad's aspect.
        const auto margin = static_cast<float>(
            std::clamp(settings.edge_margin_percent / 100.0, 0.0, MAX_EDGE_MARGIN));
        uniforms.inner[0] = margin;
        uniforms.inner[1] = margin;
        uniforms.inner[2] = 1.0f - margin;
        uniforms.inner[3] = 1.0f - margin;
        for (auto& side : uniforms.band_colours) {
            set_colour(side, settings.edge_margin_colour);
        }
    }

    uniforms.texture_size[0] = static_cast<float>(frame.width);
    uniforms.texture_size[1] = static_cast<float>(frame.height);

    const auto count = std::min(frame.regions.size(), MAX_DISPLAY_REGIONS);
    uniforms.region_info[0] = static_cast<std::int32_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        uniforms.regions[i][0] = static_cast<std::int32_t>(frame.regions[i].start_line);
        uniforms.regions[i][1] = static_cast<std::int32_t>(frame.regions[i].end_line);
        uniforms.regions[i][2] = static_cast<std::int32_t>(frame.regions[i].pixel_width);
    }
    return uniforms;
}

}  // namespace beebium::linux_client
