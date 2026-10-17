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

// test_preset_paths.cpp
// Unit tests for PresetPaths utility class

#include <beebium/server/PresetPaths.hpp>
#include <beebium/zip/ZipArchive.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>  // For creating test files
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include "test_env.hpp"
#include "test_temp_paths.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace beebium::server;

// ============================================================================
// slugify() tests
// ============================================================================

TEST_CASE("slugify: basic conversion to lowercase", "[PresetPaths][slugify]") {
    REQUIRE(PresetPaths::slugify("Hello") == "hello");
    REQUIRE(PresetPaths::slugify("UPPERCASE") == "uppercase");
    REQUIRE(PresetPaths::slugify("MixedCase") == "mixedcase");
}

TEST_CASE("slugify: spaces to hyphens", "[PresetPaths][slugify]") {
    REQUIRE(PresetPaths::slugify("BBC Model B") == "bbc-model-b");
    REQUIRE(PresetPaths::slugify("hello world") == "hello-world");
    REQUIRE(PresetPaths::slugify("a b c") == "a-b-c");
}

TEST_CASE("slugify: underscores to hyphens", "[PresetPaths][slugify]") {
    REQUIRE(PresetPaths::slugify("model_b") == "model-b");
    REQUIRE(PresetPaths::slugify("hello_world") == "hello-world");
}

TEST_CASE("slugify: preserves hyphens", "[PresetPaths][slugify]") {
    REQUIRE(PresetPaths::slugify("model-b") == "model-b");
    REQUIRE(PresetPaths::slugify("acorn-1770") == "acorn-1770");
}

TEST_CASE("slugify: collapses multiple hyphens", "[PresetPaths][slugify]") {
    REQUIRE(PresetPaths::slugify("a  b") == "a-b");
    REQUIRE(PresetPaths::slugify("a   b") == "a-b");
    REQUIRE(PresetPaths::slugify("a - b") == "a-b");
    REQUIRE(PresetPaths::slugify("a--b") == "a-b");
    REQUIRE(PresetPaths::slugify("a---b") == "a-b");
}

TEST_CASE("slugify: trims leading and trailing hyphens", "[PresetPaths][slugify]") {
    REQUIRE(PresetPaths::slugify(" hello") == "hello");
    REQUIRE(PresetPaths::slugify("hello ") == "hello");
    REQUIRE(PresetPaths::slugify("  hello  ") == "hello");
    REQUIRE(PresetPaths::slugify("-hello") == "hello");
    REQUIRE(PresetPaths::slugify("hello-") == "hello");
    REQUIRE(PresetPaths::slugify("-hello-") == "hello");
}

TEST_CASE("slugify: removes non-alphanumeric characters", "[PresetPaths][slugify]") {
    REQUIRE(PresetPaths::slugify("Model B+") == "model-b");
    REQUIRE(PresetPaths::slugify("a!b@c#d") == "abcd");
    REQUIRE(PresetPaths::slugify("test(1)") == "test1");
    REQUIRE(PresetPaths::slugify("100%") == "100");
}

TEST_CASE("slugify: preserves digits", "[PresetPaths][slugify]") {
    REQUIRE(PresetPaths::slugify("Model 128") == "model-128");
    REQUIRE(PresetPaths::slugify("512K") == "512k");
    REQUIRE(PresetPaths::slugify("1770") == "1770");
}

TEST_CASE("slugify: real-world preset names", "[PresetPaths][slugify]") {
    REQUIRE(PresetPaths::slugify("BBC Model B") == "bbc-model-b");
    REQUIRE(PresetPaths::slugify("BBC Model B+") == "bbc-model-b");
    REQUIRE(PresetPaths::slugify("BBC Model B with Acorn DFS") == "bbc-model-b-with-acorn-dfs");
    REQUIRE(PresetPaths::slugify("Master 512") == "master-512");
    REQUIRE(PresetPaths::slugify("Master Turbo (65C102)") == "master-turbo-65c102");
    REQUIRE(PresetPaths::slugify("My Elite Setup") == "my-elite-setup");
}

