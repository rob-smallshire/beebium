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

// What closing a window does to its server, client specification 4.5.

#include "app/CloseDecision.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("A server this client did not launch is only disconnected from", "[close]") {
    CHECK(close_action(false, 1) == CloseAction::disconnect);
    CHECK(close_action(false, 3) == CloseAction::disconnect);
}

TEST_CASE("Our server with no other client is shut down", "[close]") {
    CHECK(close_action(true, 1) == CloseAction::shut_down);
    CHECK(close_action(true, 0) == CloseAction::shut_down);
}

TEST_CASE("Our server with other clients asks the user", "[close]") {
    CHECK(close_action(true, 2) == CloseAction::ask);
}

TEST_CASE("Quitting leaves a shared server running without asking", "[close]") {
    CHECK(quit_action(true, 2) == CloseAction::disconnect);
    CHECK(quit_action(true, 1) == CloseAction::shut_down);
    CHECK(quit_action(false, 1) == CloseAction::disconnect);
}

TEST_CASE("A chosen save path becomes a machine directory", "[close][save]") {
    CHECK(machine_dirpath_for(QStringLiteral("/home/me/Elite")) ==
          "/home/me/Elite.beebium-machine");
    CHECK(machine_dirpath_for(QStringLiteral("/home/me/Elite.beebium-machine")) ==
          "/home/me/Elite.beebium-machine");
    CHECK(machine_dirpath_for(QStringLiteral("/home/me/Elite.beebium-machine/")) ==
          "/home/me/Elite.beebium-machine");
}
