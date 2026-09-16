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

// Error-branch coverage for DiscFormatRegistry::load_from_filepath and the
// DiscUrl decode path used to reach it. Every failure mode the front end
// surfaces to the user is pinned here to its exact message class (and, for the
// unrecognised case, to the reported size= and ext= fields), so the strings the
// UI relies on cannot silently change. The spaced-path case is the unit-level
// guard for the Stardot bug where a file:// URL's %20 was not decoded.
//
// Source literals are 7-bit ASCII (Windows CTest/Catch2 requires it).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "beebium/disc/DiscLoader.hpp"

using namespace beebium;

namespace {

// A unique temporary directory whose own name contains a space, so any path
// built inside it exercises URL-reserved-character handling. Removed on scope
// exit. Not a committed filename with a space (fixtures live in a tmp path).
class SpacedTempDir {
public:
    SpacedTempDir() {
        auto base = std::filesystem::temp_directory_path();
        for (int i = 0; i < 10000; ++i) {
            auto candidate = base / ("beebium disc test " + std::to_string(
                std::hash<std::string>{}(std::to_string(i) + std::to_string(
                    reinterpret_cast<std::uintptr_t>(this)))));
            std::error_code ec;
            if (std::filesystem::create_directory(candidate, ec)) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("could not create a temp dir");
    }
    ~SpacedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    SpacedTempDir(const SpacedTempDir&) = delete;
    SpacedTempDir& operator=(const SpacedTempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write_bytes(const std::filesystem::path& p, const std::vector<uint8_t>& bytes) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::filesystem::path genuine_ssd() {
    // A real 200K 80-track SSD shipped with the tests.
    return std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) /
           "discs" / "Disc001-CylonAttackAFSTD.ssd";
}

} // namespace

TEST_CASE("load_from_filepath: missing path reports Cannot open", "[disc][registry][errors]") {
    SpacedTempDir dir;
    auto missing = dir.path() / "no such disc.ssd";  // never created
    auto result = default_format_registry().load_from_filepath(missing);
    REQUIRE_FALSE(result.success());
    CHECK(contains(result.error, "Cannot open disc image"));
    CHECK(result.kind == DiscLoadErrorKind::CannotOpen);
}

TEST_CASE("load_from_filepath: empty file reports Empty disc image", "[disc][registry][errors]") {
    SpacedTempDir dir;
    auto empty = dir.path() / "empty.ssd";
    write_bytes(empty, {});  // zero bytes
    auto result = default_format_registry().load_from_filepath(empty);
    REQUIRE_FALSE(result.success());
    CHECK(contains(result.error, "Empty disc image"));
    CHECK(result.kind == DiscLoadErrorKind::Empty);
}

TEST_CASE("load_from_filepath: non-disc bytes report Unrecognised with size and ext",
          "[disc][registry][errors]") {
    SpacedTempDir dir;
    auto bogus = dir.path() / "not a disc.ssd";
    // 3000 bytes: non-empty but NOT a multiple of the 256-byte sector size, so
    // no SSD/DSD/ADFS/HFE handler detects it -- the unrecognised branch.
    std::vector<uint8_t> bytes(3000, 0xEE);
    bytes[0] = 0x89;  // a non-disc leading byte, PNG-ish
    write_bytes(bogus, bytes);
    auto result = default_format_registry().load_from_filepath(bogus);
    REQUIRE_FALSE(result.success());
    CHECK(contains(result.error, "Unrecognised disc image format"));
    CHECK(contains(result.error, "size=3000"));
    CHECK(contains(result.error, "ext=.ssd"));
    CHECK(result.kind == DiscLoadErrorKind::Unrecognised);
}

TEST_CASE("load_from_filepath: a genuine SSD loads (positive control)",
          "[disc][registry][errors]") {
    auto result = default_format_registry().load_from_filepath(genuine_ssd());
    REQUIRE(result.success());
    CHECK(result.error.empty());
    CHECK(result.kind == DiscLoadErrorKind::None);
}

TEST_CASE("load_disc_from_url: a genuine SSD at a spaced path loads via DiscUrl",
          "[disc][registry][errors][url]") {
    // Copy the real SSD to a path whose directory name contains a space, then
    // load it the way the front end does: as a file:// URL. from_filepath
    // percent-encodes the space to %20; parse must decode it back, or the load
    // fails "Cannot open". This is the unit-level guard for the Stardot bug.
    SpacedTempDir dir;
    auto spaced = dir.path() / "My Disc.ssd";
    std::filesystem::copy_file(genuine_ssd(), spaced);

    std::string url = DiscUrl::from_filepath(spaced).url();
    CHECK(contains(url, "%20"));  // the space really is encoded in the URL

    auto result = load_disc_from_url(url);
    REQUIRE(result.success());
    CHECK(result.error.empty());
    CHECK(result.kind == DiscLoadErrorKind::None);
}
