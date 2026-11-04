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

#ifndef BEEBIUM_LINUX_DISPLAY_VIDEO_SETTINGS_HPP
#define BEEBIUM_LINUX_DISPLAY_VIDEO_SETTINGS_HPP

#include <cstdint>

namespace beebium::linux_client {

// How a window shows its machine's picture (client specification 6.3, 6.6),
// as the macOS client's: a client concern, per window.

enum class DisplayStyle {
    standard,  // the active picture only, inside an optional edge margin
    debug,     // the active picture inside the CRTC borders, each side its own colour
};

enum class PixelShape {
    authentic,  // the BBC PAL CRT's, a little narrower than square
    crisp,      // square
};

// An opaque sRGB colour.
struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend bool operator==(const Rgb&, const Rgb&) = default;
};

struct VideoSettings {
    static constexpr int MAX_EDGE_MARGIN_PERCENT = 10;

    DisplayStyle style = DisplayStyle::standard;
    PixelShape pixels = PixelShape::authentic;
    // Around the picture, where the window is wider or taller than it.
    Rgb boxing{15, 15, 15};
    // Standard's edge margin: this percent of each edge of the picture's
    // rectangle, in its own colour, the picture inside it scaled to fit.
    int edge_margin_percent = 2;
    Rgb edge_margin_colour{0, 0, 0};

    friend bool operator==(const VideoSettings&, const VideoSettings&) = default;
};

// Width over height of one pixel.
double pixel_aspect(PixelShape shape);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_DISPLAY_VIDEO_SETTINGS_HPP
