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

// test_resource_paths.cpp
// Tests for resolving file references (ResourcePaths.hpp)

#include <beebium/server/ResourcePaths.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "test_temp_paths.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace beebium::server;

namespace {
std::string normal(const char* path) {
    return std::filesystem::path(path).lexically_normal().string();
}
}  // namespace

TEST_CASE("anchor_reference: a path with a directory resolves from the container",
          "[resource_paths][anchor_reference]") {
    CHECK(anchor_reference("discs/l3fs.dat", "/home/u/presets") ==
          normal("/home/u/presets/discs/l3fs.dat"));
    CHECK(anchor_reference("../discs/l3fs.dat", "/home/u/presets") ==
          normal("/home/u/discs/l3fs.dat"));
    CHECK(anchor_reference("./x.rom", "/home/u/presets") == normal("/home/u/presets/x.rom"));
}

TEST_CASE("anchor_reference: a bare name is left for search",
          "[resource_paths][anchor_reference]") {
    CHECK(anchor_reference("l3fs-v1_26b.dat", "/home/u/presets") == "l3fs-v1_26b.dat");
    CHECK(anchor_reference("none", "/home/u/presets") == "none");
}

TEST_CASE("anchor_reference: absolute paths, URIs and the empty string are unchanged",
          "[resource_paths][anchor_reference]") {
    CHECK(anchor_reference("/dev/ttyUSB0", "/home/u/presets") == "/dev/ttyUSB0");
    CHECK(anchor_reference("file:///x/y.dat", "/home/u/presets") == "file:///x/y.dat");
    CHECK(anchor_reference("", "/home/u/presets").empty());
}

TEST_CASE("is_absolute_reference: a leading slash is absolute on every platform",
          "[resource_paths][is_absolute_reference]") {
    CHECK(is_absolute_reference("/roms/x.rom"));
    CHECK_FALSE(is_absolute_reference("roms/x.rom"));
    CHECK_FALSE(is_absolute_reference("x.rom"));
    CHECK_FALSE(is_absolute_reference(""));
}

TEST_CASE("find_on_search_path: the first directory holding the name wins",
          "[resource_paths][find_on_search_path]") {
    beebium::test::ScopedTempDir temp_dir("beebium_resource_paths_");
    const auto first = temp_dir / "first";
    const auto second = temp_dir / "second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    std::ofstream(second / "x.rom") << "second";

    CHECK(find_on_search_path("x.rom", {first, second}, "ROM") == second / "x.rom");

    std::ofstream(first / "x.rom") << "first";
    CHECK(find_on_search_path("x.rom", {first, second}, "ROM") == first / "x.rom");
}

TEST_CASE("find_on_search_path: a directory is not a file of that name",
          "[resource_paths][find_on_search_path]") {
    beebium::test::ScopedTempDir temp_dir("beebium_resource_paths_");
    std::filesystem::create_directories(temp_dir / "a" / "x.rom");
    std::filesystem::create_directories(temp_dir / "b");
    std::ofstream(temp_dir / "b" / "x.rom") << "rom";

    CHECK(find_on_search_path("x.rom", {temp_dir / "a", temp_dir / "b"}, "ROM") ==
          temp_dir / "b" / "x.rom");
}

TEST_CASE("find_on_search_path: not found names every directory searched, in order",
          "[resource_paths][find_on_search_path]") {
    beebium::test::ScopedTempDir temp_dir("beebium_resource_paths_");
    const auto first = temp_dir / "first";
    const auto missing = temp_dir / "missing";
    std::filesystem::create_directories(first);

    const std::string expected = "ROM 'x.rom' not found; searched " + first.string() + ", " +
                                 missing.string();
    CHECK_THROWS_WITH(find_on_search_path("x.rom", {first, missing}, "ROM"), expected);
}

#ifdef _WIN32
TEST_CASE("anchor_reference: Windows drive paths are absolute, backslashes separate",
          "[resource_paths][anchor_reference][windows]") {
    CHECK(is_absolute_reference("C:\\Beeb\\x.rom"));
    CHECK(is_absolute_reference("C:/Beeb/x.rom"));
    CHECK(anchor_reference("C:\\Beeb\\x.rom", "D:\\presets") == "C:\\Beeb\\x.rom");
    CHECK(anchor_reference("discs\\x.dat", "D:\\presets") == normal("D:\\presets\\discs\\x.dat"));
}
#endif
