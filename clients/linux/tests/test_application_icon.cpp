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

// The client's own icon: the installed one from the theme, or with no
// package installed the copy built into the program, never none.

#include "app/ApplicationInfo.hpp"

#include <QIcon>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("The application icon is Beebium's even with no icon theme to find it in",
          "[application][icon]") {
    QIcon::setThemeSearchPaths({});
    QIcon::setFallbackSearchPaths({});
    const QIcon icon = application_icon();
    REQUIRE_FALSE(icon.isNull());
    // From the panel's small sizes to a large one.
    const QList<QSize> sizes = icon.availableSizes();
    CHECK(sizes.contains(QSize(16, 16)));
    CHECK(sizes.contains(QSize(48, 48)));
    CHECK(sizes.contains(QSize(256, 256)));
    CHECK_FALSE(icon.pixmap(48, 48).isNull());
}
