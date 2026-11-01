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

#include "app/WindowGeometry.hpp"

#include <algorithm>

namespace beebium::linux_client {

QSize machine_window_size(const QRect& screen, const QRect& available, int chrome_height,
                          int side_width) {
    const int reserve = WindowGeometry::DECORATION_HEIGHT +
                        (available.size() == screen.size() ? WindowGeometry::PANEL_HEIGHT : 0);
    const int room_height = available.height() - reserve - chrome_height;
    const int room_width = available.width() - WindowGeometry::SIDE_MARGIN - side_width;
    // The tallest 5:4 picture that fits both ways, in whole steps of 5 by 4.
    const int steps = std::min({WindowGeometry::PREFERRED_PICTURE.height() / 4, room_height / 4,
                                room_width / 5});
    const int least_steps = WindowGeometry::LEAST_PICTURE.height() / 4;
    const int chosen = std::max(steps, least_steps);
    return {5 * chosen + side_width, 4 * chosen + chrome_height};
}

}  // namespace beebium::linux_client
