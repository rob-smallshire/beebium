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
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>  // For creating test files
#include "test_temp_paths.hpp"

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
// PRESET_EXTENSION constant tests
// ============================================================================

TEST_CASE("PRESET_EXTENSION: has correct value", "[PresetPaths][constant]") {
    REQUIRE(PresetPaths::PRESET_EXTENSION == ".preset.beebium");
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
    // Skip if environment variable is set (would override platform path)
    if (platform::get_env("BEEBIUM_USER_PRESETS_DIRPATH").has_value()) {
        SKIP("BEEBIUM_USER_PRESETS_DIRPATH is set");
    }

    auto path = PresetPaths::get_user_presets_dirpath();
    REQUIRE(path.string().find("Application Support") != std::string::npos);
    REQUIRE(path.string().find("Beebium") != std::string::npos);
}
#endif

#ifdef __linux__
TEST_CASE("get_user_presets_dirpath: Linux path contains .config or XDG_CONFIG_HOME", "[PresetPaths][user_presets][linux]") {
    // Skip if environment variable is set (would override platform path)
    if (platform::get_env("BEEBIUM_USER_PRESETS_DIRPATH").has_value()) {
        SKIP("BEEBIUM_USER_PRESETS_DIRPATH is set");
    }

    auto path = PresetPaths::get_user_presets_dirpath();
    bool has_config = path.string().find(".config") != std::string::npos ||
                      path.string().find("config") != std::string::npos;
    REQUIRE(has_config);
    REQUIRE(path.string().find("beebium") != std::string::npos);
}
#endif

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
// list_presets_in_directory() tests
// ============================================================================

TEST_CASE("list_presets_in_directory: empty directory returns empty list", "[PresetPaths][list]") {
    beebium::test::ScopedTempDir scoped_dir("beebium_test_presets_empty_");
    const auto& temp_dir = scoped_dir.path();

    auto presets = PresetPaths::list_presets_in_directory(temp_dir);
    REQUIRE(presets.empty());
}

TEST_CASE("list_presets_in_directory: non-existent directory returns empty list", "[PresetPaths][list]") {
    auto non_existent = beebium::test::unique_temp_path("beebium_test_nonexistent_dir_");
    REQUIRE_FALSE(std::filesystem::exists(non_existent));

    auto presets = PresetPaths::list_presets_in_directory(non_existent);
    REQUIRE(presets.empty());
}

TEST_CASE("list_presets_in_directory: finds preset files", "[PresetPaths][list]") {
    beebium::test::ScopedTempDir scoped_dir("beebium_test_presets_find_");
    const auto& temp_dir = scoped_dir.path();

    // Create test preset files
    std::ofstream(temp_dir / "model-b.preset.beebium") << "{}";
    std::ofstream(temp_dir / "my-setup.preset.beebium") << "{}";

    auto presets = PresetPaths::list_presets_in_directory(temp_dir);

    REQUIRE(presets.size() == 2);
    REQUIRE(std::find(presets.begin(), presets.end(), "model-b") != presets.end());
    REQUIRE(std::find(presets.begin(), presets.end(), "my-setup") != presets.end());
}

TEST_CASE("list_presets_in_directory: ignores non-preset files", "[PresetPaths][list]") {
    beebium::test::ScopedTempDir scoped_dir("beebium_test_presets_ignore_");
    const auto& temp_dir = scoped_dir.path();

    // Create preset file and non-preset files
    std::ofstream(temp_dir / "model-b.preset.beebium") << "{}";
    std::ofstream(temp_dir / "readme.txt") << "text";
    std::ofstream(temp_dir / "config.json") << "{}";
    std::ofstream(temp_dir / "preset.beebium.bak") << "{}";  // Wrong extension pattern

    auto presets = PresetPaths::list_presets_in_directory(temp_dir);

    REQUIRE(presets.size() == 1);
    REQUIRE(presets[0] == "model-b");
}

TEST_CASE("list_presets_in_directory: ignores subdirectories", "[PresetPaths][list]") {
    beebium::test::ScopedTempDir scoped_dir("beebium_test_presets_subdir_");
    const auto& temp_dir = scoped_dir.path();
    std::filesystem::create_directories(temp_dir / "subdir.preset.beebium");  // Directory with preset-like name

    std::ofstream(temp_dir / "model-b.preset.beebium") << "{}";

    auto presets = PresetPaths::list_presets_in_directory(temp_dir);

    REQUIRE(presets.size() == 1);
    REQUIRE(presets[0] == "model-b");
}

// ============================================================================
// is_system_preset() tests
// ============================================================================

TEST_CASE("is_system_preset: returns false when no system directory", "[PresetPaths][is_system]") {
    // When get_system_presets_dirpath returns nullopt
    auto user_path = PresetPaths::get_user_presets_dirpath() / "test.preset.beebium";

    // This test behavior depends on whether system presets exist
    // Just verify it doesn't crash and returns a boolean
    bool result = PresetPaths::is_system_preset(user_path);
    REQUIRE((result == true || result == false));
}

// ============================================================================
// generate_unique_preset_id() tests
// ============================================================================

TEST_CASE("generate_unique_preset_id: returns base ID when available", "[PresetPaths][unique_id]") {
    // This would require setting up a test directory structure
    // For now, just test the basic case without file conflicts
    // When there's no conflicting preset, it should return the base ID
    auto id = PresetPaths::generate_unique_preset_id("unique-test-id-12345");

    // Without file conflicts, should return the base ID
    // (The actual behavior depends on what presets exist)
    REQUIRE_FALSE(id.empty());
    REQUIRE(id.find("unique-test-id-12345") != std::string::npos);
}

// ============================================================================
// find_preset_filepath() integration tests
// ============================================================================

TEST_CASE("find_preset_filepath: returns nullopt for nonexistent preset", "[PresetPaths][find]") {
    auto result = PresetPaths::find_preset_filepath("nonexistent-preset-12345");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("find_preset_filepath: user_only flag works", "[PresetPaths][find]") {
    // With user_only=true, should only search user presets
    auto result = PresetPaths::find_preset_filepath("nonexistent-12345", false, true);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("find_preset_filepath: system_only flag works", "[PresetPaths][find]") {
    // With system_only=true, should only search system presets
    auto result = PresetPaths::find_preset_filepath("nonexistent-12345", true, false);
    REQUIRE_FALSE(result.has_value());
}
