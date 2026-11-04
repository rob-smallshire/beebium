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

// A command line's requests, as one client hands them to another.

#include "app/Application.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("Options and positional arguments become explicit requests", "[arguments]") {
    const QStringList requests = launch_requests(
        {QStringLiteral("host-a:48875")}, {QStringLiteral("model-b")},
        {QStringLiteral("beebium://connect?host=a&port=1"), QStringLiteral("Elite.beebium-machine"),
         QStringLiteral("/abs/Other.beebium-machine")},
        QStringLiteral("/home/me"));
    CHECK(requests == QStringList{QStringLiteral("--connect"), QStringLiteral("host-a:48875"),
                                  QStringLiteral("--new-machine"), QStringLiteral("model-b"),
                                  QStringLiteral("beebium://connect?host=a&port=1"),
                                  QStringLiteral("--open"),
                                  QStringLiteral("/home/me/Elite.beebium-machine"),
                                  QStringLiteral("--open"),
                                  QStringLiteral("/abs/Other.beebium-machine")});
}

TEST_CASE("No arguments are no requests", "[arguments]") {
    CHECK(launch_requests({}, {}, {}, QStringLiteral("/")).isEmpty());
}

TEST_CASE("A preset file is a request to launch it, a machine's document to open it",
          "[arguments]") {
    const QStringList requests = launch_requests(
        {}, {},
        {QStringLiteral("Mine.beebium-preset"), QStringLiteral("Elite.beebium-machine/Elite.beebium-machine")},
        QStringLiteral("/home/u"));
    CHECK(requests == QStringList{QStringLiteral("--preset-file"),
                                  QStringLiteral("/home/u/Mine.beebium-preset"),
                                  QStringLiteral("--open"),
                                  QStringLiteral("/home/u/Elite.beebium-machine/Elite.beebium-machine")});
}
