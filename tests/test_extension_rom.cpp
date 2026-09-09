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

// Tests for the extension-API ROM resolution and loading: Extension::rom_filepath,
// Extension::load_rom, and the free read_rom_image / validate_rom_image helpers.
// A coprocessor plugin ships firmware in its own roms/ directory, declared in
// its manifest; these resolve and load it against the manifest directory,
// accepting a padded double-size EPROM dump with a blank lower half.

#include <catch2/catch_test_macros.hpp>

#include <beebium/extension/Extension.hpp>
#include <beebium/extension/ExtensionManifest.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <vector>

using namespace beebium;

namespace {

struct TestExtension : Extension {};

// A unique temp directory for one test, cleaned by the caller. The random
// suffix keeps parallel ctest processes from colliding without needing a
// platform-specific pid.
std::filesystem::path make_temp_dir() {
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path()
             / ("beebium-rom-test-" + std::to_string(rd()) + "-" + std::to_string(rd()));
    std::filesystem::create_directories(dir / "roms");
    return dir;
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

// An Extension whose manifest points at `dir` and declares one 2048-byte ROM
// "client" with filename "client.rom".
TestExtension make_extension(const std::filesystem::path& dir) {
    ExtensionManifest m;
    m.name = "test-cop";
    m.manifest_dirpath = dir;
    m.roms.push_back(RomImage{"client", "client.rom", 2048, "test client ROM"});
    TestExtension ext;
    ext.set_manifest(m);
    return ext;
}

}  // namespace

TEST_CASE("Extension::rom_filepath resolves beside the manifest, under roms/",
          "[extension][rom]") {
    auto dir = make_temp_dir();
    auto ext = make_extension(dir);
    CHECK(ext.rom_filepath("client") == dir / "roms" / "client.rom");
    CHECK_THROWS(ext.rom_filepath("nonexistent-key"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("Extension::load_rom loads an exact-size image", "[extension][rom]") {
    auto dir = make_temp_dir();
    std::vector<std::uint8_t> image(2048);
    for (size_t i = 0; i < image.size(); ++i) image[i] = static_cast<std::uint8_t>(i & 0xFF);
    write_file(dir / "roms" / "client.rom", image);

    auto ext = make_extension(dir);
    std::array<std::uint8_t, 2048> dest{};
    ext.load_rom("client", std::span<std::uint8_t>(dest));
    CHECK(std::equal(dest.begin(), dest.end(), image.begin()));
    std::filesystem::remove_all(dir);
}

TEST_CASE("Extension::load_rom throws naming the path when the file is missing",
          "[extension][rom]") {
    auto dir = make_temp_dir();  // roms/ exists but client.rom does not
    auto ext = make_extension(dir);
    std::array<std::uint8_t, 2048> dest{};
    try {
        ext.load_rom("client", std::span<std::uint8_t>(dest));
        FAIL("expected load_rom to throw");
    } catch (const std::exception& e) {
        std::string msg = e.what();
        CHECK(msg.find("client.rom") != std::string::npos);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("Extension::load_rom rejects a wrong-size image", "[extension][rom]") {
    auto dir = make_temp_dir();
    write_file(dir / "roms" / "client.rom", std::vector<std::uint8_t>(1000, 0x00));
    auto ext = make_extension(dir);
    std::array<std::uint8_t, 2048> dest{};
    CHECK_THROWS(ext.load_rom("client", std::span<std::uint8_t>(dest)));
    std::filesystem::remove_all(dir);
}

TEST_CASE("read_rom_image requires exactly the expected size", "[extension][rom]") {
    auto dir = make_temp_dir();

    // A ROM image is the device's contents: exactly the declared size, whatever
    // it holds. There is no padded-dump or half-size acceptance -- a file of any
    // other size is a different image, and synthesising the rest would be a
    // guess. A double-size file is rejected just like any other wrong size.
    std::vector<std::uint8_t> exact(2048);
    for (size_t i = 0; i < exact.size(); ++i)
        exact[i] = static_cast<std::uint8_t>((i * 7 + 3) & 0xFF);
    write_file(dir / "roms" / "exact.rom", exact);

    std::vector<std::uint8_t> doubled(4096, 0xFF);
    std::copy(exact.begin(), exact.end(), doubled.begin() + 2048);
    write_file(dir / "roms" / "doubled.rom", doubled);

    std::array<std::uint8_t, 2048> dest{};
    read_rom_image(dir / "roms" / "exact.rom", std::span<std::uint8_t>(dest));
    CHECK(std::equal(dest.begin(), dest.end(), exact.begin()));

    // The 4 KB file (even with a blank lower half) no longer loads into a 2 KB
    // buffer; it names the mismatch.
    try {
        std::array<std::uint8_t, 2048> dest2{};
        read_rom_image(dir / "roms" / "doubled.rom", std::span<std::uint8_t>(dest2));
        FAIL("expected read_rom_image to reject the 4 KB file");
    } catch (const std::exception& e) {
        std::string msg = e.what();
        CHECK(msg.find("4096") != std::string::npos);
        CHECK(msg.find("exactly 2048") != std::string::npos);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("validate_rom_image accepts valid forms and rejects the rest",
          "[extension][rom]") {
    auto dir = make_temp_dir();
    write_file(dir / "roms" / "exact.rom", std::vector<std::uint8_t>(2048, 0x11));
    std::vector<std::uint8_t> doubled(4096, 0xFF);
    std::fill(doubled.begin() + 2048, doubled.end(), 0x22);
    write_file(dir / "roms" / "doubled.rom", doubled);
    write_file(dir / "roms" / "wrong.rom", std::vector<std::uint8_t>(3000, 0x00));

    // Only the exact declared size is valid. A double-size file (even one with a
    // blank lower half) is rejected like any other wrong size -- the image is
    // the device's contents, not a padded fragment.
    CHECK_NOTHROW(validate_rom_image(dir / "roms" / "exact.rom", 2048));
    CHECK_THROWS(validate_rom_image(dir / "roms" / "doubled.rom", 2048));
    CHECK_THROWS(validate_rom_image(dir / "roms" / "wrong.rom", 2048));
    CHECK_THROWS(validate_rom_image(dir / "roms" / "absent.rom", 2048));
    std::filesystem::remove_all(dir);
}
