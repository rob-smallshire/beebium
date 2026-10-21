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

#include <catch2/catch_test_macros.hpp>

#include <beebium/Sha1.hpp>
#include <beebium/server/DfsRomManifest.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace beebium;

namespace {

std::vector<uint8_t> load_file(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<uint8_t> bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

}  // namespace

TEST_CASE("SHA-1 of the standard test vectors", "[sha1]") {
    CHECK(sha1_hex(bytes("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK(sha1_hex(bytes("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(sha1_hex(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    std::vector<uint8_t> million(1'000'000, 'a');
    CHECK(sha1_hex(million) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST_CASE("The DFS manifest knows each bundled DFS by its contents", "[rom-manifest]") {
    struct Case {
        const char* filename;
        const char* name;
        const char* fdc_chip;
    };
    const std::vector<Case> cases = {
        {"acorn-dfs_0_90.rom", "Acorn DFS 0.90", "8271"},
        {"acorn-dfs_0_98.rom", "Acorn DFS 0.98", "8271"},
        {"acorn-dnfs_1_00.rom", "Acorn DFS 1.00 (NFS 3.40)", "8271"},
        {"acorn-dnfs_1_20.rom", "Acorn DFS 1.20 (NFS 3.60)", "8271"},
        {"acorn-dnfs_3_02.rom", "Acorn DFS 1.22 (NFS 3.62)", "8271"},
        {"acorn-dfs_2_26.rom", "Acorn DFS 2.26", "WD1770"},
    };
    for (const auto& c : cases) {
        INFO(c.filename);
        auto data = load_file(std::filesystem::path(BEEBIUM_ROM_DIR) / c.filename);
        REQUIRE_FALSE(data.empty());
        const DfsRom* rom = find_dfs_rom(data);
        REQUIRE(rom != nullptr);
        CHECK(rom->name == c.name);
        CHECK(rom->fdc_chip == c.fdc_chip);
    }
}

TEST_CASE("The DFS manifest does not judge other ROMs", "[rom-manifest]") {
    auto basic = load_file(std::filesystem::path(BEEBIUM_ROM_DIR) / "bbc-basic_2.rom");
    REQUIRE_FALSE(basic.empty());
    CHECK(find_dfs_rom(basic) == nullptr);
}

TEST_CASE("A DFS that fits the controller has no complaint", "[rom-manifest]") {
    auto dfs = load_file(std::filesystem::path(BEEBIUM_ROM_DIR) / "acorn-dnfs_1_20.rom");
    const DfsRom* rom = find_dfs_rom(dfs);
    REQUIRE(rom != nullptr);
    CHECK_FALSE(dfs_controller_mismatch(*rom, "8271").has_value());
    auto complaint = dfs_controller_mismatch(*rom, "WD1770");
    REQUIRE(complaint.has_value());
    CHECK(*complaint == "Acorn DFS 1.20 (NFS 3.60) needs the 8271 disc controller; this machine has the WD1770");
    auto none = dfs_controller_mismatch(*rom, "");
    REQUIRE(none.has_value());
    CHECK(*none == "Acorn DFS 1.20 (NFS 3.60) needs the 8271 disc controller; this machine has none");
}
