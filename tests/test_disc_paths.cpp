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

// Unit tests for DiscPaths: how a disc reference resolves, and what a
// machine copies into its own directory. Every directory is a temporary one,
// pinned through the environment where the server reads it, so no real
// per-user state is touched.

#include <catch2/catch_test_macros.hpp>

#include <catch2/matchers/catch_matchers_string.hpp>

#include "beebium/server/DiscPaths.hpp"
#include "beebium/zip/ZipArchive.hpp"
#include "test_env.hpp"
#include "test_temp_paths.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <fstream>
#include <stdexcept>
#include <string>

using beebium::server::DiscPaths;

namespace {

namespace fs = std::filesystem;

void write_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

}  // namespace

// ---------------------------------------------------------------------------
// resolve_disc_image: the file reference rule, and what a machine copies
// ---------------------------------------------------------------------------

namespace {

// A preset's directory (the template), a per-user directory pinned through
// the environment, and a machine directory, with a hard disc in the per-user
// discs/. `destination` copies into the machine, launched from the template;
// `cli` as a launch with no template.
struct DiscSearchTree {
    beebium::test::ScopedTempDir root{"beebium_disc_search_"};
    fs::path container = root / "container";
    fs::path user = root / "user";
    fs::path machine = root / "machine";
    beebium::test::ScopedEnv user_env{"BEEBIUM_USER_DIRPATH", user.string()};
    DiscPaths::CopyDestination destination{machine, container};
    DiscPaths::CopyDestination cli{machine, std::nullopt};

    DiscSearchTree() {
        for (const auto& d : {container, user / "discs", machine}) {
            fs::create_directories(d);
        }
        write_file(user / "discs" / "l3fs.dat", "USER");
        write_file(user / "discs" / "l3fs.dsc", "geometry");
    }
};

}  // namespace

TEST_CASE("resolve_disc_image: a per-user disc found by search is copied, the original kept",
          "[disc][discpaths][resolve]") {
    DiscSearchTree t;
    write_file(t.user / "discs" / "mine.dat", "MINE");
    write_file(t.user / "discs" / "mine.dsc", "geometry");
    const auto resolved = DiscPaths::resolve_disc_image("mine.dat", t.container, t.cli);
    CHECK(resolved == t.machine / "mine.dat");
    CHECK(read_file(resolved) == "MINE");
    CHECK(read_file(t.machine / "mine.dsc") == "geometry");
    write_file(resolved, "WRITTEN");
    CHECK(read_file(t.user / "discs" / "mine.dat") == "MINE");
}

TEST_CASE("resolve_disc_image: the template's own discs are copied, at their own paths",
          "[disc][discpaths][resolve]") {
    DiscSearchTree t;
    write_file(t.container / "l3fs.dat", "BESIDE");
    write_file(t.container / "sub" / "game.ssd", "GAME");
    // Beside the template, by bare name, ahead of the per-user disc of that
    // name.
    const auto bare = DiscPaths::resolve_disc_image("l3fs.dat", t.container, t.destination);
    CHECK(bare == t.machine / "l3fs.dat");
    CHECK(read_file(bare) == "BESIDE");
    // By a path within it, anchored or absolute.
    CHECK(DiscPaths::resolve_disc_image("sub/game.ssd", t.container, t.destination) ==
          t.machine / "sub" / "game.ssd");
    CHECK(DiscPaths::resolve_disc_image((t.container / "sub" / "game.ssd").string(), t.container,
                                        t.destination) == t.machine / "sub" / "game.ssd");
    CHECK(read_file(t.container / "sub" / "game.ssd") == "GAME");
}

TEST_CASE("resolve_disc_image: media named by location outside the template is used in place",
          "[disc][discpaths][resolve]") {
    DiscSearchTree t;
    const auto elsewhere = (t.root / "elsewhere" / "game.ssd").lexically_normal();
    write_file(elsewhere, "MINE");
    CHECK(DiscPaths::resolve_disc_image(elsewhere.string(), t.container, t.destination) ==
          elsewhere);
    beebium::test::ScopedEnv home("HOME", (t.root / "elsewhere").string());
    CHECK(DiscPaths::resolve_disc_image("~/game.ssd", t.container, t.destination) == elsewhere);
    // Without a template, every path is the user's: the command line's.
    write_file(t.container / "cwd.ssd", "CWD");
    CHECK(DiscPaths::resolve_disc_image((t.container / "cwd.ssd").string(), t.container, t.cli) ==
          t.container / "cwd.ssd");
    CHECK(fs::is_empty(t.machine));
}

TEST_CASE("resolve_disc_image: a disc named twice is copied once",
          "[disc][discpaths][resolve]") {
    DiscSearchTree t;
    const auto first = DiscPaths::resolve_disc_image("l3fs.dat", t.container, t.destination);
    write_file(first, "WRITTEN");
    CHECK(DiscPaths::resolve_disc_image("l3fs.dat", t.container, t.destination) == first);
    CHECK(read_file(first) == "WRITTEN");
}

TEST_CASE("resolve_disc_image: a name found nowhere names every directory searched",
          "[disc][discpaths][resolve]") {
    DiscSearchTree t;
    CHECK_THROWS_WITH(DiscPaths::resolve_disc_image("nope.dat", t.container, t.destination),
                      "Disc image 'nope.dat' not found; searched " + t.container.string() +
                          ", " + (t.user / "discs").string());
}

TEST_CASE("resolve_disc_image: a path to nothing is returned for its user to report",
          "[disc][discpaths][resolve]") {
    DiscSearchTree t;
    CHECK(DiscPaths::resolve_disc_image("sub/missing.ssd", t.container, t.destination) ==
          (t.container / "sub" / "missing.ssd").lexically_normal());
}

TEST_CASE("resolve_disc_image: a disc held in a preset archive is extracted into the machine",
          "[disc][discpaths][resolve][preset_archive]") {
    DiscSearchTree t;
    const auto archive_filepath = t.root / "p.beebium-preset";
    auto bytes = [](const std::string& text) {
        return std::vector<std::uint8_t>(text.begin(), text.end());
    };
    beebium::zip::write_zip(archive_filepath, {{"top.ssd", bytes("TOP")},
                                               {"discs/fs.dat", bytes("FS")},
                                               {"discs/fs.dsc", bytes("FS-GEOMETRY")}});
    const auto archive = beebium::server::ReferenceContainer::archive(
        archive_filepath, std::make_shared<const beebium::zip::ZipReader>(archive_filepath));
    const DiscPaths::CopyDestination destination{t.machine, archive_filepath};

    // By bare name, and by path with its geometry beside it.
    const auto top = DiscPaths::resolve_disc_image("top.ssd", archive, destination);
    CHECK(top == t.machine / "top.ssd");
    CHECK(read_file(top) == "TOP");
    const auto fs_dat = DiscPaths::resolve_disc_image("discs/fs.dat", archive, destination);
    CHECK(fs_dat == t.machine / "discs" / "fs.dat");
    CHECK(read_file(fs_dat) == "FS");
    CHECK(read_file(t.machine / "discs" / "fs.dsc") == "FS-GEOMETRY");
    // The copy is the machine's to write.
    CHECK((fs::status(fs_dat).permissions() & fs::perms::owner_write) != fs::perms::none);
    // One the archive does not hold is the path, for the caller to report.
    CHECK(DiscPaths::resolve_disc_image("discs/missing.ssd", archive, destination) ==
          archive_filepath / "discs" / "missing.ssd");
}