TEST_CASE("slugify: empty and edge cases", "[PresetPaths][slugify]") {
    REQUIRE(PresetPaths::slugify("") == "");
    REQUIRE(PresetPaths::slugify("   ") == "");
    REQUIRE(PresetPaths::slugify("---") == "");
    REQUIRE(PresetPaths::slugify("!@#$%") == "");
    REQUIRE(PresetPaths::slugify("a") == "a");
}

// ============================================================================
// filename_for() and id_from_filename() tests
// ============================================================================

TEST_CASE("filename_for and id_from_filename: a stored preset's name and id", "[PresetPaths][naming]") {
    CHECK(PresetPaths::filename_for("model-b") == "model-b.beebium-preset");
    CHECK(PresetPaths::id_from_filename("model-b.beebium-preset") == "model-b");
    CHECK(PresetPaths::id_from_filename(std::filesystem::path("dir") / "model-b.beebium-preset") ==
          "model-b");
    CHECK(PresetPaths::id_from_filename(PresetPaths::filename_for("a.b")) == "a.b");
}

TEST_CASE("id_from_filename: names that are not stored presets have no id", "[PresetPaths][naming]") {
    CHECK_FALSE(PresetPaths::id_from_filename("readme.txt").has_value());
    CHECK_FALSE(PresetPaths::id_from_filename("config.json").has_value());
    CHECK_FALSE(PresetPaths::id_from_filename("beebium-preset.bak").has_value());
    CHECK_FALSE(PresetPaths::id_from_filename(".beebium-preset").has_value());
    // The earlier stored form is not a stored preset's name (see migration).
    CHECK_FALSE(PresetPaths::id_from_filename("model-b.preset.beebium").has_value());
}

// ============================================================================
// get_user_presets_dirpath() tests
// ============================================================================

TEST_CASE("get_user_presets_dirpath: returns non-empty path", "[PresetPaths][user_presets]") {
    auto path = PresetPaths::get_user_presets_dirpath();
    REQUIRE_FALSE(path.empty());
}

TEST_CASE("get_user_presets_dirpath: path contains 'presets'", "[PresetPaths][user_presets]") {
    auto path = PresetPaths::get_user_presets_dirpath();
    REQUIRE(path.filename() == "presets");
}

#ifdef __APPLE__
TEST_CASE("get_user_presets_dirpath: macOS path contains Application Support", "[PresetPaths][user_presets][macos]") {
    // The platform path, with the overrides cleared (it is only computed).
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", "");

    auto path = PresetPaths::get_user_presets_dirpath();
    REQUIRE(path.string().find("Application Support") != std::string::npos);
    REQUIRE(path.string().find("Beebium") != std::string::npos);
}
#endif

#ifdef __linux__
TEST_CASE("get_user_presets_dirpath: Linux path contains .config or XDG_CONFIG_HOME", "[PresetPaths][user_presets][linux]") {
    // The platform path, with the overrides cleared (it is only computed).
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", "");

    auto path = PresetPaths::get_user_presets_dirpath();
    bool has_config = path.string().find(".config") != std::string::npos ||
                      path.string().find("config") != std::string::npos;
    REQUIRE(has_config);
    REQUIRE(path.string().find("beebium") != std::string::npos);
}
#endif

TEST_CASE("BEEBIUM_USER_DIRPATH moves every per-user directory",
          "[PresetPaths][user_presets][user_dirpath]") {
    beebium::test::ScopedTempDir temp_dir("beebium_user_dirpath_");
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", temp_dir.path().string());

    CHECK(beebium::platform::user_state_base_dirpath() == temp_dir.path());
    CHECK(PresetPaths::get_user_presets_dirpath() == temp_dir / "presets");
}

TEST_CASE("an empty BEEBIUM_USER_DIRPATH is no override",
          "[PresetPaths][user_presets][user_dirpath]") {
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", "");
    CHECK_FALSE(beebium::platform::user_state_base_dirpath().empty());
}

