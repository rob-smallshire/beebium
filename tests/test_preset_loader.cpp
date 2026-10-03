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

// test_preset_loader.cpp
// Tests for preset file loading and parsing (PresetLoader.hpp)

#include <beebium/server/PresetLoader.hpp>
#include <catch2/catch_test_macros.hpp>
#include "test_temp_paths.hpp"

#include <filesystem>

using namespace beebium::server;

// Test assets directory is defined via compile definition
#ifndef BEEBIUM_TEST_ASSETS_DIR
#error "BEEBIUM_TEST_ASSETS_DIR must be defined"
#endif

namespace {
    std::filesystem::path presets_dirpath() {
        return std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) / "presets";
    }
}

// ============================================================================
// normalize_image_uri() tests
// ============================================================================

TEST_CASE("normalize_image_uri: file:// URI unchanged", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("file:///path/to/game.ssd", "/some/dir");
    REQUIRE(result == "file:///path/to/game.ssd");
}

TEST_CASE("normalize_image_uri: library:// URI unchanged", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("library://games/Elite.ssd", "/some/dir");
    REQUIRE(result == "library://games/Elite.ssd");
}

TEST_CASE("normalize_image_uri: http:// URI unchanged", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("http://example.com/game.ssd", "/some/dir");
    REQUIRE(result == "http://example.com/game.ssd");
}

TEST_CASE("normalize_image_uri: absolute path to file:// URI", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("/path/to/game.ssd", "/some/dir");
    REQUIRE(result == "file:///path/to/game.ssd");
}

TEST_CASE("normalize_image_uri: relative path resolved from base dir", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("game.ssd", "/home/user/presets");
    REQUIRE(result == "file:///home/user/presets/game.ssd");
}

TEST_CASE("normalize_image_uri: relative path with subdirectory", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("discs/game.ssd", "/home/user/presets");
    REQUIRE(result == "file:///home/user/presets/discs/game.ssd");
}

TEST_CASE("normalize_image_uri: relative path with parent directory", "[preset][normalize_image_uri]") {
    auto result = normalize_image_uri("../discs/game.ssd", "/home/user/presets");
    // The filesystem path resolution should normalize the ..
    REQUIRE(result == "file:///home/user/discs/game.ssd");
}

// ============================================================================
// load_preset() tests - successful loading
// ============================================================================

TEST_CASE("load_preset: valid minimal preset", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "minimal.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config.has_value());
    REQUIRE(result.config->name == "Minimal Preset");
    REQUIRE_FALSE(result.config->model.has_value());
    REQUIRE_FALSE(result.config->storage.has_value());
}

TEST_CASE("load_preset: valid storage section", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "storage.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config.has_value());
    REQUIRE(result.config->name == "Storage Test Preset");
    REQUIRE(result.config->model == "model-b");

    REQUIRE(result.config->storage.has_value());
    const auto& storage = *result.config->storage;

    // FDC socket
    REQUIRE(storage.fdc_socket_id == "acorn-1770");
    REQUIRE(storage.fdc_socket_mode == "1770");

    // Floppy drives
    REQUIRE(storage.floppy_drives.size() == 2);
    REQUIRE(storage.floppy_drives.at(0).has_value());
    REQUIRE(*storage.floppy_drives.at(0) == "file:///tmp/test.ssd");
    REQUIRE_FALSE(storage.floppy_drives.at(1).has_value());  // Explicit null

    // Cassette
    REQUIRE(storage.cassette_image_uri == "file:///tmp/test.uef");
}

