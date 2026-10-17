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
#include <beebium/zip/ZipArchive.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "test_env.hpp"
#include "test_temp_paths.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

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

// ---------------------------------------------------------------------------
// Rule 4: a leading "~/" is the home directory
// ---------------------------------------------------------------------------

TEST_CASE("anchor_reference: a leading ~/ or ~\\ is the home directory",
          "[resource_paths][anchor_reference][tilde]") {
    beebium::test::ScopedEnv home("HOME", "/home/u");
    const auto expected = normal("/home/u/discs/x.dat");
    CHECK(anchor_reference("~/discs/x.dat", "/elsewhere") == expected);
    CHECK(anchor_reference("~\\discs/x.dat", "/elsewhere") == expected);
    // Only a leading ~ followed by a separator: these name files.
    CHECK(anchor_reference("~x.dat", "/elsewhere") == "~x.dat");
    CHECK(anchor_reference("discs/~/x.dat", "/p") == normal("/p/discs/~/x.dat"));
}

TEST_CASE("anchor_reference: without HOME, ~ is USERPROFILE",
          "[resource_paths][anchor_reference][tilde]") {
    beebium::test::ScopedEnv home("HOME", "");
    beebium::test::ScopedEnv profile("USERPROFILE", "/users/u");
    CHECK(anchor_reference("~/x.rom", "/elsewhere") == normal("/users/u/x.rom"));
}

// ---------------------------------------------------------------------------
// Rule 1: a bare name is searched for -- container, per-user, system
// ---------------------------------------------------------------------------

namespace {

// A container, a per-user directory (pinned through BEEBIUM_USER_DIRPATH) and a
// system directory, each empty until a test puts a file in it.
struct SearchTree {
    beebium::test::ScopedTempDir root{"beebium_resource_search_"};
    std::filesystem::path container = root / "container";
    std::filesystem::path user = root / "user";
    std::filesystem::path system = root / "system";
    beebium::test::ScopedEnv user_env{"BEEBIUM_USER_DIRPATH", user.string()};

    SearchTree() {
        for (const auto& d : {container, user / "discs", user / "roms", system}) {
            std::filesystem::create_directories(d);
        }
    }
    static void put(const std::filesystem::path& filepath) {
        std::ofstream(filepath) << filepath.string();
    }
};

// What `body` writes to std::cerr.
template <typename F>
std::string cerr_of(F body) {
    std::ostringstream captured;
    auto* previous = std::cerr.rdbuf(captured.rdbuf());
    try {
        body();
    } catch (...) {
        std::cerr.rdbuf(previous);
        throw;
    }
    std::cerr.rdbuf(previous);
    return captured.str();
}

}  // namespace

TEST_CASE("resolve_reference: the container comes first, then per-user, then system",
          "[resource_paths][resolve_reference]") {
    SearchTree t;
    const auto kind = GENERATE(ResourceKind::Disc, ResourceKind::Rom);
    const auto user_dirpath = t.user / (kind == ResourceKind::Disc ? "discs" : "roms");

    SearchTree::put(t.system / "x");
    auto found = resolve_reference("x", kind, t.container, {t.system});
    CHECK(found.path == t.system / "x");
    CHECK(found.origin == ReferenceOrigin::System);
    SearchTree::put(user_dirpath / "x");
    found = resolve_reference("x", kind, t.container, {t.system});
    CHECK(found.path == user_dirpath / "x");
    CHECK(found.origin == ReferenceOrigin::User);
    SearchTree::put(t.container / "x");
    found = resolve_reference("x", kind, t.container, {t.system});
    CHECK(found.path == t.container / "x");
    CHECK(found.origin == ReferenceOrigin::Container);
}

TEST_CASE("resolve_reference: not found names every directory searched, in order",
          "[resource_paths][resolve_reference]") {
    SearchTree t;
    const std::string expected = "ROM 'x.rom' not found; searched " + t.container.string() +
                                 ", " + (t.user / "roms").string() + ", " + t.system.string();
    CHECK_THROWS_WITH(resolve_reference("x.rom", ResourceKind::Rom, t.container, {t.system}),
                      expected);
}

TEST_CASE("resolve_reference: a per-user ROM hiding a system ROM is noted, naming both",
          "[resource_paths][resolve_reference][notice]") {
    SearchTree t;
    SearchTree::put(t.system / "x.rom");
    SearchTree::put(t.user / "roms" / "x.rom");

    const auto said = cerr_of(
        [&] { (void)resolve_reference("x.rom", ResourceKind::Rom, t.container, {t.system}); });
    CHECK(said.find((t.user / "roms" / "x.rom").string()) != std::string::npos);
    CHECK(said.find((t.system / "x.rom").string()) != std::string::npos);
    CHECK(std::count(said.begin(), said.end(), '\n') == 1);
}

