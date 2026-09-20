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

// Unit tests for the inline CLI argument parsers in ServerMain.hpp.
// Specifically: tolerance for case in enum-style values, and quality of
// error messages when an unrecognised value is supplied.

#include <catch2/catch_test_macros.hpp>

#include <beebium/server/CliArgParsers.hpp>

#include <stdexcept>

using beebium::server::parse_sideways_arg;
using beebium::server::parse_wait_arg;
using beebium::server::parse_format_arg;
using beebium::server::parse_speed_arg;
using beebium::server::SidewaysSlotType;
using beebium::server::WaitMode;
using beebium::server::OutputFormat;

// ---- parse_sideways_arg ----

TEST_CASE("parse_sideways_arg accepts lowercase rom/ram/empty", "[cli][sideways]") {
    REQUIRE(parse_sideways_arg("slot=4:type=rom:image=foo.rom").type == SidewaysSlotType::Rom);
    REQUIRE(parse_sideways_arg("slot=4:type=ram").type == SidewaysSlotType::Ram);
    REQUIRE(parse_sideways_arg("slot=4:type=empty").type == SidewaysSlotType::Empty);
}

TEST_CASE("parse_sideways_arg accepts uppercase ROM/RAM/EMPTY", "[cli][sideways]") {
    REQUIRE(parse_sideways_arg("slot=4:type=ROM:image=foo.rom").type == SidewaysSlotType::Rom);
    REQUIRE(parse_sideways_arg("slot=4:type=RAM").type == SidewaysSlotType::Ram);
    REQUIRE(parse_sideways_arg("slot=4:type=EMPTY").type == SidewaysSlotType::Empty);
}

TEST_CASE("parse_sideways_arg accepts mixed-case values and keys", "[cli][sideways]") {
    REQUIRE(parse_sideways_arg("slot=4:type=Rom:image=foo.rom").type == SidewaysSlotType::Rom);
    REQUIRE(parse_sideways_arg("SLOT=4:TYPE=Ram").type == SidewaysSlotType::Ram);
    REQUIRE(parse_sideways_arg("slot=4:type=Empty").type == SidewaysSlotType::Empty);
}

TEST_CASE("parse_sideways_arg parses image and write-protect", "[cli][sideways]") {
    auto rom = parse_sideways_arg("slot=13:type=rom:image=dfs.rom");
    REQUIRE(rom.slot == 13);
    REQUIRE(rom.image_filepath == "dfs.rom");
    REQUIRE_FALSE(rom.write_protected);

    auto ram = parse_sideways_arg("slot=15:type=ram:write-protect");
    REQUIRE(ram.type == SidewaysSlotType::Ram);
    REQUIRE(ram.write_protected);

    // A value containing ':' can be quoted.
    REQUIRE(parse_sideways_arg("slot=15:type=rom:image=\"a:b.rom\"").image_filepath == "a:b.rom");
}

TEST_CASE("parse_sideways_arg keeps colons in an unquoted image path", "[cli][sideways]") {
    // A Windows filepath carries a drive-letter colon; the colon-separated
    // grammar must not tear image=D:\... into separate fields, so clients need
    // not quote the path.
    auto win = parse_sideways_arg("slot=14:type=rom:image=D:\\roms\\acorn-dfs_2_26.rom");
    REQUIRE(win.slot == 14);
    REQUIRE(win.type == SidewaysSlotType::Rom);
    REQUIRE(win.image_filepath == "D:\\roms\\acorn-dfs_2_26.rom");

    // A trailing flag after such a path is still recognised as its own field.
    auto win_wp = parse_sideways_arg("slot=15:type=ram:image=D:\\ram.bin:write-protect");
    REQUIRE(win_wp.image_filepath == "D:\\ram.bin");
    REQUIRE(win_wp.write_protected);

    // The same holds for any multi-colon value, e.g. a URL.
    auto url = parse_sideways_arg("slot=13:type=rom:image=ip232://host:1234/rom");
    REQUIRE(url.image_filepath == "ip232://host:1234/rom");
}

TEST_CASE("parse_sideways_arg rejects the retired positional form", "[cli][sideways]") {
    // The old SLOT:TYPE[:IMAGE] form must be rejected with guidance.
    try {
        parse_sideways_arg("4:ram");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("key=value") != std::string::npos);
    }
}

