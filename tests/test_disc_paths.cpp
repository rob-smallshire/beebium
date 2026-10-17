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

#include <catch2/matchers/catch_matchers_string.hpp>

#include "beebium/server/DiscPaths.hpp"
#include "test_env.hpp"
#include "test_temp_paths.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

using beebium::server::DiscPaths;
using beebium::server::DiscWorkMode;

namespace {

namespace fs = std::filesystem;

// A process-unique random tag, so parallel test processes don't collide in
// the shared temp directory. std::random_device is portable (no POSIX
// getpid()), which keeps this test buildable on MSVC as well as POSIX.
inline std::string unique_tag() {
    std::random_device rd;
    return std::to_string((static_cast<std::uint64_t>(rd()) << 32) ^ rd());
}

struct TmpTree {
    fs::path root;
    TmpTree() {
        root = fs::temp_directory_path() /
               ("beebium_disc_paths_" + unique_tag() + "_" +
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
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// Build a master l3fs-v1_26.dat + .dsc + .json in master_dir, the image
// files marked read-only (as shipped). Returns nothing; asserts creation.
void make_master(const fs::path& master_dir, const std::string& dat,
                 const std::string& dsc) {
    write_file(master_dir / "l3fs-v1_26.dat", dat);
    write_file(master_dir / "l3fs-v1_26.dsc", dsc);
    write_file(master_dir / "l3fs-v1_26.json", R"({"display_name": "L3FS"})");
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

TEST_CASE("copy_file_atomically: a failure mid-copy leaves the old copy intact",
          "[disc][discpaths]") {
    TmpTree tmp;
    auto dir = tmp.subdir("work");
    write_file(dir / "src.dat", std::string(4096, 'N'));
    write_file(dir / "dst.dat", "OLD-WORKING-COPY");

    // The copier writes half the image, then fails (a full disc, say).
    auto failing_copy = [](const fs::path& src, const fs::path& dst) {
        write_file(dst, read_file(src).substr(0, 2048));
        throw std::runtime_error("simulated failure mid-copy");
    };
    CHECK_THROWS_AS(DiscPaths::copy_file_atomically(dir / "src.dat", dir / "dst.dat",
                                                    failing_copy),
                    std::runtime_error);

    CHECK(read_file(dir / "dst.dat") == "OLD-WORKING-COPY");
    // The partial temporary is cleaned up: only the two files remain.
    size_t entries = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator(dir)) ++entries;
    CHECK(entries == 2);
}

TEST_CASE("copy_file_atomically replaces the destination with a writable copy",
          "[disc][discpaths]") {
    TmpTree tmp;
    auto dir = tmp.subdir("work");
    write_file(dir / "src.dat", "NEW");
    fs::permissions(dir / "src.dat", fs::perms::owner_read);
    write_file(dir / "dst.dat", "OLD");
    fs::permissions(dir / "dst.dat", fs::perms::owner_read);

    DiscPaths::copy_file_atomically(dir / "src.dat", dir / "dst.dat");

    CHECK(read_file(dir / "dst.dat") == "NEW");
    auto perms = fs::status(dir / "dst.dat").permissions();
    CHECK((perms & fs::perms::owner_write) != fs::perms::none);
}

#ifndef _WIN32
// Two preparations of the same scratch target racing, as two servers sharing
// one BEEBIUM_DISC_WORK_DIR do, while a reader repeatedly opens the working
// image: the reader must only ever see the complete image. (Not on Windows,
// where replacing a file another process holds open fails by design.)
TEST_CASE("prepare_working_image (Scratch): a reader never sees a short image",
          "[disc][discpaths]") {
    TmpTree tmp;
    auto master = tmp.subdir("master");
    auto work = tmp.subdir("work");
    const std::string image(4 * 1024 * 1024, 'D');
    make_master(master, image, "DSC");
    DiscPaths::prepare_working_image("l3fs-v1_26.dat", master, work, DiscWorkMode::Scratch);

    std::atomic<bool> done{false};
    auto preparer = [&] {
        while (!done.load()) {
            DiscPaths::prepare_working_image("l3fs-v1_26.dat", master, work,
                                             DiscWorkMode::Scratch);
        }
    };
    std::thread first(preparer);
    std::thread second(preparer);

    int short_or_missing = 0;
    for (int i = 0; i < 300; ++i) {
        std::error_code ec;
        const auto size = fs::file_size(work / "l3fs-v1_26.dat", ec);
        if (ec || size != image.size()) ++short_or_missing;
    }
    done.store(true);
    first.join();
    second.join();

    CHECK(short_or_missing == 0);
}
#endif

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

// ---------------------------------------------------------------------------
// resolve_disc_image: the file reference rule, and what a machine copies
// ---------------------------------------------------------------------------

namespace {

// A preset's directory (the template), a per-user directory, a master
// directory and a machine directory, the first three pinned through the
// environment. `destination` copies into the machine, launched from the
// template; `cli` as a launch with no template.
struct DiscSearchTree {
    beebium::test::ScopedTempDir root{"beebium_disc_search_"};
    fs::path container = root / "container";
    fs::path user = root / "user";
    fs::path master = root / "master";
    fs::path machine = root / "machine";
    beebium::test::ScopedEnv user_env{"BEEBIUM_USER_DIRPATH", user.string()};
    beebium::test::ScopedEnv master_env{"BEEBIUM_DISC_DIR", master.string()};
    DiscPaths::CopyDestination destination{machine, container};
    DiscPaths::CopyDestination cli{machine, std::nullopt};

    DiscSearchTree() {
        for (const auto& d : {container, user / "discs", master, machine}) {
            fs::create_directories(d);
        }
        write_file(master / "l3fs.dat", "MASTER");
        write_file(master / "l3fs.dsc", "geometry");
        write_file(master / "l3fs.json", R"({"display_name": "L3FS"})");
    }
};

}  // namespace

TEST_CASE("resolve_disc_image: a master is copied into the machine, never into discs/",
          "[disc][discpaths][resolve]") {
    DiscSearchTree t;
    const auto resolved = DiscPaths::resolve_disc_image("l3fs.dat", t.container, t.destination);
    CHECK(resolved == t.machine / "l3fs.dat");
    CHECK(read_file(resolved) == "MASTER");
    CHECK(fs::exists(t.machine / "l3fs.dsc"));
    CHECK(fs::is_empty(t.user / "discs"));
}

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
    // Beside the template, by bare name, ahead of the master of that name.
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
                          ", " + (t.user / "discs").string() + ", " + t.master.string());
}

TEST_CASE("resolve_disc_image: a path to nothing is returned for its user to report",
          "[disc][discpaths][resolve]") {
    DiscSearchTree t;
    CHECK(DiscPaths::resolve_disc_image("sub/missing.ssd", t.container, t.destination) ==
          (t.container / "sub" / "missing.ssd").lexically_normal());
}

// ---------------------------------------------------------------------------
// bundled_hard_disc_images: the masters a bare hard-disc name can refer to
// ---------------------------------------------------------------------------

TEST_CASE("bundled_hard_disc_images lists each .dat with its .dsc and .json, by name",
          "[disc_paths][bundled]") {
    beebium::test::ScopedTempDir master_dir("beebium_bundled_");
    write_file(master_dir / "zeta.dat", "0123456789");
    write_file(master_dir / "zeta.dsc", "g");
    write_file(master_dir / "zeta.json", R"({"display_name": "Zeta"})");
    write_file(master_dir / "alpha.dat", "01234");
    write_file(master_dir / "alpha.dsc", "g");
    write_file(master_dir / "alpha.json", R"({"display_name": "Alpha"})");
    write_file(master_dir / "orphan.dat", "x");     // no sidecars: not a master
    write_file(master_dir / "unnamed.dat", "x");    // no .json: not a master
    write_file(master_dir / "unnamed.dsc", "g");
    write_file(master_dir / "notes.txt", "x");

    auto images = DiscPaths::bundled_hard_disc_images(master_dir.path());

    REQUIRE(images.size() == 2);
    CHECK(images[0].image == "alpha.dat");
    CHECK(images[0].size_bytes == 5);
    CHECK(images[1].image == "zeta.dat");
    CHECK(images[1].size_bytes == 10);
}

TEST_CASE("bundled_hard_disc_images takes a master's display name from its .json",
          "[disc_paths][bundled]") {
    beebium::test::ScopedTempDir master_dir("beebium_bundled_");
    write_file(master_dir / "named.dat", "0");
    write_file(master_dir / "named.dsc", "g");
    write_file(master_dir / "named.json", R"({"display_name": "A Named Disc"})");
    write_file(master_dir / "nameless.dat", "0");
    write_file(master_dir / "nameless.dsc", "g");
    write_file(master_dir / "nameless.json", R"({"title": "no display_name"})");

    auto images = DiscPaths::bundled_hard_disc_images(master_dir.path());

    REQUIRE(images.size() == 1);
    CHECK(images[0].image == "named.dat");
    CHECK(images[0].display_name == "A Named Disc");
}

TEST_CASE("prepare_working_image refuses a master with no display name",
          "[disc][discpaths]") {
    TmpTree tmp;
    auto master = tmp.subdir("master");
    auto work = tmp.subdir("work");
    make_master(master, "DATA-A", "DSC-A");
    fs::remove(master / "l3fs-v1_26.json");

    try {
        DiscPaths::prepare_working_image("l3fs-v1_26.dat", master, work,
                                         DiscWorkMode::Persistent);
        FAIL("a master with no .json was used");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find("l3fs-v1_26.json") != std::string::npos);
    }
}

TEST_CASE("every committed disc master has a .json giving its display name",
          "[disc_paths][bundled]") {
    const fs::path bundled_dirpath(BEEBIUM_BUNDLED_DISCS_SRC_DIRPATH);
    int tarballs = 0;
    for (const auto& entry : fs::directory_iterator(bundled_dirpath)) {
        const auto filename = entry.path().filename().string();
        const std::string suffix = ".tar.xz";
        if (filename.size() <= suffix.size() ||
            filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        ++tarballs;
        const auto id = filename.substr(0, filename.size() - suffix.size());
        INFO(id);
        CHECK(DiscPaths::master_display_name(bundled_dirpath / (id + ".dat")).has_value());
    }
    CHECK(tarballs > 0);
}

TEST_CASE("bundled_hard_disc_images of a missing directory is empty",
          "[disc_paths][bundled]") {
    beebium::test::ScopedTempDir dir("beebium_bundled_");
    CHECK(DiscPaths::bundled_hard_disc_images(dir / "absent").empty());
}
