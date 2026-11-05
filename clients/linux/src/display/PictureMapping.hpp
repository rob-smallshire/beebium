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

#ifndef BEEBIUM_LINUX_DISPLAY_PICTURE_MAPPING_HPP
#define BEEBIUM_LINUX_DISPLAY_PICTURE_MAPPING_HPP

#include "connection/DecodedFrame.hpp"
#include "connection/ScreenSelection.hpp"
#include "display/DisplayUniforms.hpp"
#include "display/VideoSettings.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace beebium::linux_client {

// Where a view point falls in the frame and back (client specification
// 6.5): the exact inverse of how the display draws a frame, taken from the
// same uniforms, so a selection's highlight sits on the glyphs it names.
//
// The two halves move separately. The content is the frame being read and
// shown, a held still for a selection; the presentation is the widget as it
// is now. A resize or a change of style or pixel shape mid-selection lays
// the same content out afresh, and the highlight stays registered.

// A frame's shape, without its pixels.
struct ScreenContent {
    std::uint32_t width = 0;   // texels
    std::uint32_t height = 0;  // scanlines
    std::uint32_t display_width = 0;
    std::uint32_t display_height = 0;
    std::uint32_t left_border = 0;
    std::uint32_t right_border = 0;
    std::uint32_t top_border = 0;
    std::uint32_t bottom_border = 0;
    bool interlaced = false;
    std::vector<FrameRegion> regions;
};

ScreenContent screen_content(const DecodedFrame& frame);

// The view the content is drawn into: its size, in the same units as the
// points mapped, and the window's video settings.
struct ScreenPresentation {
    double width = 0.0;
    double height = 0.0;
    VideoSettings settings;
};

// In view units from the view's top-left.
struct ViewPoint {
    double x = 0.0;
    double y = 0.0;
};

struct ViewRect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

// A view point's frame pixel: inside the active area, or clamped to its
// nearest edge from a border or margin around it, so a drag past the text
// still selects to the edge.
struct FrameHit {
    FramePoint pixel;
    bool inside_active = false;
};

class PictureMapping {
public:
    PictureMapping(const ScreenContent& content, const ScreenPresentation& presentation);

    // Nothing outside the picture: the boxing around it is not the frame's.
    std::optional<FrameHit> frame_hit(ViewPoint point) const;

    ViewPoint view_point(FramePoint pixel) const;

    // A rect within one band, mapped by that band's pixel width.
    ViewRect view_rect(const PixelRect& rect) const;

    // The picture as drawn, the margin or borders included.
    ViewRect picture_rect() const { return quad_; }

private:
    double pixel_width(double frame_y) const;

    ScreenContent content_;
    DisplayUniforms uniforms_;
    ViewRect quad_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_DISPLAY_PICTURE_MAPPING_HPP
