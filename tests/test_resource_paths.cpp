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

#include <filesystem>

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

#ifdef _WIN32
TEST_CASE("anchor_reference: Windows drive paths are absolute, backslashes separate",
          "[resource_paths][anchor_reference][windows]") {
    CHECK(is_absolute_reference("C:\\Beeb\\x.rom"));
    CHECK(is_absolute_reference("C:/Beeb/x.rom"));
    CHECK(anchor_reference("C:\\Beeb\\x.rom", "D:\\presets") == "C:\\Beeb\\x.rom");
    CHECK(anchor_reference("discs\\x.dat", "D:\\presets") == normal("D:\\presets\\discs\\x.dat"));
}
#endif
