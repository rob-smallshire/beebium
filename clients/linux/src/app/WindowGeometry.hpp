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

#ifndef BEEBIUM_LINUX_APP_WINDOW_GEOMETRY_HPP
#define BEEBIUM_LINUX_APP_WINDOW_GEOMETRY_HPP

#include <QRect>
#include <QSize>

namespace beebium::linux_client {

struct WindowGeometry {
    // The picture a window opens with where it fits: twice the BBC's 640 by
    // 512, at 5:4.
    static constexpr QSize PREFERRED_PICTURE{1280, 1024};
    // The display's own minimum, below which no window shrinks it.
    static constexpr QSize LEAST_PICTURE{320, 256};
    // What the window manager adds above the window (labwc's title bar is
    // about 30 px), and the desktop panel's allowance where the platform
    // does not report it (Wayland reports the whole screen as available;
    // Raspberry Pi OS's panel is 36 px).
    static constexpr int DECORATION_HEIGHT = 40;
    static constexpr int PANEL_HEIGHT = 48;
    static constexpr int SIDE_MARGIN = 16;
};

// The size a machine window opens at, given its screen, the part of it the
// desktop leaves free, the height of everything in the window but the
// picture, and the width beside it (the sidebar's, if open): the preferred
// picture if it fits, else one shrunk at 5:4 until the window, its title bar
// and the panel fit; never below the least picture. The renderer letterboxes
// the picture in whatever the window is given, so this decides only the
// window.
QSize machine_window_size(const QRect& screen, const QRect& available, int chrome_height,
                          int side_width);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_WINDOW_GEOMETRY_HPP
