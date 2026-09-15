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

// Unit tests for DiscPaths copy-on-write resolution. Exercise the
// dir-injecting core (prepare_working_image) with temp master/working
// directories, so no environment or per-user state is touched.

#include <catch2/catch_test_macros.hpp>

#include "beebium/server/DiscPaths.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

using beebium::server::DiscPaths;
using beebium::server::DiscWorkMode;

namespace {

namespace fs = std::filesystem;

struct TmpTree {
    fs::path root;
    TmpTree() {
        root = fs::temp_directory_path() /
               ("beebium_disc_paths_" + std::to_string(::getpid()) + "_" +
                std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }
    ~TmpTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    fs::path subdir(const std::string& name) {
        auto p = root / name;
        fs::create_directories(p);
        return p;
    }
};

void write_file(const fs::path& p, const std::string& content) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// Build a master l3fs-v1_26.dat + .dsc in master_dir, marked read-only (as
// shipped). Returns nothing; asserts creation.
void make_master(const fs::path& master_dir, const std::string& dat,
                 const std::string& dsc) {
    write_file(master_dir / "l3fs-v1_26.dat", dat);
    write_file(master_dir / "l3fs-v1_26.dsc", dsc);
    fs::permissions(master_dir / "l3fs-v1_26.dat",
                    fs::perms::owner_read | fs::perms::group_read |
                        fs::perms::others_read);
    fs::permissions(master_dir / "l3fs-v1_26.dsc",
                    fs::perms::owner_read | fs::perms::group_read |
                        fs::perms::others_read);
}

}  // namespace

TEST_CASE("prepare_working_image copies a read-only master to a writable copy",
          "[disc][discpaths]") {
    TmpTree tmp;
    auto master = tmp.subdir("master");
    auto work = tmp.subdir("work");
    make_master(master, "DATA-A", "DSC-A");

    auto working = DiscPaths::prepare_working_image(
        "l3fs-v1_26.dat", master, work, DiscWorkMode::Persistent);

    // Returned path is in the working dir, never the master.
    CHECK(working == work / "l3fs-v1_26.dat");
    CHECK(read_file(working) == "DATA-A");
    // The .dsc sidecar came along.
    CHECK(read_file(work / "l3fs-v1_26.dsc") == "DSC-A");

    // Working copy is writable even though the master is read-only.
    auto perms = fs::status(working).permissions();
    CHECK((perms & fs::perms::owner_write) != fs::perms::none);

    // Master is untouched and still read-only.
    CHECK(read_file(master / "l3fs-v1_26.dat") == "DATA-A");
    auto master_perms = fs::status(master / "l3fs-v1_26.dat").permissions();
    CHECK((master_perms & fs::perms::owner_write) == fs::perms::none);
}

TEST_CASE("prepare_working_image (Persistent) keeps an existing working copy",
          "[disc][discpaths]") {
    TmpTree tmp;
    auto master = tmp.subdir("master");
    auto work = tmp.subdir("work");
    make_master(master, "DATA-A", "DSC-A");

    auto working = DiscPaths::prepare_working_image(
        "l3fs-v1_26.dat", master, work, DiscWorkMode::Persistent);

    // The emulator writes to its working copy.
    write_file(working, "MODIFIED-BY-GUEST");

    // Persistent resolution must NOT clobber the user's working copy.
    auto again = DiscPaths::prepare_working_image(
        "l3fs-v1_26.dat", master, work, DiscWorkMode::Persistent);
    CHECK(again == working);
    CHECK(read_file(again) == "MODIFIED-BY-GUEST");
}

TEST_CASE("prepare_working_image (Scratch) always refreshes from the master",
          "[disc][discpaths]") {
    TmpTree tmp;
    auto master = tmp.subdir("master");
    auto work = tmp.subdir("work");
    make_master(master, "DATA-A", "DSC-A");

    auto working = DiscPaths::prepare_working_image(
        "l3fs-v1_26.dat", master, work, DiscWorkMode::Scratch);
    write_file(working, "STALE");

    // Scratch resolution replaces the stale copy with a pristine master copy.
    auto again = DiscPaths::prepare_working_image(
        "l3fs-v1_26.dat", master, work, DiscWorkMode::Scratch);
    CHECK(read_file(again) == "DATA-A");
    // Refreshed copy is writable.
    auto perms = fs::status(again).permissions();
    CHECK((perms & fs::perms::owner_write) != fs::perms::none);
}

TEST_CASE("prepare_working_image (Scratch) replaces a read-only stale copy",
          "[disc][discpaths]") {
    // A previous scratch build may have left a read-only copy; the refresh
    // must still truncate/replace it, not fail.
    TmpTree tmp;
    auto master = tmp.subdir("master");
    auto work = tmp.subdir("work");
    make_master(master, "DATA-A", "DSC-A");

    write_file(work / "l3fs-v1_26.dat", "OLD");
    write_file(work / "l3fs-v1_26.dsc", "OLD");
    fs::permissions(work / "l3fs-v1_26.dat", fs::perms::owner_read);

    auto again = DiscPaths::prepare_working_image(
        "l3fs-v1_26.dat", master, work, DiscWorkMode::Scratch);
    CHECK(read_file(again) == "DATA-A");
}

TEST_CASE("prepare_working_image throws when the master is absent",
          "[disc][discpaths]") {
    TmpTree tmp;
    auto master = tmp.subdir("master");  // empty
    auto work = tmp.subdir("work");
    CHECK_THROWS(DiscPaths::prepare_working_image(
        "nope.dat", master, work, DiscWorkMode::Persistent));
}

TEST_CASE("prepare_working_image throws when the .dsc sidecar is missing",
          "[disc][discpaths]") {
    TmpTree tmp;
    auto master = tmp.subdir("master");
    auto work = tmp.subdir("work");
    write_file(master / "l3fs-v1_26.dat", "DATA-A");  // no .dsc
    CHECK_THROWS(DiscPaths::prepare_working_image(
        "l3fs-v1_26.dat", master, work, DiscWorkMode::Persistent));
}

TEST_CASE("resolve_disc_image passes explicit paths through untouched",
          "[disc][discpaths]") {
    // Absolute path or a path with a directory component is the user's own
    // image: returned verbatim, no copy-on-write.
    TmpTree tmp;
    auto explicit_abs = (tmp.root / "my-hdd.dat");
    write_file(explicit_abs, "USER");
    CHECK(DiscPaths::resolve_disc_image(explicit_abs.string()) == explicit_abs);

    fs::path rel = fs::path("subdir") / "user.dat";
    CHECK(DiscPaths::resolve_disc_image(rel.string()) == rel);
}
