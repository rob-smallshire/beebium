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

#include <beebium/server/OsRomManifest.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> read_rom(const std::string& filename) {
    std::ifstream in(std::filesystem::path(BEEBIUM_ROM_DIR) / filename, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("OS ROM manifest names each bundled MOS by its image", "[os-rom][manifest]") {
    const std::vector<std::pair<std::string, std::string>> bundled = {
        {"acorn-mos_1_20.rom", "Acorn MOS 1.20"},
        {"acorn-mos_2_0.rom", "Acorn MOS 2.0"},
        {"acorn-mos_3_20.rom", "Acorn MOS 3.20"},
        {"acorn-mos_3_50.rom", "Acorn MOS 3.50"},
    };
    for (const auto& [filename, title] : bundled) {
        INFO(filename);
        const auto image = read_rom(filename);
        REQUIRE(image.size() == 16384);
        const auto* known = beebium::find_os_rom(image);
        REQUIRE(known != nullptr);
        CHECK(known->title == title);
    }
}

TEST_CASE("OS ROM found by name reports its bare name and manifest title",
          "[os-rom][manifest]") {
    const auto filepath = std::filesystem::path(BEEBIUM_ROM_DIR) / "acorn-mos_3_20.rom";
    const auto image = read_rom("acorn-mos_3_20.rom");

    const auto os_rom = beebium::describe_os_rom("acorn-mos_3_20.rom", filepath, image);

    CHECK(os_rom.rom_name == "acorn-mos_3_20");
    CHECK(os_rom.title == "Acorn MOS 3.20");
    CHECK(os_rom.size_bytes == 16384);
    CHECK(std::filesystem::path(os_rom.image_uri).is_absolute());
    CHECK(std::filesystem::path(os_rom.image_uri).filename() == "acorn-mos_3_20.rom");
}

TEST_CASE("OS ROM named by a path has no bare name", "[os-rom][manifest]") {
    const auto filepath = std::filesystem::path(BEEBIUM_ROM_DIR) / "acorn-mos_1_20.rom";
    const auto image = read_rom("acorn-mos_1_20.rom");

    const auto os_rom = beebium::describe_os_rom(filepath.string(), filepath, image);

    CHECK(os_rom.rom_name.empty());
    CHECK(os_rom.title == "Acorn MOS 1.20");
}

TEST_CASE("OS ROM the manifest does not know is named by its file", "[os-rom][manifest]") {
    const std::vector<std::uint8_t> image(16384, 0xEA);
    const std::filesystem::path filepath = "/roms/my-own.os.bin";

    const auto os_rom = beebium::describe_os_rom(filepath.string(), filepath, image);

    CHECK(os_rom.rom_name.empty());
    CHECK(os_rom.title == "my-own.os");
    CHECK(os_rom.size_bytes == 16384);
}

TEST_CASE("OS ROM bare name loses only a .rom extension", "[os-rom][manifest]") {
    const std::vector<std::uint8_t> image(16384, 0x00);
    const std::filesystem::path filepath = "/roms/mos.3.20";

    CHECK(beebium::describe_os_rom("mos.3.20", filepath, image).rom_name == "mos.3.20");
}