TEST_CASE("parse_sideways_arg rejects write-protect on a non-RAM slot", "[cli][sideways]") {
    try {
        parse_sideways_arg("slot=4:type=rom:image=foo.rom:write-protect");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        REQUIRE(std::string(e.what()).find("write-protect") != std::string::npos);
    }
}

TEST_CASE("parse_sideways_arg error message lists allowed values", "[cli][sideways]") {
    try {
        parse_sideways_arg("slot=4:type=WAT");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        // The bad value, the flag name, and at least the three allowed values
        // must all appear in the message so the user can see what to type.
        REQUIRE(msg.find("WAT") != std::string::npos);
        REQUIRE(msg.find("--sideways") != std::string::npos);
        REQUIRE(msg.find("rom") != std::string::npos);
        REQUIRE(msg.find("ram") != std::string::npos);
        REQUIRE(msg.find("empty") != std::string::npos);
    }
}

TEST_CASE("parse_sideways_arg rejects bad slot number with clear message", "[cli][sideways]") {
    try {
        parse_sideways_arg("slot=99:type=rom:image=foo.rom");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("99") != std::string::npos);
        REQUIRE(msg.find("0-15") != std::string::npos);
    }
}

// ---- parse_wait_arg ----

TEST_CASE("parse_wait_arg accepts lowercase cli/api", "[cli][wait]") {
    REQUIRE(parse_wait_arg("cli") == WaitMode::Cli);
    REQUIRE(parse_wait_arg("api") == WaitMode::Api);
}

TEST_CASE("parse_wait_arg accepts uppercase CLI/API", "[cli][wait]") {
    REQUIRE(parse_wait_arg("CLI") == WaitMode::Cli);
    REQUIRE(parse_wait_arg("API") == WaitMode::Api);
}

TEST_CASE("parse_wait_arg error message lists allowed values", "[cli][wait]") {
    try {
        parse_wait_arg("WAT");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("WAT") != std::string::npos);
        REQUIRE(msg.find("cli") != std::string::npos);
        REQUIRE(msg.find("api") != std::string::npos);
    }
}

// ---- parse_format_arg ----

TEST_CASE("parse_format_arg accepts mixed-case values", "[cli][format]") {
    REQUIRE(parse_format_arg("Pretty") == OutputFormat::Pretty);
    REQUIRE(parse_format_arg("TSV") == OutputFormat::Tsv);
    REQUIRE(parse_format_arg("JSONL") == OutputFormat::Jsonl);
}

TEST_CASE("parse_format_arg error message lists allowed values", "[cli][format]") {
    try {
        parse_format_arg("xml");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("xml") != std::string::npos);
        REQUIRE(msg.find("pretty") != std::string::npos);
        REQUIRE(msg.find("tsv") != std::string::npos);
        REQUIRE(msg.find("jsonl") != std::string::npos);
    }
}

// ---- parse_speed_arg ----

TEST_CASE("parse_speed_arg accepts positive multipliers", "[cli][speed]") {
    REQUIRE(parse_speed_arg("1") == 1.0);
    REQUIRE(parse_speed_arg("1.0") == 1.0);
    REQUIRE(parse_speed_arg("0.5") == 0.5);
    REQUIRE(parse_speed_arg("2.0") == 2.0);
}

TEST_CASE("parse_speed_arg maps the word 'unlimited' to the 0.0 sentinel", "[cli][speed]") {
    REQUIRE(parse_speed_arg("unlimited") == 0.0);
    REQUIRE(parse_speed_arg("UNLIMITED") == 0.0);
    REQUIRE(parse_speed_arg("Unlimited") == 0.0);
}

TEST_CASE("parse_speed_arg rejects zero and points at 'unlimited'", "[cli][speed]") {
    try {
        parse_speed_arg("0");
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("0") != std::string::npos);
        REQUIRE(msg.find("unlimited") != std::string::npos);
    }
}

TEST_CASE("parse_speed_arg rejects negative multipliers", "[cli][speed]") {
    REQUIRE_THROWS_AS(parse_speed_arg("-1"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_speed_arg("-0.5"), std::runtime_error);
}

TEST_CASE("parse_speed_arg rejects non-numeric and trailing junk", "[cli][speed]") {
    REQUIRE_THROWS_AS(parse_speed_arg("fast"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_speed_arg("2x"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_speed_arg(""), std::runtime_error);
}

TEST_CASE("parse_speed_arg rejects non-finite values", "[cli][speed]") {
    REQUIRE_THROWS_AS(parse_speed_arg("inf"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_speed_arg("nan"), std::runtime_error);
}
