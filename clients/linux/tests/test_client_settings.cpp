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

#include "app/ClientSettings.hpp"

#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("Remembered connections persist across runs, most recent first",
          "[settings]") {
    QTemporaryDir directory;
    const QString filepath = directory.filePath(QStringLiteral("Beebium.conf"));
    {
        ClientSettings settings(filepath);
        settings.remember_connection({"localhost", 48875});
        settings.remember_connection({"pi400.local", 48875});
        settings.remember_connection({"localhost", 48875});
    }
    ClientSettings reopened(filepath);
    const auto recents = reopened.recent_connections();
    REQUIRE(recents.size() == 2);
    CHECK(recents[0] == Endpoint{"localhost", 48875});
    CHECK(recents[1] == Endpoint{"pi400.local", 48875});
}

TEST_CASE("The recents are capped", "[settings]") {
    QTemporaryDir directory;
    ClientSettings settings(directory.filePath(QStringLiteral("Beebium.conf")));
    for (int port = 1; port <= 15; ++port) {
        settings.remember_connection({"host", static_cast<std::uint16_t>(port)});
    }
    const auto recents = settings.recent_connections();
    CHECK(recents.size() == ClientSettings::RECENT_CONNECTIONS_CAP);
    CHECK(recents.front().port == 15);
}

TEST_CASE("An entry that no longer parses is skipped", "[settings]") {
    QTemporaryDir directory;
    const QString filepath = directory.filePath(QStringLiteral("Beebium.conf"));
    {
        QSettings raw(filepath, QSettings::IniFormat);
        raw.setValue(QStringLiteral("connections/recent"),
                     QStringList{QStringLiteral("good:48875"), QStringLiteral("bad:0")});
    }
    ClientSettings settings(filepath);
    REQUIRE(settings.recent_connections().size() == 1);
}