TEST_CASE("load_preset: relative paths are resolved", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "relative_paths.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config->storage.has_value());

    const auto& storage = *result.config->storage;
    REQUIRE(storage.floppy_drives.size() == 2);

    // Drive 0: relative path "game.ssd"
    REQUIRE(storage.floppy_drives.at(0).has_value());
    std::string drive0_uri = *storage.floppy_drives.at(0);
    REQUIRE(drive0_uri.find("file://") == 0);
    REQUIRE(drive0_uri.find("presets/game.ssd") != std::string::npos);

    // Drive 1: relative path with parent directory
    REQUIRE(storage.floppy_drives.at(1).has_value());
    std::string drive1_uri = *storage.floppy_drives.at(1);
    REQUIRE(drive1_uri.find("file://") == 0);
    // The .. should be resolved
    REQUIRE(drive1_uri.find("discs/other.ssd") != std::string::npos);
}

TEST_CASE("load_preset: unknown keys are ignored (forward compatibility)", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "unknown_keys.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config->name == "Unknown Keys Preset");
    REQUIRE(result.config->model == "model-b");
    REQUIRE(result.config->storage.has_value());
    REQUIRE(result.config->storage->fdc_socket_id == "acorn-1770");
}

TEST_CASE("load_preset: name falls back to filename stem", "[preset][load_preset]") {
    // Create a test preset without a name field
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "no_name.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"model": "model-b"})";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->name == "no_name.preset");  // Stem of filename

    std::filesystem::remove(temp_filepath);
}

// ============================================================================
// load_preset() tests - error handling
// ============================================================================

TEST_CASE("load_preset: missing file returns error", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "nonexistent.preset.beebium");

    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("Cannot open") != std::string::npos);
}

TEST_CASE("load_preset: invalid JSON returns error", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "invalid.json");

    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("Invalid JSON") != std::string::npos);
    REQUIRE(result.error.find("parse error") != std::string::npos);
}

TEST_CASE("load_preset: non-object JSON returns error", "[preset][load_preset]") {
    auto result = load_preset(presets_dirpath() / "not_object.json");

    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("must contain a JSON object") != std::string::npos);
}

// ============================================================================
// PresetLoadResult tests
// ============================================================================

TEST_CASE("PresetLoadResult: success() matches config presence", "[preset][PresetLoadResult]") {
    PresetLoadResult success_result{PresetConfig{"Test", std::nullopt, std::nullopt, std::nullopt, std::nullopt}, ""};
    REQUIRE(success_result.success());
    REQUIRE(static_cast<bool>(success_result));

    PresetLoadResult error_result{std::nullopt, "Error message"};
    REQUIRE_FALSE(error_result.success());
    REQUIRE_FALSE(static_cast<bool>(error_result));
}

// ============================================================================
// PresetStorageConfig tests
// ============================================================================

TEST_CASE("PresetStorageConfig: default state", "[preset][PresetStorageConfig]") {
    PresetStorageConfig storage;

    REQUIRE_FALSE(storage.fdc_socket_id.has_value());
    REQUIRE_FALSE(storage.fdc_socket_mode.has_value());
    REQUIRE(storage.floppy_drives.empty());
    REQUIRE_FALSE(storage.cassette_image_uri.has_value());
}

// ============================================================================
// PresetConfig tests
// ============================================================================

TEST_CASE("PresetConfig: default state", "[preset][PresetConfig]") {
    PresetConfig config;

    REQUIRE(config.name.empty());
    REQUIRE_FALSE(config.model.has_value());
    REQUIRE_FALSE(config.storage.has_value());
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("load_preset: empty storage section", "[preset][load_preset]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "empty_storage.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "Empty Storage", "storage": {}})";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->storage.has_value());
    REQUIRE_FALSE(result.config->storage->fdc_socket_id.has_value());
    REQUIRE(result.config->storage->floppy_drives.empty());

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: fdc_socket without mode", "[preset][load_preset]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "fdc_no_mode.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "FDC No Mode", "storage": {"fdc_socket": {"id": "acorn-1770"}}})";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->storage->fdc_socket_id == "acorn-1770");
    REQUIRE_FALSE(result.config->storage->fdc_socket_mode.has_value());

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: floppy_drives with only drive 1", "[preset][load_preset]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "drive1_only.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "Drive 1 Only", "storage": {"floppy_drives": [{"drive": 1, "image_uri": "file:///test.ssd"}]}})";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->storage->floppy_drives.size() == 1);
    REQUIRE(result.config->storage->floppy_drives.count(0) == 0);
    REQUIRE(result.config->storage->floppy_drives.count(1) == 1);
    REQUIRE(*result.config->storage->floppy_drives.at(1) == "file:///test.ssd");

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: fdc_socket id 'none'", "[preset][load_preset]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "fdc_none.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "No FDC", "storage": {"fdc_socket": {"id": "none"}}})";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->storage->fdc_socket_id == "none");

    std::filesystem::remove(temp_filepath);
}

