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

// URL <-> filesystem-path conversion for disc images. The macOS front end sends
// disc paths as file:// URLs (url.absoluteString) with reserved characters
// percent-encoded, so a path containing a space arrives as "...%20...". parse
// must decode those back to real bytes, and from_filepath must encode them, so
// that a path with spaces (or #, ?, %, non-ASCII bytes) round-trips.
//
// Source literals are 7-bit ASCII (Windows CTest/Catch2 requires it); UTF-8
// multi-byte content is written with explicit \xNN byte escapes.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "beebium/disc/DiscUrl.hpp"

using beebium::DiscUrl;

TEST_CASE("DiscUrl::parse leaves a space-free path unchanged", "[disc][url]") {
    auto url = DiscUrl::parse("file:///tmp/beebium/disc.ssd");
    REQUIRE(url.has_value());
    CHECK(url->to_filepath() == std::filesystem::path("/tmp/beebium/disc.ssd"));
}

TEST_CASE("DiscUrl::parse percent-decodes a space", "[disc][url]") {
    auto url = DiscUrl::parse("file:///tmp/My%20Disc.ssd");
    REQUIRE(url.has_value());
    CHECK(url->to_filepath() == std::filesystem::path("/tmp/My Disc.ssd"));
}

TEST_CASE("DiscUrl::parse percent-decodes reserved characters", "[disc][url]") {
    // %25 -> '%', %23 -> '#', %3F -> '?'
    auto url = DiscUrl::parse("file:///tmp/50%25%20off%20%23%3F.ssd");
    REQUIRE(url.has_value());
    CHECK(url->to_filepath() == std::filesystem::path("/tmp/50% off #?.ssd"));
}

TEST_CASE("DiscUrl::parse accepts lower-case hex digits", "[disc][url]") {
    auto url = DiscUrl::parse("file:///tmp/a%2fb.ssd");  // %2f -> '/'
    REQUIRE(url.has_value());
    // A decoded '/' becomes a path separator, which is the byte that was encoded.
    CHECK(url->to_filepath() == std::filesystem::path("/tmp/a/b.ssd"));
}

TEST_CASE("DiscUrl::parse percent-decodes UTF-8 multi-byte sequences", "[disc][url]") {
    // "caf" + U+00E9 (e-acute, UTF-8 0xC3 0xA9) + ".ssd"
    auto url = DiscUrl::parse("file:///tmp/caf%C3%A9.ssd");
    REQUIRE(url.has_value());
    CHECK(url->to_filepath() == std::filesystem::path("/tmp/caf\xC3\xA9.ssd"));
}

TEST_CASE("DiscUrl::parse does not turn '+' into a space", "[disc][url]") {
    // '+' means space only in application/x-www-form-urlencoded query strings,
    // never in a path component. A path '+' is a literal plus and must survive.
    auto url = DiscUrl::parse("file:///tmp/Elite+.ssd");
    REQUIRE(url.has_value());
    CHECK(url->to_filepath() == std::filesystem::path("/tmp/Elite+.ssd"));
}

TEST_CASE("DiscUrl::parse leaves a malformed percent-escape literal", "[disc][url]") {
    // Documented behaviour: a '%' not followed by two hex digits is emitted as a
    // literal '%' and parsing continues -- it never throws or returns nullopt.
    SECTION("trailing percent with no hex digits") {
        auto url = DiscUrl::parse("file:///tmp/100%.ssd");
        REQUIRE(url.has_value());
        CHECK(url->to_filepath() == std::filesystem::path("/tmp/100%.ssd"));
    }
    SECTION("percent with a single trailing hex digit") {
        auto url = DiscUrl::parse("file:///tmp/x%2");
        REQUIRE(url.has_value());
        CHECK(url->to_filepath() == std::filesystem::path("/tmp/x%2"));
    }
    SECTION("percent followed by non-hex characters") {
        auto url = DiscUrl::parse("file:///tmp/z%GG.ssd");
        REQUIRE(url.has_value());
        CHECK(url->to_filepath() == std::filesystem::path("/tmp/z%GG.ssd"));
    }
}

TEST_CASE("DiscUrl::from_filepath percent-encodes reserved characters", "[disc][url]") {
    auto cwd = std::filesystem::current_path();
    auto url = DiscUrl::from_filepath(cwd / "My Disc #1.ssd");
    const std::string& s = url.url();
    // The space and the '#' must be encoded; no raw space or '#' in the URL.
    CHECK(s.find("%20") != std::string::npos);
    CHECK(s.find("%23") != std::string::npos);
    CHECK(s.find(' ') == std::string::npos);
    CHECK(s.find('#') == std::string::npos);
}

TEST_CASE("DiscUrl::from_filepath encodes a literal percent as %25", "[disc][url]") {
    auto cwd = std::filesystem::current_path();
    auto url = DiscUrl::from_filepath(cwd / "50% off.ssd");
    CHECK(url.url().find("%25") != std::string::npos);
}

TEST_CASE("DiscUrl round-trips parse(from_filepath(p)) == p", "[disc][url]") {
    auto cwd = std::filesystem::current_path();
    const char* names[] = {
        "disc.ssd",
        "My Disc.ssd",
        "My Disc #1 (why?).ssd",
        "100% full.ssd",
        "caf\xC3\xA9.ssd",
    };
    for (const char* name : names) {
        std::filesystem::path p = cwd / name;
        auto url = DiscUrl::parse(DiscUrl::from_filepath(p).url());
        REQUIRE(url.has_value());
        CHECK(url->to_filepath() == p);
    }
}

#ifdef _WIN32
TEST_CASE("DiscUrl::parse handles a percent-encoded Windows drive path", "[disc][url]") {
    auto url = DiscUrl::parse("file:///C:/Users/rjs/My%20Disc.ssd");
    REQUIRE(url.has_value());
    CHECK(url->to_filepath() == std::filesystem::path("C:/Users/rjs/My Disc.ssd"));
}
#endif