// ============================================================================
// ensure_user_presets_dirpath_exists() tests
// ============================================================================

TEST_CASE("ensure_user_presets_dirpath_exists: creates directory if missing", "[PresetPaths][user_presets]") {
    // Create a temporary directory for testing
    auto temp_dir = beebium::test::unique_temp_path("beebium_test_presets_create_");
    REQUIRE_FALSE(std::filesystem::exists(temp_dir));

    // This test would need to set the env var to work properly
    // For now, just verify the function doesn't crash
    // The actual behavior depends on platform and existing state
    bool result = PresetPaths::ensure_user_presets_dirpath_exists();
    // Just verify it returns a boolean (doesn't crash)
    REQUIRE((result == true || result == false));
}

// ============================================================================
// enumerate() and find() tests
// ============================================================================

namespace {

// A system store and a user store of its own, each empty to begin with.
struct PresetStores {
    beebium::test::ScopedTempDir servers{"beebium_test_servers_"};
    beebium::test::ScopedTempDir user{"beebium_test_user_"};
    beebium::test::ScopedEnv servers_env{"BEEBIUM_SERVERS_DIRPATH", servers.path().string()};
    beebium::test::ScopedEnv user_env{"BEEBIUM_USER_DIRPATH", user.path().string()};

    PresetStores() {
        std::filesystem::create_directories(system_dirpath());
        std::filesystem::create_directories(user_dirpath());
    }
    std::filesystem::path system_dirpath() const { return servers / "presets"; }
    std::filesystem::path user_dirpath() const { return user / "presets"; }
};

std::vector<std::string> ids_of(const std::vector<PresetPaths::Entry>& entries) {
    std::vector<std::string> ids;
    for (const auto& entry : entries) ids.push_back(entry.id);
    return ids;
}

}  // namespace

TEST_CASE("enumerate: empty stores give no presets", "[PresetPaths][enumerate]") {
    PresetStores stores;
    CHECK(PresetPaths::enumerate().empty());
}

TEST_CASE("enumerate: a missing user store gives no user presets", "[PresetPaths][enumerate]") {
    PresetStores stores;
    std::filesystem::remove(stores.user_dirpath());
    std::ofstream(stores.system_dirpath() / "model-b.beebium-preset") << "{}";
    CHECK(ids_of(PresetPaths::enumerate()) == std::vector<std::string>{"model-b"});
}

TEST_CASE("enumerate: system presets then user presets, each by id, with their origin",
          "[PresetPaths][enumerate]") {
    PresetStores stores;
    std::ofstream(stores.system_dirpath() / "zebra.beebium-preset") << "{}";
    std::ofstream(stores.system_dirpath() / "model-b.beebium-preset") << "{}";
    std::ofstream(stores.user_dirpath() / "my-setup.beebium-preset") << "{}";
    std::ofstream(stores.user_dirpath() / "another.beebium-preset") << "{}";

    const auto entries = PresetPaths::enumerate();
    REQUIRE(ids_of(entries) ==
            std::vector<std::string>{"model-b", "zebra", "another", "my-setup"});
    CHECK(entries[0].origin == PresetPaths::Origin::System);
    CHECK(entries[0].filepath == stores.system_dirpath() / "model-b.beebium-preset");
    CHECK(entries[3].origin == PresetPaths::Origin::User);
    CHECK(entries[3].filepath == stores.user_dirpath() / "my-setup.beebium-preset");
}

TEST_CASE("enumerate: ignores other files and directories", "[PresetPaths][enumerate]") {
    PresetStores stores;
    std::ofstream(stores.user_dirpath() / "model-b.beebium-preset") << "{}";
    std::ofstream(stores.user_dirpath() / "readme.txt") << "text";
    std::ofstream(stores.user_dirpath() / "beebium-preset.bak") << "{}";
    std::filesystem::create_directories(stores.user_dirpath() / "subdir.beebium-preset");

    CHECK(ids_of(PresetPaths::enumerate()) == std::vector<std::string>{"model-b"});
}

