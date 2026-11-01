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

#ifndef BEEBIUM_LINUX_CONNECTION_PICTURE_GEOMETRY_HPP
#define BEEBIUM_LINUX_CONNECTION_PICTURE_GEOMETRY_HPP

#include "connection/DecodedFrame.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace beebium::linux_client {

// Where the picture goes, from server geometry only, never from a mode number
// (client specification 6.2). The display shader applies the same rules per
// fragment; these are the reference it is tested against.

// The BBC PAL CRT's pixel aspect (authentic), and square pixels (crisp);
// client specification 6.3.
inline constexpr double AUTHENTIC_PIXEL_ASPECT = 0.96;
inline constexpr double SQUARE_PIXEL_ASPECT = 1.0;

// Split-screen bands a renderer honours; the specification requires eight.
inline constexpr std::size_t MAX_DISPLAY_REGIONS = 8;

// Width over height of the picture as it should appear. Progressive (bitmap)
// modes are line-doubled, so 256 scanlines fill the height ~500 interlaced
// teletext lines do and switching mode keeps the apparent picture size.
double picture_aspect(std::uint32_t display_width, std::uint32_t display_height,
                      bool interlaced, double pixel_aspect);

// The aspect-fitted picture inside a drawable, letterboxed or pillarboxed,
// never stretched. x, y, width and height are in drawable pixels from the
// top-left; scale_x and scale_y are the same rectangle as half-extents of a
// centred unit quad in normalised device coordinates.
struct PictureFit {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double scale_x = 0.0;
    double scale_y = 0.0;
};

PictureFit fit_picture(double aspect, double drawable_width, double drawable_height);

// The logical pixel width of a scanline: its region's, when the frame has
// several regions, else the texture's own width. Only the first
// MAX_DISPLAY_REGIONS regions count, as in the shader.
std::uint32_t pixel_width_for_scanline(std::span<const FrameRegion> regions,
                                       std::uint32_t scanline,
                                       std::uint32_t texture_width);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_PICTURE_GEOMETRY_HPP
