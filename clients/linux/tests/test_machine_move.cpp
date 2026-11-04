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

// Moving a saved machine (client specification 5.4): where it goes, and the
// sentence a refusal is told in, as the macOS client words them.

#include "app/MachineMove.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("A machine moves into the chosen folder under its own name", "[move]") {
    CHECK(move_destination(QStringLiteral("/home/me/games"), QStringLiteral("Elite")) ==
          QStringLiteral("/home/me/games/Elite.beebium-machine"));
    CHECK(move_destination(QStringLiteral("/home/me/games/"), QStringLiteral("Elite")) ==
          QStringLiteral("/home/me/games/Elite.beebium-machine"));
}

TEST_CASE("A refused move is told in a sentence chosen by the server's code", "[move]") {
    CHECK(move_failure_sentence(grpc::StatusCode::ALREADY_EXISTS, QStringLiteral("Elite")) ==
          QStringLiteral("A machine named “Elite” already exists in that folder."));
    CHECK(move_failure_sentence(grpc::StatusCode::NOT_FOUND, QStringLiteral("Elite")) ==
          QStringLiteral("The destination folder could not be found."));
    CHECK(move_failure_sentence(grpc::StatusCode::INVALID_ARGUMENT, QStringLiteral("Elite")) ==
          QStringLiteral("That location can’t be used."));
    CHECK(move_failure_sentence(grpc::StatusCode::INTERNAL, QStringLiteral("Elite")) ==
          QStringLiteral("Couldn’t move the machine."));
}