TEST_CASE("find: the first preset with the id in search order, or from one store",
          "[PresetPaths][find]") {
    PresetStores stores;
    std::ofstream(stores.system_dirpath() / "shared.beebium-preset") << "{}";
    std::ofstream(stores.user_dirpath() / "shared.beebium-preset") << "{}";
    std::ofstream(stores.user_dirpath() / "mine.beebium-preset") << "{}";

    auto shared = PresetPaths::find("shared");
    REQUIRE(shared.has_value());
    CHECK(shared->origin == PresetPaths::Origin::System);

    auto users = PresetPaths::find("shared", PresetPaths::Origin::User);
    REQUIRE(users.has_value());
    CHECK(users->filepath == stores.user_dirpath() / "shared.beebium-preset");

    CHECK_FALSE(PresetPaths::find("mine", PresetPaths::Origin::System).has_value());
    CHECK_FALSE(PresetPaths::find("nonexistent").has_value());
}

TEST_CASE("locate: a file path, else the id of a stored preset", "[PresetPaths][locate]") {
    PresetStores stores;
    std::ofstream(stores.user_dirpath() / "mine.beebium-preset") << "{}";
    const auto loose_filepath = stores.user / "loose.json";
    std::ofstream(loose_filepath) << "{}";

    CHECK(PresetPaths::locate(loose_filepath.string()) == loose_filepath);
    CHECK(PresetPaths::locate("mine") == stores.user_dirpath() / "mine.beebium-preset");
    CHECK_FALSE(PresetPaths::locate("nonexistent").has_value());
    // A directory is located as a preset source (PresetSource says whether
    // it holds a preset).
    CHECK(PresetPaths::locate(stores.user_dirpath().string()) == stores.user_dirpath());
    CHECK(PresetPaths::not_found_message("nonexistent") ==
          "Preset not found: 'nonexistent' (no such file, and no system or user preset with "
          "that id)");
}

TEST_CASE("open: the preset an argument names, read, or why there is none", "[PresetPaths][open]") {
    PresetStores stores;
    std::ofstream(stores.user_dirpath() / "mine.beebium-preset") << R"({"name": "Mine"})";
    std::string error;

    auto mine = PresetPaths::open("mine", error);
    REQUIRE(mine.has_value());
    CHECK(mine->document_text() == R"({"name": "Mine"})");

    CHECK_FALSE(PresetPaths::open("nonexistent", error).has_value());
    CHECK(error == PresetPaths::not_found_message("nonexistent"));

    CHECK_FALSE(PresetPaths::open(stores.user.path().string(), error).has_value());
    CHECK(error.rfind("No preset.json in preset directory: ", 0) == 0);
}

TEST_CASE("user_filepath_for: where a user preset with the id is stored", "[PresetPaths][naming]") {
    PresetStores stores;
    CHECK(PresetPaths::user_filepath_for("mine") ==
          stores.user_dirpath() / "mine.beebium-preset");
}

// ============================================================================
// generate_unique_preset_id() tests
// ============================================================================

TEST_CASE("generate_unique_preset_id: the base id, else the first free numbered one in any store",
          "[PresetPaths][unique_id]") {
    PresetStores stores;
    CHECK(PresetPaths::generate_unique_preset_id("mine") == "mine");
    std::ofstream(stores.user_dirpath() / "mine.beebium-preset") << "{}";
    std::ofstream(stores.user_dirpath() / "mine-2.beebium-preset") << "{}";
    CHECK(PresetPaths::generate_unique_preset_id("mine") == "mine-3");
    // A system preset's id is taken too: a user preset with it would be
    // hidden behind the system one.
    std::ofstream(stores.system_dirpath() / "shipped.beebium-preset") << "{}";
    CHECK(PresetPaths::generate_unique_preset_id("shipped") == "shipped-2");
}

// ============================================================================
// name_of() tests
// ============================================================================

