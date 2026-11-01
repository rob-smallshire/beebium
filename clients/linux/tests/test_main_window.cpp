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

#include "app/MainWindow.hpp"

#include <QMenuBar>

#include <catch2/catch_test_macros.hpp>

using beebium::linux_client::MainWindow;

TEST_CASE("A machine window has a menu bar and a display area", "[app][window]") {
    MainWindow window;

    REQUIRE(window.menuWidget() != nullptr);
    CHECK(window.menuBar()->actions().isEmpty());

    REQUIRE(window.centralWidget() != nullptr);
    CHECK(window.centralWidget()->objectName() == QStringLiteral("displayPlaceholder"));
}

TEST_CASE("A machine window shows under the offscreen platform", "[app][window]") {
    MainWindow window;
    window.show();
    CHECK(window.isVisible());
}