TEST_CASE("resolve_reference: no notice when nothing is hidden",
          "[resource_paths][resolve_reference][notice]") {
    SearchTree t;
    SearchTree::put(t.user / "roms" / "x.rom");
    SearchTree::put(t.container / "y.rom");
    SearchTree::put(t.system / "y.rom");
    SearchTree::put(t.system / "z.dat");
    SearchTree::put(t.user / "discs" / "z.dat");

    CHECK(cerr_of([&] {
              (void)resolve_reference("x.rom", ResourceKind::Rom, t.container, {t.system});
              // The container's own file is what the preset means: no notice.
              (void)resolve_reference("y.rom", ResourceKind::Rom, t.container, {t.system});
              // Discs: notices are for ROMs only.
              (void)resolve_reference("z.dat", ResourceKind::Disc, t.container, {t.system});
          }).empty());
}

TEST_CASE("resolve_reference: a path is anchored, never searched",
          "[resource_paths][resolve_reference]") {
    SearchTree t;
    SearchTree::put(t.user / "roms" / "x.rom");
    // Rule 2: from the container, whether or not the file exists.
    CHECK(resolve_reference("sub/x.rom", ResourceKind::Rom, t.container, {t.system}).path ==
          (t.container / "sub" / "x.rom").lexically_normal());
    // Rule 3: as is.
    const auto absolute = (t.root / "else" / "x.rom").lexically_normal();
    const auto anchored =
        resolve_reference(absolute.string(), ResourceKind::Rom, t.container, {t.system});
    CHECK(anchored.path == absolute);
    CHECK(anchored.origin == ReferenceOrigin::Path);
    // Rule 4: from home.
    beebium::test::ScopedEnv home("HOME", t.root.path().string());
    CHECK(resolve_reference("~/x.rom", ResourceKind::Rom, t.container, {t.system}).path ==
          (t.root / "x.rom").lexically_normal());
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

TEST_CASE("ReferenceContainer: which paths lie within it, by name", "[resource_paths][container]") {
    const ReferenceContainer container(std::filesystem::path("/c/presets/p"));
    CHECK(container.relative_path_of("/c/presets/p/discs/x.ssd") ==
          std::filesystem::path("discs") / "x.ssd");
    CHECK(container.relative_path_of("/c/presets/p/./discs/../x.ssd") ==
          std::filesystem::path("x.ssd"));
    CHECK_FALSE(container.relative_path_of("/c/presets/other/x.ssd").has_value());
    CHECK_FALSE(container.relative_path_of("/c/presets/p/../x.ssd").has_value());
}

TEST_CASE("ReferenceContainer: reads and copies files within it and elsewhere",
          "[resource_paths][container]") {
    beebium::test::ScopedTempDir root("beebium_container_");
    std::filesystem::create_directories(root / "c" / "sub");
    std::ofstream(root / "c" / "sub" / "in.rom", std::ios::binary) << "INSIDE";
    std::ofstream(root / "out.rom", std::ios::binary) << "OUTSIDE";
    std::filesystem::permissions(root / "c" / "sub" / "in.rom",
                                 std::filesystem::perms::owner_read);
    const ReferenceContainer container(root / "c");

    CHECK(container.has_file(root / "c" / "sub" / "in.rom"));
    CHECK(container.has_file(root / "out.rom"));
    CHECK_FALSE(container.has_file(root / "c" / "sub"));
    CHECK_FALSE(container.has_file(root / "c" / "missing.rom"));

    const auto inside = container.read_file(root / "c" / "sub" / "in.rom");
    CHECK(std::string(inside.begin(), inside.end()) == "INSIDE");
    const auto outside = container.read_file(root / "out.rom");
    CHECK(std::string(outside.begin(), outside.end()) == "OUTSIDE");
    CHECK_THROWS(container.read_file(root / "c" / "missing.rom"));

    // A copy is writable even when the original is not.
    container.copy_file(root / "c" / "sub" / "in.rom", root / "copy.rom");
    std::ifstream copy(root / "copy.rom", std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(copy), {}) == "INSIDE");
    CHECK((std::filesystem::status(root / "copy.rom").permissions() &
           std::filesystem::perms::owner_write) != std::filesystem::perms::none);
}

TEST_CASE("command_line_reference: a bare name the container holds becomes its path",
          "[resource_paths][container]") {
    beebium::test::ScopedTempDir root("beebium_container_");
    std::ofstream(root / "here.ssd") << "x";
    CHECK(command_line_reference("here.ssd", root.path()) == (root / "here.ssd").string());
    CHECK(command_line_reference("elsewhere.ssd", root.path()) == "elsewhere.ssd");
}

