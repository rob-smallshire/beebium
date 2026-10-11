// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include <catch2/catch_test_macros.hpp>
#include "TimeParser.hpp"

using namespace beebium;

//////////////////////////////////////////////////////////////////////////////
// parse_absolute_time tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("parse_absolute_time valid inputs", "[time-parser][absolute]") {
    SECTION("Standard format YYYY-MM-DDThh:mm") {
        auto dt = parse_absolute_time("1985-03-15T10:30");
        REQUIRE(dt.year == 1985);
        REQUIRE(dt.month == 3);
        REQUIRE(dt.day == 15);
        REQUIRE(dt.hour == 10);
        REQUIRE(dt.minute == 30);
    }

    SECTION("With seconds (ignored)") {
        auto dt = parse_absolute_time("1985-03-15T10:30:45");
        REQUIRE(dt.year == 1985);
        REQUIRE(dt.month == 3);
        REQUIRE(dt.day == 15);
        REQUIRE(dt.hour == 10);
        REQUIRE(dt.minute == 30);
    }

    SECTION("Midnight") {
        auto dt = parse_absolute_time("2000-01-01T00:00");
        REQUIRE(dt.year == 2000);
        REQUIRE(dt.hour == 0);
        REQUIRE(dt.minute == 0);
    }

    SECTION("End of day") {
        auto dt = parse_absolute_time("1999-12-31T23:59");
        REQUIRE(dt.year == 1999);
        REQUIRE(dt.month == 12);
        REQUIRE(dt.day == 31);
        REQUIRE(dt.hour == 23);
        REQUIRE(dt.minute == 59);
    }

    SECTION("Lowercase t separator") {
        auto dt = parse_absolute_time("1985-03-15t10:30");
        REQUIRE(dt.year == 1985);
    }

    SECTION("Compact format (no colon in time)") {
        auto dt = parse_absolute_time("1985-06-15T1430");
        REQUIRE(dt.year == 1985);
        REQUIRE(dt.month == 6);
        REQUIRE(dt.day == 15);
        REQUIRE(dt.hour == 14);
        REQUIRE(dt.minute == 30);
    }
}

TEST_CASE("parse_absolute_time invalid inputs", "[time-parser][absolute]") {
    SECTION("Too short") {
        REQUIRE_THROWS_AS(parse_absolute_time("1985"), std::invalid_argument);
    }

    SECTION("Bad separator") {
        REQUIRE_THROWS_AS(parse_absolute_time("1985/03/15T10:30"), std::invalid_argument);
    }

    SECTION("Month out of range") {
        REQUIRE_THROWS_AS(parse_absolute_time("1985-13-15T10:30"), std::invalid_argument);
    }

    SECTION("Hour out of range") {
        REQUIRE_THROWS_AS(parse_absolute_time("1985-03-15T25:30"), std::invalid_argument);
    }

    SECTION("Minute out of range") {
        REQUIRE_THROWS_AS(parse_absolute_time("1985-03-15T10:61"), std::invalid_argument);
    }
}

//////////////////////////////////////////////////////////////////////////////
// apply_relative_offset tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("apply_relative_offset", "[time-parser][relative]") {
    const Saf3019p::DateTime base{2026, 3, 15, 22, 30};

    SECTION("Negative year offset") {
        auto dt = apply_relative_offset("-10y", base);
        REQUIRE(dt.year == 2016);
        REQUIRE(dt.month == 3);
        REQUIRE(dt.day == 15);
    }

    SECTION("Positive hour offset rolls over to the next day") {
        auto dt = apply_relative_offset("+5h", base);
        REQUIRE(dt.day == 16);
        REQUIRE(dt.hour == 3);
        REQUIRE(dt.minute == 30);
    }

    SECTION("Combined offset") {
        // -1y6M subtracts 1 year and 6 months; the month normalises into the
        // previous year.
        auto dt = apply_relative_offset("-1y6M", base);
        REQUIRE(dt.year == 2024);
        REQUIRE(dt.month == 9);
    }
}

TEST_CASE("apply_relative_offset invalid inputs", "[time-parser][relative]") {
    const Saf3019p::DateTime base{2026, 3, 15, 22, 30};

    SECTION("Empty string") {
        REQUIRE_THROWS_AS(apply_relative_offset("", base), std::invalid_argument);
    }

    SECTION("Missing unit") {
        REQUIRE_THROWS_AS(apply_relative_offset("-10", base), std::invalid_argument);
    }

    SECTION("Unknown unit") {
        REQUIRE_THROWS_AS(apply_relative_offset("-10x", base), std::invalid_argument);
    }
}

//////////////////////////////////////////////////////////////////////////////
// current_local_datetime tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("current_local_datetime returns plausible values", "[time-parser]") {
    auto dt = current_local_datetime();
    REQUIRE(dt.year >= 2020);
    REQUIRE(dt.month >= 1);
    REQUIRE(dt.month <= 12);
    REQUIRE(dt.day >= 1);
    REQUIRE(dt.day <= 31);
    REQUIRE(dt.hour >= 0);
    REQUIRE(dt.hour <= 23);
    REQUIRE(dt.minute >= 0);
    REQUIRE(dt.minute <= 59);
}