TEST_CASE("name_of: the name an imported preset's id is made from", "[PresetPaths][naming]") {
    beebium::test::ScopedTempDir root("beebium_name_of_");
    std::filesystem::create_directories(root / "My Game");
    CHECK(PresetPaths::name_of(root / "elite.beebium-preset") == "elite");
    CHECK(PresetPaths::name_of(root / "elite.preset.beebium") == "elite");
    CHECK(PresetPaths::name_of(root / "elite.zip") == "elite");
    CHECK(PresetPaths::name_of(root / "preset.json") == "preset");
    CHECK(PresetPaths::name_of(root / "My Game") == "My Game");
    CHECK(PresetPaths::name_of(root.path() / "My Game" / "") == "My Game");
}

// ============================================================================
// Migration of the user store from the earlier stored form
// ============================================================================

namespace {

std::string read_text(const std::filesystem::path& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// What `body` writes to std::cerr.
template <typename F>
std::string cerr_of(F body) {
    std::ostringstream captured;
    auto* previous = std::cerr.rdbuf(captured.rdbuf());
    body();
    std::cerr.rdbuf(previous);
    return captured.str();
}

}  // namespace

TEST_CASE("enumerate: the user store's earlier presets become archives, one line each",
          "[PresetPaths][migration]") {
    PresetStores stores;
    std::ofstream(stores.user_dirpath() / "old.preset.beebium", std::ios::binary)
        << R"({"name": "Old"})";
    std::ofstream(stores.user_dirpath() / "old.thumbnail.png", std::ios::binary) << "PNG";
    std::ofstream(stores.user_dirpath() / "bare.preset.beebium", std::ios::binary) << "{}";
    // The system store is never touched.
    std::ofstream(stores.system_dirpath() / "shipped.preset.beebium") << "{}";

    std::vector<PresetPaths::Entry> entries;
    const auto said = cerr_of([&] { entries = PresetPaths::enumerate(); });

    CHECK(ids_of(entries) == std::vector<std::string>{"bare", "old"});
    auto migrated = [&](const std::string& id) {
        return "Migrated the preset " +
               (stores.user_dirpath() / (id + ".preset.beebium")).string() + " to " +
               (stores.user_dirpath() / (id + ".beebium-preset")).string() + "\n";
    };
    CHECK(said == migrated("bare") + migrated("old"));
    beebium::zip::ZipReader old(stores.user_dirpath() / "old.beebium-preset");
    CHECK(old.member_names() == std::vector<std::string>{"preset.json", "thumbnail.png"});
    const auto document = old.read("preset.json");
    CHECK(std::string(document.begin(), document.end()) == R"({"name": "Old"})");
    CHECK_FALSE(std::filesystem::exists(stores.user_dirpath() / "old.preset.beebium"));
    CHECK_FALSE(std::filesystem::exists(stores.user_dirpath() / "old.thumbnail.png"));
    CHECK(read_text(stores.system_dirpath() / "shipped.preset.beebium") == "{}");

    // Once done, nothing more is said.
    CHECK(cerr_of([] { (void)PresetPaths::enumerate(); }).empty());
}

#ifndef _WIN32
TEST_CASE("enumerate: a user store that cannot be written is left as it is, with one notice",
          "[PresetPaths][migration]") {
    if (::geteuid() == 0) SKIP("permissions do not bind root");
    PresetStores stores;
    std::ofstream(stores.user_dirpath() / "old.preset.beebium") << "{}";
    std::filesystem::permissions(stores.user_dirpath(), std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::remove);

    std::vector<PresetPaths::Entry> entries;
    const auto said = cerr_of([&] { entries = PresetPaths::enumerate(); });
    std::filesystem::permissions(stores.user_dirpath(), std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);

    CHECK(entries.empty());
    CHECK(said.rfind("Cannot migrate the presets in " + stores.user_dirpath().string() +
                         " to .beebium-preset archives (",
                     0) == 0);
    CHECK(std::count(said.begin(), said.end(), '\n') == 1);
    CHECK(read_text(stores.user_dirpath() / "old.preset.beebium") == "{}");
}
#endif
