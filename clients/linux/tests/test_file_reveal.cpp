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

// Showing a file on the desktop: selected by the file manager when one
// answers the freedesktop call, else its folder opened; and when a path the
// server reported may be revealed here at all.

#include "app/FileReveal.hpp"

#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <optional>

using namespace beebium::linux_client;

TEST_CASE("A file is shown selected when the file manager answers", "[reveal]") {
    QString asked;
    std::optional<QUrl> opened;
    const RevealRoutes routes{[&](const QString& uri) {
                                  asked = uri;
                                  return true;
                              },
                              [&](const QUrl& folder) { opened = folder; }};
    CHECK(reveal_file(QStringLiteral("/home/me/discs/Elite.ssd"), routes) ==
          Revealed::selected_in_file_manager);
    CHECK(asked == QStringLiteral("file:///home/me/discs/Elite.ssd"));
    CHECK_FALSE(opened.has_value());
}

TEST_CASE("Without a file manager answering, the file's folder is opened", "[reveal]") {
    std::optional<QUrl> opened;
    const RevealRoutes routes{[](const QString&) { return false; },
                              [&](const QUrl& folder) { opened = folder; }};
    CHECK(reveal_file(QStringLiteral("/home/me/discs/Elite.ssd"), routes) ==
          Revealed::folder_opened);
    REQUIRE(opened.has_value());
    CHECK(*opened == QUrl::fromLocalFile(QStringLiteral("/home/me/discs")));
}

TEST_CASE("A path is revealed only from a server sharing this filesystem, and if it exists here",
          "[reveal]") {
    QTemporaryDir directory;
    const QString here = directory.filePath(QStringLiteral("Elite.ssd"));
    QFile file(here);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.close();
    CHECK(reveal_refusal(here, true).isEmpty());
    CHECK(reveal_refusal(here, false) == QStringLiteral("The server is on another host"));
    CHECK(reveal_refusal(directory.filePath(QStringLiteral("gone.ssd")), true) ==
          QStringLiteral("The file is not on this computer"));
    CHECK(reveal_refusal(QString(), true) == QStringLiteral("The file is not on this computer"));
}
