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

DisplayUniforms make_display_uniforms(const DecodedFrame& frame, double pixel_aspect,
                                      double drawable_width, double drawable_height,
                                      bool ndc_y_up) {
    DisplayUniforms uniforms;
    const double aspect = picture_aspect(frame.display_width, frame.display_height,
                                         frame.interlaced, pixel_aspect);
    const PictureFit fit = fit_picture(aspect, drawable_width, drawable_height);
    uniforms.quad[0] = static_cast<float>(fit.scale_x);
    uniforms.quad[1] = static_cast<float>(fit.scale_y);
    uniforms.quad[2] = ndc_y_up ? 1.0f : -1.0f;

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