namespace {

std::vector<std::uint8_t> as_bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

// A preset archive at root/p.beebium-preset holding discs/x.rom and
// roms/x.rom (two members of one name in different places), with a file
// beside the archive, and its container.
struct ArchiveTree {
    beebium::test::ScopedTempDir root{"beebium_archive_container_"};
    std::filesystem::path archive_filepath = root / "p.beebium-preset";
    ReferenceContainer container = make();

    ReferenceContainer make() {
        beebium::zip::write_zip(archive_filepath, {{"discs/x.rom", as_bytes("DISCS")},
                                                   {"roms/x.rom", as_bytes("ROMS")},
                                                   {"top.rom", as_bytes("TOP")}});
        std::ofstream(root / "beside.rom", std::ios::binary) << "BESIDE";
        return ReferenceContainer::archive(
            archive_filepath, std::make_shared<const beebium::zip::ZipReader>(archive_filepath));
    }
};

}  // namespace

TEST_CASE("ReferenceContainer: an archive is anchored on its own path as a directory",
          "[resource_paths][container][preset_archive]") {
    ArchiveTree t;
    CHECK(t.container.dirpath() == t.archive_filepath);
    CHECK(anchor_reference("roms/x.rom", t.container.dirpath()) ==
          (t.archive_filepath / "roms" / "x.rom").string());
    // Beside the archive, as beside a directory preset.
    CHECK(anchor_reference("../beside.rom", t.container.dirpath()) ==
          (t.root / "beside.rom").string());
}

TEST_CASE("ReferenceContainer: an archive's members are its files; others are ordinary files",
          "[resource_paths][container][preset_archive]") {
    ArchiveTree t;
    CHECK(t.container.has_file(t.archive_filepath / "roms" / "x.rom"));
    CHECK_FALSE(t.container.has_file(t.archive_filepath / "roms"));
    CHECK_FALSE(t.container.has_file(t.archive_filepath / "missing.rom"));
    CHECK(t.container.has_file(t.root / "beside.rom"));

    CHECK(t.container.read_file(t.archive_filepath / "roms" / "x.rom") == as_bytes("ROMS"));
    CHECK(t.container.read_file(t.root / "beside.rom") == as_bytes("BESIDE"));
    CHECK_THROWS(t.container.read_file(t.archive_filepath / "missing.rom"));

    t.container.copy_file(t.archive_filepath / "discs" / "x.rom", t.root / "copy.rom");
    std::ifstream copy(t.root / "copy.rom", std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(copy), {}) == "DISCS");
}

TEST_CASE("ReferenceContainer: a bare name is searched for among an archive's members",
          "[resource_paths][container][preset_archive]") {
    ArchiveTree t;
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", (t.root / "user").string());
    const auto found = resolve_reference("top.rom", ResourceKind::Rom, t.container, {});
    CHECK(found.path == t.archive_filepath / "top.rom");
    CHECK(found.origin == ReferenceOrigin::Container);
    CHECK(command_line_reference("top.rom", t.container) ==
          (t.archive_filepath / "top.rom").string());
}

TEST_CASE("ReferenceContainer::materialize: a member becomes a file at its own relative path "
          "in the machine directory, once",
          "[resource_paths][container][preset_archive]") {
    ArchiveTree t;
    const auto machine = t.root / "machine";
    const auto discs_x = t.container.materialize(t.archive_filepath / "discs" / "x.rom", machine);
    const auto roms_x = t.container.materialize(t.archive_filepath / "roms" / "x.rom", machine);
    CHECK(discs_x == machine / "discs" / "x.rom");
    CHECK(roms_x == machine / "roms" / "x.rom");
    auto read = [](const std::filesystem::path& filepath) {
        std::ifstream in(filepath, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    CHECK(read(discs_x) == "DISCS");
    CHECK(read(roms_x) == "ROMS");
    // Once: a second call finds the first copy, written to since.
    std::ofstream(roms_x, std::ios::binary) << "WRITTEN";
    CHECK(t.container.materialize(t.archive_filepath / "roms" / "x.rom", machine) == roms_x);
    CHECK(read(roms_x) == "WRITTEN");
    // Not a member: the path itself, nothing written.
    CHECK(t.container.materialize(t.root / "beside.rom", machine) == t.root / "beside.rom");
    CHECK(t.container.materialize(t.archive_filepath / "missing.rom", machine) ==
          t.archive_filepath / "missing.rom");
    // A directory container's files are real already.
    const ReferenceContainer directory(t.root.path());
    CHECK(directory.materialize(t.root / "beside.rom", machine) == t.root / "beside.rom");
}
