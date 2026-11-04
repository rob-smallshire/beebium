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

#ifndef BEEBIUM_LINUX_DISPLAY_DISPLAY_UNIFORMS_HPP
#define BEEBIUM_LINUX_DISPLAY_DISPLAY_UNIFORMS_HPP

#include "connection/DecodedFrame.hpp"
#include "connection/PictureGeometry.hpp"
#include "display/VideoSettings.hpp"

#include <cstdint>

namespace beebium::linux_client {

// The display shader's uniform block, laid out as std140 so the same bytes
// serve SPIR-V and GLSL ES alike. Mirrors `buf` in shaders/display.vert and
// shaders/display.frag; change all three together.
struct DisplayUniforms {
    // xy: the picture's half-extents in normalised device coordinates.
    // z: +1 where NDC y points up (OpenGL), -1 where it points down (Vulkan).
    float quad[4] = {0, 0, 1, 0};
    // xy: the frame texture's size in texels.
    float texture_size[4] = {0, 0, 0, 0};
    // x: how many of `regions` are in use.
    std::int32_t region_info[4] = {0, 0, 0, 0};
    // Per region: x first scanline, y one past the last, z logical pixel width.
    std::int32_t regions[MAX_DISPLAY_REGIONS][4] = {};
    // The active picture within the quad, as u0, v0, u1, v1 (v from the top).
    float inner[4] = {0, 0, 1, 1};
    // What fills the quad outside `inner`: the edge margin's colour.
    float band_colour[4] = {0, 0, 0, 1};
};

static_assert(sizeof(DisplayUniforms) == 208, "std140 layout of the display uniform block");

// Fill the uniforms for drawing `frame` into a drawable of the given pixel
// size (client specification 6.2, 6.3): the aspect-fitted quad at the
// settings' pixel shape, the picture inset by the edge margin, and
// split-screen regions beyond the first eight dropped as the shader does.
DisplayUniforms make_display_uniforms(const DecodedFrame& frame, const VideoSettings& settings,
                                      double drawable_width, double drawable_height,
                                      bool ndc_y_up);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_DISPLAY_DISPLAY_UNIFORMS_HPP