// ============================================================================
// Econet section tests
// ============================================================================

TEST_CASE("load_preset: econet station only", "[preset][load_preset][econet]") {
    auto result = load_preset(presets_dirpath() / "econet_station_only.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config->econet.has_value());
    REQUIRE(result.config->econet->station == 42);
    REQUIRE_FALSE(result.config->econet->transport.has_value());
}

TEST_CASE("load_preset: econet with AUN transport", "[preset][load_preset][econet]") {
    auto result = load_preset(presets_dirpath() / "econet_with_port.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config->econet.has_value());
    REQUIRE(result.config->econet->station == 5);
    REQUIRE(result.config->econet->transport.has_value());
    REQUIRE(result.config->econet->transport->name == "aun");
    REQUIRE(result.config->econet->transport->parameters.at("port") == "12345");
}

TEST_CASE("load_preset: econet AUN with port=none disables network",
          "[preset][load_preset][econet]") {
    auto result = load_preset(presets_dirpath() / "econet_no_network.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config->econet.has_value());
    REQUIRE(result.config->econet->station == 10);
    REQUIRE(result.config->econet->transport.has_value());
    REQUIRE(result.config->econet->transport->name == "aun");
    REQUIRE(result.config->econet->transport->parameters.at("port") == "none");
}

TEST_CASE("load_preset: AUN map as JSON array lands in list_parameters",
          "[preset][load_preset][econet]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "econet_aun_map_array.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({
            "name": "AUN multi-peer",
            "econet": {
                "station": 32,
                "transport": {
                    "name": "aun",
                    "parameters": {
                        "port": "32768",
                        "map": [
                            "0.254@127.0.0.1@32769",
                            "0.253@127.0.0.1@32770"
                        ]
                    }
                }
            }
        })";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE(result.success());
    const auto& transport = *result.config->econet->transport;
    CHECK(transport.parameters.at("port") == "32768");
    CHECK(transport.parameters.count("map") == 0);
    REQUIRE(transport.list_parameters.at("map") == std::vector<std::string>{
        "0.254@127.0.0.1@32769", "0.253@127.0.0.1@32770"});

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: AUN map as single string stays as scalar parameter",
          "[preset][load_preset][econet]") {
    // Backwards-compatible single-peer preset form. Downstream
    // normalisation (in ServerMain) will move this into list_config
    // once the schema is known.
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "econet_aun_map_string.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({
            "name": "AUN single-peer",
            "econet": {
                "station": 32,
                "transport": {
                    "name": "aun",
                    "parameters": {
                        "port": "32768",
                        "map": "0.254@127.0.0.1@32769"
                    }
                }
            }
        })";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE(result.success());
    const auto& transport = *result.config->econet->transport;
    CHECK(transport.parameters.at("map") == "0.254@127.0.0.1@32769");
    CHECK(transport.list_parameters.count("map") == 0);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: econet with piconet via transport object",
          "[preset][load_preset][econet][piconet]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "econet_piconet.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({
            "name": "Econet via Piconet",
            "econet": {
                "station": 32,
                "transport": {
                    "name": "piconet",
                    "parameters": { "device_path": "/dev/tty.usbmodem101" }
                }
            }
        })";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE(result.success());
    REQUIRE(result.config->econet.has_value());
    CHECK(result.config->econet->station == 32);
    REQUIRE(result.config->econet->transport.has_value());
    CHECK(result.config->econet->transport->name == "piconet");
    CHECK(result.config->econet->transport->parameters.at("device_path")
          == "/dev/tty.usbmodem101");

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: legacy piconet object is rejected with migration hint",
          "[preset][load_preset][econet][piconet]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "econet_legacy_piconet.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({
            "name": "Legacy Piconet",
            "econet": {
                "station": 32,
                "piconet": { "device_path": "/dev/tty.usbmodem101" }
            }
        })";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE_FALSE(result.success());
    CHECK(result.error.find("piconet") != std::string::npos);
    CHECK(result.error.find("transport") != std::string::npos);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: legacy aun_port is rejected with migration hint",
          "[preset][load_preset][econet]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "econet_legacy_aun_port.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({
            "name": "Legacy AUN port",
            "econet": { "station": 32, "aun_port": 32768 }
        })";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE_FALSE(result.success());
    CHECK(result.error.find("aun_port") != std::string::npos);
    CHECK(result.error.find("transport") != std::string::npos);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: econet with storage", "[preset][load_preset][econet]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "econet_storage.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({
            "name": "Econet and Storage",
            "econet": { "station": 100 },
            "storage": { "fdc_socket": { "id": "acorn-1770" } }
        })";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->econet.has_value());
    REQUIRE(result.config->econet->station == 100);
    REQUIRE(result.config->storage.has_value());
    REQUIRE(result.config->storage->fdc_socket_id == "acorn-1770");

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: unknown econet keys ignored", "[preset][load_preset][econet]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "econet_unknown.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "Unknown Keys", "econet": { "station": 1, "future_field": true }})";
    }

    auto result = load_preset(temp_filepath);

    REQUIRE(result.success());
    REQUIRE(result.config->econet.has_value());
    REQUIRE(result.config->econet->station == 1);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: econet station 0 is invalid", "[preset][load_preset][econet]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "econet_bad0.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "Bad Station", "econet": { "station": 0 }})";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("between 1 and 254") != std::string::npos);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: econet station 255 is invalid", "[preset][load_preset][econet]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "econet_bad255.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "Bad Station", "econet": { "station": 255 }})";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("between 1 and 254") != std::string::npos);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: econet station 300 is invalid", "[preset][load_preset][econet]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "econet_bad300.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "Bad Station", "econet": { "station": 300 }})";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("between 1 and 254") != std::string::npos);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: econet missing station", "[preset][load_preset][econet]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "econet_nostation.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"name": "No Station", "econet": { "aun_port": 12345 }})";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("requires a 'station' field") != std::string::npos);

    std::filesystem::remove(temp_filepath);
}

