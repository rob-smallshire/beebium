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

// The order lists of names are read in, the same in every locale.

#include "text/NaturalOrder.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("Names compare as a person reads them, in any locale", "[text][natural-order]") {
    // Numbers by value.
    CHECK(natural_compare(QStringLiteral("B+ 2"), QStringLiteral("B+ 10")) < 0);
    CHECK(natural_compare(QStringLiteral("B+ 10"), QStringLiteral("B+ 2")) > 0);
    CHECK(natural_compare(QStringLiteral("Disc 9 side 2"), QStringLiteral("Disc 9 side 10")) < 0);
    // Equal values: the fewer leading zeros first.
    CHECK(natural_compare(QStringLiteral("Disc 2"), QStringLiteral("Disc 02")) < 0);
    CHECK(natural_compare(QStringLiteral("Disc 02"), QStringLiteral("Disc 10")) < 0);
    // Letters without regard to case.
    CHECK(natural_compare(QStringLiteral("acorn"), QStringLiteral("BBC")) < 0);
    CHECK(natural_compare(QStringLiteral("Model b"), QStringLiteral("Model B")) == 0);
    // A prefix first.
    CHECK(natural_compare(QStringLiteral("Model B"), QStringLiteral("Model B+")) < 0);
    CHECK(natural_compare(QStringLiteral("Elite"), QStringLiteral("Elite")) == 0);
}
