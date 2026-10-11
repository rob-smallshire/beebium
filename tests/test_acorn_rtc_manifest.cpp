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

// The Acorn real-time clock's manifest against the code: what the manifest
// tells a user about the register layouts must be what the chip enforces.

#include <catch2/catch_test_macros.hpp>

#include "AcornRtcConfig.hpp"
#include "Saf3019p.hpp"

#include <beebium/extension/PluginLoader.hpp>

#include <regex>
#include <set>
#include <string>

using namespace beebium;

namespace {

ExtensionManifest acorn_rtc_manifest() {
    PluginLoader loader;
    auto manifests = loader.scan_manifests(BEEBIUM_ACORN_RTC_MANIFEST_DIRPATH,
                                           PluginLoader::MissingDirPolicy::Throw);
    REQUIRE(manifests.size() == 1);
    return manifests[0];
}

const ParameterSchema& layout_parameter(const ExtensionManifest& m) {
    for (const auto& p : m.parameters) {
        if (p.key == "layout") return p;
    }
    FAIL("the manifest has no layout parameter");
    throw;
}

}  // namespace

TEST_CASE("a register layout is named as the manifest's layout choices name it",
          "[acorn-rtc][manifest]") {
    CHECK(register_layout_from_name("4bit-year-in-r1") == RegisterLayout::FourBitYearInR1);
    CHECK(register_layout_from_name("7bit-year-in-r7") == RegisterLayout::SevenBitYearInR7);
    CHECK(register_layout_from_name("7bit-year-in-r1r5") ==
          RegisterLayout::SevenBitYearInR1R5);
    CHECK_FALSE(register_layout_from_name("8bit-year"));
}

TEST_CASE("each layout choice states the years the chip holds in that layout",
          "[acorn-rtc][manifest]") {
    const auto manifest = acorn_rtc_manifest();
    const auto& layout = layout_parameter(manifest);
    REQUIRE(layout.choices.size() == 3);

    const std::regex years(R"(Years (\d{4}) to (\d{4})\b)");
    std::set<RegisterLayout> described;
    for (const auto& choice : layout.choices) {
        INFO(choice.value << ": " << choice.description);
        const auto parsed = register_layout_from_name(choice.value);
        REQUIRE(parsed);
        described.insert(*parsed);

        std::smatch match;
        REQUIRE(std::regex_search(choice.description, match, years));
        const auto [first, last] = Saf3019p::year_range(*parsed);
        CHECK(std::stoi(match[1]) == first);
        CHECK(std::stoi(match[2]) == last);
    }
    CHECK(described.size() == 3);
}

TEST_CASE("the manifest's default layout is Level 3 File Server 1.26's",
          "[acorn-rtc][manifest]") {
    // It holds the years to 2099, so a clock started at host time works.
    const auto manifest = acorn_rtc_manifest();
    CHECK(layout_parameter(manifest).default_value == "7bit-year-in-r7");
}

TEST_CASE("the layout that ends before 2000 says it is not Y2K compliant",
          "[acorn-rtc][manifest]") {
    const auto manifest = acorn_rtc_manifest();
    for (const auto& choice : layout_parameter(manifest).choices) {
        INFO(choice.value);
        const auto [first, last] = Saf3019p::year_range(*register_layout_from_name(choice.value));
        const bool says_not_y2k = choice.description.find("Not Y2K compliant") != std::string::npos;
        CHECK(says_not_y2k == (last < 2000));
    }
}