// ============================================================================
// PresetEconetConfig tests
// ============================================================================

TEST_CASE("PresetEconetConfig: default state", "[preset][PresetEconetConfig]") {
    PresetEconetConfig econet{};

    REQUIRE(econet.station == 0);
    REQUIRE_FALSE(econet.transport.has_value());
}

TEST_CASE("PresetConfig: econet default state", "[preset][PresetConfig]") {
    PresetConfig config;

    REQUIRE_FALSE(config.econet.has_value());
}

// ============================================================================
// load_preset() tests - sideways_bank section
// ============================================================================

TEST_CASE("load_preset: valid sideways_bank section", "[preset][load_preset][sideways]") {
    auto result = load_preset(presets_dirpath() / "sideways.preset.beebium");

    REQUIRE(result.success());
    REQUIRE(result.config->model == "model-b");
    REQUIRE(result.config->sideways.size() == 4);

    // Slot 14: ROM with image (stored verbatim, resolved via ROM search path).
    REQUIRE(result.config->sideways[0].slot == 14);
    REQUIRE(result.config->sideways[0].type == SidewaysSlotType::Rom);
    REQUIRE(result.config->sideways[0].image_filepath == "acorn-dfs_2_26.rom");

    // Slot 13: empty.
    REQUIRE(result.config->sideways[1].slot == 13);
    REQUIRE(result.config->sideways[1].type == SidewaysSlotType::Empty);
    REQUIRE(result.config->sideways[1].image_filepath.empty());

    // Slot 4: RAM with no preload image.
    REQUIRE(result.config->sideways[2].slot == 4);
    REQUIRE(result.config->sideways[2].type == SidewaysSlotType::Ram);
    REQUIRE(result.config->sideways[2].image_filepath.empty());

    // Slot 5: RAM with a preload image.
    REQUIRE(result.config->sideways[3].slot == 5);
    REQUIRE(result.config->sideways[3].type == SidewaysSlotType::Ram);
    REQUIRE(result.config->sideways[3].image_filepath == "preload.rom");
}

