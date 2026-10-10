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

// The Acorn real-time clock's reading of its configuration, and the check it
// offers the framework before launch: a start the register layout cannot hold
// is found before the machine is built.

#include <catch2/catch_test_macros.hpp>

#include "AcornRtcConfig.hpp"

#include <map>
#include <string>

using namespace beebium;

namespace {

using Config = std::map<std::string, std::string>;
using Severity = ConfigDiagnostic::Severity;

constexpr Saf3019p::DateTime kNow{2026, 10, 10, 12, 0};

ExtensionManifest rtc_manifest() {
    ExtensionManifest m;
    m.name = "acorn-rtc";
    ParameterSchema layout{"layout", "string", "How the year is stored."};
    layout.choices = {{"4bit-year-in-r1", "Acorn original"},
                      {"7bit-year-in-r7", "Level 3 File Server 1.26"},
                      {"7bit-year-in-r1r5", "BeebMaster Y2KFIX"}};
    m.parameters.push_back(layout);
    return m;
}

std::vector<ConfigDiagnostic> check(const Config& config) {
    return check_acorn_rtc_config(rtc_manifest(), config, kNow);
}

}  // namespace

TEST_CASE("the RTC starts at its time, at now moved by its offset, or at now",
          "[acorn-rtc][config]") {
    const auto at = acorn_rtc_start_time({{"time", "1985-03-15T10:30"}}, kNow);
    CHECK(at.year == 1985);
    CHECK(at.month == 3);
    CHECK(at.minute == 30);
    CHECK(acorn_rtc_start_time({{"offset", "-30y"}}, kNow).year == 1996);
    CHECK(acorn_rtc_start_time({}, kNow).year == 2026);
    CHECK_THROWS_AS(acorn_rtc_start_time({{"time", "soon"}}, kNow), std::invalid_argument);
}

TEST_CASE("the RTC's layout is the one its config names, the Acorn original by default",
          "[acorn-rtc][config]") {
    CHECK(acorn_rtc_layout({}) == RegisterLayout::FourBitYearInR1);
    CHECK(acorn_rtc_layout({{"layout", "7bit-year-in-r7"}}) == RegisterLayout::SevenBitYearInR7);
    CHECK_THROWS_AS(acorn_rtc_layout({{"layout", "8bit"}}), std::invalid_argument);
}

TEST_CASE("a start time the layout holds is accepted", "[acorn-rtc][config]") {
    CHECK(check({{"time", "1985-03-15T10:30"}, {"layout", "4bit-year-in-r1"}}).empty());
    CHECK(check({{"time", "2001-01-01T00:00"}, {"layout", "7bit-year-in-r7"}}).empty());
}

TEST_CASE("a start time outside the layout's years is an error at the time",
          "[acorn-rtc][config]") {
    const auto found = check({{"time", "2001-01-01T00:00"}, {"layout", "4bit-year-in-r1"}});
    REQUIRE(found.size() == 1);
    CHECK(found[0].parameter == "time");
    CHECK(found[0].severity == Severity::Error);
    INFO(found[0].message);
    CHECK(found[0].message.find("2001") != std::string::npos);
    CHECK(found[0].message.find("1981-1996") != std::string::npos);
    CHECK(found[0].message.find("Acorn original") != std::string::npos);
}

TEST_CASE("a start time that is not a date is an error at the time", "[acorn-rtc][config]") {
    const auto found = check({{"time", "soon"}, {"layout", "4bit-year-in-r1"}});
    REQUIRE(found.size() == 1);
    CHECK(found[0].parameter == "time");
    CHECK(found[0].severity == Severity::Error);
}

TEST_CASE("an offset that is not an offset is an error at the offset", "[acorn-rtc][config]") {
    const auto found = check({{"offset", "-10q"}, {"layout", "4bit-year-in-r1"}});
    REQUIRE(found.size() == 1);
    CHECK(found[0].parameter == "offset");
    CHECK(found[0].severity == Severity::Error);
}

TEST_CASE("an offset that would start outside the layout's years today is a warning",
          "[acorn-rtc][config]") {
    CHECK(check({{"offset", "-30y"}, {"layout", "4bit-year-in-r1"}}).empty());
    const auto found = check({{"offset", "+5y"}, {"layout", "7bit-year-in-r1r5"}});
    CHECK(found.empty());
    const auto late = check({{"offset", "-10y"}, {"layout", "4bit-year-in-r1"}});
    REQUIRE(late.size() == 1);
    CHECK(late[0].parameter == "offset");
    CHECK(late[0].severity == Severity::Warning);
    INFO(late[0].message);
    CHECK(late[0].message.find("2016") != std::string::npos);
    CHECK(late[0].message.find("1981-1996") != std::string::npos);
}

TEST_CASE("host time outside the layout's years today is a warning on the instance",
          "[acorn-rtc][config]") {
    CHECK(check({{"layout", "7bit-year-in-r7"}}).empty());
    const auto found = check({{"layout", "4bit-year-in-r1"}});
    REQUIRE(found.size() == 1);
    CHECK(found[0].parameter.empty());
    CHECK(found[0].severity == Severity::Warning);
    INFO(found[0].message);
    CHECK(found[0].message.find("2026") != std::string::npos);
    CHECK(found[0].message.find("1981-1996") != std::string::npos);
}
