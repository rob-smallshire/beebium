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

// The size a machine window opens at: the picture at twice the BBC's 640
// by 512 beside the sidebar, unless that will not fit the screen, when the
// picture shrinks at 5:4 until the window, its title bar and the desktop's
// panel fit.

#include "app/WindowGeometry.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

// The menu bar, the status bar and the display's focus margins.
constexpr int CHROME = 60;
constexpr int SIDEBAR = 280;

int outer_height(QSize window, bool panel_known) {
    return window.height() + WindowGeometry::DECORATION_HEIGHT +
           (panel_known ? 0 : WindowGeometry::PANEL_HEIGHT);
}

}  // namespace

TEST_CASE("On a 1080p Wayland desktop the window, its title bar and the panel fit",
          "[window][geometry]") {
    // Wayland reports no panels: the available area is the whole screen.
    const QRect screen(0, 0, 1920, 1080);
    const QSize window = machine_window_size(screen, screen, CHROME, SIDEBAR);
    CHECK(outer_height(window, false) <= 1080);
    CHECK(window.width() <= 1920);
    // The picture shrank keeping 5:4.
    const int picture_height = window.height() - CHROME;
    const int picture_width = window.width() - SIDEBAR;
    CHECK(picture_width * 4 == picture_height * 5);
    CHECK(picture_height < 1024);
}

TEST_CASE("Where the panel is reported, only the title bar is allowed for", "[window][geometry]") {
    const QRect screen(0, 0, 1920, 1080);
    const QRect available(0, 0, 1920, 1044);  // a 36 px panel
    const QSize window = machine_window_size(screen, available, CHROME, SIDEBAR);
    CHECK(window.height() + WindowGeometry::DECORATION_HEIGHT <= 1044);
    CHECK(window.height() + WindowGeometry::DECORATION_HEIGHT > 1044 - 8);
}

TEST_CASE("A screen with room opens the picture at twice the BBC's size", "[window][geometry]") {
    const QRect screen(0, 0, 2560, 1440);
    CHECK(machine_window_size(screen, screen, CHROME, SIDEBAR) == QSize(1280 + SIDEBAR, 1024 + CHROME));
}

TEST_CASE("A narrow screen shrinks the picture to the width", "[window][geometry]") {
    const QRect screen(0, 0, 1024, 1080);
    const QSize window = machine_window_size(screen, screen, CHROME, SIDEBAR);
    CHECK(window.width() <= 1024);
    CHECK((window.width() - SIDEBAR) * 4 == (window.height() - CHROME) * 5);
}

TEST_CASE("A tiny screen still gets the display's least picture", "[window][geometry]") {
    const QRect screen(0, 0, 640, 400);
    const QSize window = machine_window_size(screen, screen, CHROME, SIDEBAR);
    CHECK(window.width() - SIDEBAR >= WindowGeometry::LEAST_PICTURE.width());
    CHECK(window.height() - CHROME >= WindowGeometry::LEAST_PICTURE.height());
}