TEST_CASE("load_preset: sideways ROM image is not URI-normalized", "[preset][load_preset][sideways]") {
    // Sideways ROM references are ROM-library names resolved via the ROM
    // search path (like --sideways and DEFAULT_DFS_ROM), not disc-image
    // paths, so they must NOT be rewritten to file:// URIs.
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "sw_verbatim.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"model": "model-b", "sideways_bank": { "slots": [ { "slot": 14, "type": "rom", "image_uri": "acorn-dfs_2_26.rom" } ] }})";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE(result.success());
    REQUIRE(result.config->sideways.size() == 1);
    REQUIRE(result.config->sideways[0].image_filepath == "acorn-dfs_2_26.rom");

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: sideways_bank present but no slots is empty", "[preset][load_preset][sideways]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "sw_noslots.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"model": "model-b", "sideways_bank": {}})";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE(result.success());
    REQUIRE(result.config->sideways.empty());

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: sideways rom without image_uri is invalid", "[preset][load_preset][sideways]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "sw_rom_noimage.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"model": "model-b", "sideways_bank": { "slots": [ { "slot": 14, "type": "rom" } ] }})";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("requires an 'image_uri'") != std::string::npos);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: sideways empty with image_uri is invalid", "[preset][load_preset][sideways]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "sw_empty_image.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"model": "model-b", "sideways_bank": { "slots": [ { "slot": 13, "type": "empty", "image_uri": "x.rom" } ] }})";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("cannot have an 'image_uri'") != std::string::npos);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: sideways invalid type is rejected", "[preset][load_preset][sideways]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "sw_badtype.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"model": "model-b", "sideways_bank": { "slots": [ { "slot": 14, "type": "flash", "image_uri": "x.rom" } ] }})";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("expected one of: rom, ram, empty") != std::string::npos);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: sideways slot out of range is rejected", "[preset][load_preset][sideways]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "sw_badslot.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"model": "model-b", "sideways_bank": { "slots": [ { "slot": 16, "type": "rom", "image_uri": "x.rom" } ] }})";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("between 0 and 15") != std::string::npos);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("load_preset: sideways slot entry missing slot is rejected", "[preset][load_preset][sideways]") {
    beebium::test::ScopedTempDir temp_dir("beebium_preset_loader_");
    std::filesystem::path temp_filepath = temp_dir / "sw_noslotfield.preset.beebium";
    {
        std::ofstream file(temp_filepath);
        file << R"({"model": "model-b", "sideways_bank": { "slots": [ { "type": "rom", "image_uri": "x.rom" } ] }})";
    }

    auto result = load_preset(temp_filepath);
    REQUIRE_FALSE(result.success());
    REQUIRE(result.error.find("requires an integer 'slot'") != std::string::npos);

    std::filesystem::remove(temp_filepath);
}

TEST_CASE("PresetConfig: sideways default state", "[preset][PresetConfig][sideways]") {
    PresetConfig config;

    REQUIRE(config.sideways.empty());
}
