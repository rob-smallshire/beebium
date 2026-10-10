// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Test PluginLoader: manifest scanning, opt-in loading, and extension registration.

#include <catch2/catch_test_macros.hpp>

#include <beebium/extension/PeripheralExtension.hpp>
#include <beebium/extension/PluginLoader.hpp>
#include <beebium/extension/ExtensionAbi.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/ExtensionRegistry.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>

#include "test_temp_paths.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace {

// The plugin build directory is passed via compile definition
#ifdef BEEBIUM_PLUGIN_DIR
    const std::filesystem::path kPluginDirpath = BEEBIUM_PLUGIN_DIR;
#else
    const std::filesystem::path kPluginDirpath;
#endif

// The parent extensions/ deploy directory (holds every deployed plugin), for the
// manifest/code drift guard.
#ifdef BEEBIUM_EXTENSIONS_DIR
    const std::filesystem::path kExtensionsDirpath = BEEBIUM_EXTENSIONS_DIR;
#else
    const std::filesystem::path kExtensionsDirpath;
#endif

}  // namespace

TEST_CASE("PluginLoader scan_manifests finds manifests in directory",
          "[extension][plugin]") {
    if (kPluginDirpath.empty()) {
        SKIP("BEEBIUM_PLUGIN_DIR not set");
    }

    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(kPluginDirpath);

    REQUIRE_FALSE(manifests.empty());

    auto* manifest = beebium::PluginLoader::find_manifest(manifests, "test-scratch-ram");
    REQUIRE(manifest != nullptr);
    REQUIRE(manifest->name == "test-scratch-ram");
    REQUIRE(manifest->library_stem == "test-scratch-ram");
    REQUIRE_FALSE(manifest->description.empty());
    // display_name is parsed from the manifest and used as the per-instance
    // label default by Extension::label() when no explicit label is given.
    REQUIRE(manifest->display_name == "Test Scratch RAM");
    // Manifests without an explicit extension_kind default to "peripheral".
    REQUIRE(manifest->extension_kind == "peripheral");
}

TEST_CASE("PluginLoader scan_manifests returns empty for non-existent directory",
          "[extension][plugin]") {
    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests("/non/existent/path");
    REQUIRE(manifests.empty());
}

TEST_CASE("PluginLoader scan_manifests with MissingDirPolicy::Throw "
          "throws on non-existent directory",
          "[extension][plugin]") {
    beebium::PluginLoader loader;
    REQUIRE_THROWS_AS(
        loader.scan_manifests("/non/existent/path",
                              beebium::PluginLoader::MissingDirPolicy::Throw),
        std::runtime_error);
}

TEST_CASE("PluginLoader scan_manifests with MissingDirPolicy::Throw "
          "succeeds for existing empty directory",
          "[extension][plugin]") {
    beebium::test::ScopedTempDir tmp_dir("beebium_plugin_loader_empty_dir_");

    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(
        tmp_dir.path(), beebium::PluginLoader::MissingDirPolicy::Throw);
    REQUIRE(manifests.empty());
}

TEST_CASE("PluginLoader find_manifest returns nullptr for unknown name",
          "[extension][plugin]") {
    std::vector<beebium::ExtensionManifest> manifests;
    manifests.push_back({
        .name = "test-scratch-ram",
        .description = "desc",
        .library_stem = "test-scratch-ram",
    });

    REQUIRE(beebium::PluginLoader::find_manifest(manifests, "unknown") == nullptr);
}

TEST_CASE("PluginLoader load_extension loads and registers extension",
          "[extension][plugin]") {
    if (kPluginDirpath.empty()) {
        SKIP("BEEBIUM_PLUGIN_DIR not set");
    }

    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(kPluginDirpath);

    auto* manifest = beebium::PluginLoader::find_manifest(manifests, "test-scratch-ram");
    REQUIRE(manifest != nullptr);

    beebium::ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");

    auto loaded = loader.load_extension(*manifest);
    auto* peripheral = dynamic_cast<beebium::PeripheralExtension*>(loaded.get());
    REQUIRE(peripheral != nullptr);
    (void)loaded.release();
    registry.register_extension(std::unique_ptr<beebium::PeripheralExtension>(peripheral));
    REQUIRE(registry.extension_count() == 1);

    beebium::OneMHzBusPort port;
    beebium::ExtensionContext ctx(&port);
    registry.resolve_and_init(ctx);

    REQUIRE(registry.init_count() == 1);
    REQUIRE(registry.extensions()[0]->name() == "test-scratch-ram");

    // Verify the extension is functional: write and read via the bus port.
    // TestScratchRam occupies 0x80-0x87 (Test Hardware range per Acorn App Note 003).
    port.write(0x80, 0xAB);
    REQUIRE(port.read(0x80) == 0xAB);
    REQUIRE(port.read(0x88) == 0xFF);  // unclaimed

    registry.shutdown();
}

TEST_CASE("PluginLoader loaded extension provides an RPC dispatcher",
          "[extension][plugin]") {
    if (kPluginDirpath.empty()) {
        SKIP("BEEBIUM_PLUGIN_DIR not set");
    }

    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(kPluginDirpath);

    auto* manifest = beebium::PluginLoader::find_manifest(manifests, "test-scratch-ram");
    REQUIRE(manifest != nullptr);

    beebium::ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");
    auto loaded = loader.load_extension(*manifest);
    auto* peripheral = dynamic_cast<beebium::PeripheralExtension*>(loaded.get());
    REQUIRE(peripheral != nullptr);
    (void)loaded.release();
    registry.register_extension(std::unique_ptr<beebium::PeripheralExtension>(peripheral));

    beebium::OneMHzBusPort port;
    beebium::ExtensionContext ctx(&port);
    registry.resolve_and_init(ctx);

    // The extension's API is served through the core's ExtensionRpc channel,
    // so it contributes an ExtensionRpcDispatcher (not a hosted gRPC service).
    bool any_dispatcher = false;
    for (auto* ext : registry.extensions()) {
        if (!ext->rpc_dispatchers().empty()) {
            any_dispatcher = true;
        }
    }
    REQUIRE(any_dispatcher);

    registry.shutdown();
}

// Drift guard: every deployed plugin manifest's attaches_to must match the
// extension's own attaches_to() (the code), as a set. This catches a manifest.json
// that names the wrong attachment point, or one left stale when the code changes.
TEST_CASE("plugin manifest attaches_to matches the extension code",
          "[extension][plugin][attachment-points]") {
    if (kExtensionsDirpath.empty()) {
        SKIP("BEEBIUM_EXTENSIONS_DIR not set");
    }

    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(kExtensionsDirpath);
    REQUIRE_FALSE(manifests.empty());

    int peripherals_checked = 0;
    for (const auto& manifest : manifests) {
        // Only peripheral extensions carry attaches_to(); econet-transports use a
        // different mechanism (extension_kind).
        if (manifest.extension_kind != "peripheral") continue;

        auto loaded = loader.load_extension(manifest);
        auto* peripheral = dynamic_cast<beebium::PeripheralExtension*>(loaded.get());
        REQUIRE(peripheral != nullptr);

        std::set<std::string> from_manifest(manifest.attaches_to.begin(),
                                            manifest.attaches_to.end());
        std::set<std::string> from_code;
        for (auto point : peripheral->attaches_to()) from_code.emplace(point);

        INFO("extension: " << manifest.name);
        CHECK(from_manifest == from_code);
        ++peripherals_checked;
    }
    CHECK(peripherals_checked > 0);
}

// ---------------------------------------------------------------------------
// Manifest `roms` parsing and the load-time firmware presence check (Step 1e)
// ---------------------------------------------------------------------------

namespace {

// Write a manifest.json with the given body into a fresh temp directory and
// return that directory. Caller removes it.
std::filesystem::path write_manifest(const std::string& json) {
    auto dir = beebium::test::unique_temp_path("beebium-manifest-roms-");
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / "manifest.json");
    f << json;
    return dir;
}

}  // namespace

TEST_CASE("Manifest roms: a well-formed array is parsed", "[extension][plugin][rom]") {
    auto dir = write_manifest(R"({
        "name": "cop", "library": "cop",
        "roms": [
            {"key": "client", "filename": "fw.rom", "size": 2048, "display_name": "d"}
        ]
    })");
    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(dir);
    REQUIRE(manifests.size() == 1);
    REQUIRE(manifests[0].roms.size() == 1);
    CHECK(manifests[0].roms[0].key == "client");
    CHECK(manifests[0].roms[0].filename == "fw.rom");
    CHECK(manifests[0].roms[0].size == 2048);
    CHECK(manifests[0].roms[0].display_name == "d");
    std::filesystem::remove_all(dir);
}

TEST_CASE("Manifest choices: a choice may describe itself", "[extension][plugin][choices]") {
    auto dir = write_manifest(R"({
        "name": "c", "library": "c",
        "parameters": [{"key": "mode", "type": "string", "choices": [
            "bare",
            {"value": "labelled", "label": "Labelled"},
            {"value": "described", "label": "Described", "description": "Says what it is."}
        ]}]
    })");
    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(dir);
    std::filesystem::remove_all(dir);
    REQUIRE(manifests.size() == 1);
    const auto& choices = manifests[0].parameters.at(0).choices;
    REQUIRE(choices.size() == 3);
    CHECK(choices[0].label == "bare");
    CHECK(choices[0].description.empty());
    CHECK(choices[1].description.empty());
    CHECK(choices[2].label == "Described");
    CHECK(choices[2].description == "Says what it is.");
}

TEST_CASE("Manifest roms: absent means an empty list", "[extension][plugin][rom]") {
    auto dir = write_manifest(R"({"name": "cop", "library": "cop"})");
    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(dir);
    REQUIRE(manifests.size() == 1);
    CHECK(manifests[0].roms.empty());
    std::filesystem::remove_all(dir);
}

TEST_CASE("Manifest roms: malformed entries are skipped, not fatal", "[extension][plugin][rom]") {
    // A non-array `roms`, and an array with an entry missing filename: the
    // manifest still parses, dropping the bad entries.
    auto non_array = write_manifest(R"({"name": "a", "library": "a", "roms": "nope"})");
    auto bad_entry = write_manifest(R"({
        "name": "b", "library": "b",
        "roms": [{"key": "x"}, 42, {"filename": "y.rom"},
                 {"key": "ok", "filename": "ok.rom", "size": 16}]
    })");
    beebium::PluginLoader loader;

    auto m1 = loader.scan_manifests(non_array);
    REQUIRE(m1.size() == 1);
    CHECK(m1[0].roms.empty());

    auto m2 = loader.scan_manifests(bad_entry);
    REQUIRE(m2.size() == 1);
    REQUIRE(m2[0].roms.size() == 1);          // only the complete entry survives
    CHECK(m2[0].roms[0].key == "ok");
    CHECK(m2[0].roms[0].size == 16);

    std::filesystem::remove_all(non_array);
    std::filesystem::remove_all(bad_entry);
}

// Manifest `groups`: parameters that are alternatives to one another
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kClockManifest = R"({
    "name": "clock", "library": "clock",
    "parameters": [
        {"key": "time", "type": "string"},
        {"key": "offset", "type": "string"},
        {"key": "layout", "type": "string", "default": "a"}
    ],
    "groups": [
        {"key": "start", "label": "Start", "description": "Where the clock starts.",
         "parameters": ["time", "offset"],
         "none": {"label": "Host time", "description": "The clock follows the host."}}
    ]
})";

const beebium::ParameterSchema& schema_parameter(const beebium::ExtensionManifest& m,
                                                 const std::string& key) {
    for (const auto& p : m.parameters) {
        if (p.key == key) return p;
    }
    FAIL("no parameter " << key);
    throw;
}

// Scan a manifest body and return how many manifests survived.
std::size_t manifests_scanned(const std::string& json) {
    auto dir = write_manifest(json);
    beebium::PluginLoader loader;
    auto count = loader.scan_manifests(dir).size();
    std::filesystem::remove_all(dir);
    return count;
}

}  // namespace

TEST_CASE("Manifest groups: a group is parsed with its none state",
          "[extension][plugin][groups]") {
    auto dir = write_manifest(kClockManifest);
    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(dir);
    std::filesystem::remove_all(dir);
    REQUIRE(manifests.size() == 1);
    REQUIRE(manifests[0].groups.size() == 1);
    const auto& g = manifests[0].groups[0];
    CHECK(g.key == "start");
    CHECK(g.label == "Start");
    CHECK(g.description == "Where the clock starts.");
    CHECK(g.parameters == std::vector<std::string>{"time", "offset"});
    REQUIRE(g.none);
    CHECK(g.none->label == "Host time");
    CHECK(g.none->description == "The clock follows the host.");
}

TEST_CASE("Manifest groups: a group need not name a none state",
          "[extension][plugin][groups]") {
    auto dir = write_manifest(R"({
        "name": "c", "library": "c",
        "parameters": [{"key": "a", "type": "string"}, {"key": "b", "type": "string"}],
        "groups": [{"key": "g", "label": "G", "parameters": ["a", "b"]}]
    })");
    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(dir);
    std::filesystem::remove_all(dir);
    REQUIRE(manifests.size() == 1);
    REQUIRE(manifests[0].groups.size() == 1);
    CHECK_FALSE(manifests[0].groups[0].none);
}

TEST_CASE("Manifest groups: each member of a group excludes the others",
          "[extension][plugin][groups]") {
    auto dir = write_manifest(kClockManifest);
    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(dir);
    std::filesystem::remove_all(dir);
    REQUIRE(manifests.size() == 1);
    const auto& m = manifests[0];
    CHECK(schema_parameter(m, "time").excludes == std::vector<std::string>{"offset"});
    CHECK(schema_parameter(m, "offset").excludes == std::vector<std::string>{"time"});
    CHECK(schema_parameter(m, "layout").excludes.empty());

    auto both = beebium::normalise_extension_config(
        "clock", {{"time", "1985-01-01T0000"}, {"offset", "-1y"}}, {}, m.parameters);
    CHECK_FALSE(both.ok);
    CHECK(both.error.find("cannot be combined") != std::string::npos);
    CHECK(beebium::normalise_extension_config("clock", {{"offset", "-1y"}}, {}, m.parameters).ok);
    CHECK(beebium::normalise_extension_config("clock", {}, {}, m.parameters).ok);
}

TEST_CASE("Manifest groups: a group's excludes are not repeated when also declared",
          "[extension][plugin][groups]") {
    auto dir = write_manifest(R"({
        "name": "c", "library": "c",
        "parameters": [{"key": "a", "type": "string", "excludes": ["b"]},
                       {"key": "b", "type": "string"}],
        "groups": [{"key": "g", "label": "G", "parameters": ["a", "b"]}]
    })");
    beebium::PluginLoader loader;
    auto manifests = loader.scan_manifests(dir);
    std::filesystem::remove_all(dir);
    REQUIRE(manifests.size() == 1);
    CHECK(schema_parameter(manifests[0], "a").excludes == std::vector<std::string>{"b"});
}

TEST_CASE("Manifest groups: a malformed group refuses the manifest",
          "[extension][plugin][groups]") {
    SECTION("a member that is not a parameter") {
        CHECK(manifests_scanned(R"({
            "name": "c", "library": "c",
            "parameters": [{"key": "a", "type": "string"}],
            "groups": [{"key": "g", "label": "G", "parameters": ["a", "nope"]}]
        })") == 0);
    }
    SECTION("a parameter in two groups") {
        CHECK(manifests_scanned(R"({
            "name": "c", "library": "c",
            "parameters": [{"key": "a", "type": "string"}, {"key": "b", "type": "string"},
                           {"key": "d", "type": "string"}],
            "groups": [{"key": "g", "label": "G", "parameters": ["a", "b"]},
                       {"key": "h", "label": "H", "parameters": ["b", "d"]}]
        })") == 0);
    }
    SECTION("a member with a default") {
        CHECK(manifests_scanned(R"({
            "name": "c", "library": "c",
            "parameters": [{"key": "a", "type": "string", "default": "x"},
                           {"key": "b", "type": "string"}],
            "groups": [{"key": "g", "label": "G", "parameters": ["a", "b"]}]
        })") == 0);
    }
    SECTION("a required member") {
        CHECK(manifests_scanned(R"({
            "name": "c", "library": "c",
            "parameters": [{"key": "a", "type": "string", "required": true},
                           {"key": "b", "type": "string"}],
            "groups": [{"key": "g", "label": "G", "parameters": ["a", "b"]}]
        })") == 0);
    }
    SECTION("a group without a key") {
        CHECK(manifests_scanned(R"({
            "name": "c", "library": "c",
            "parameters": [{"key": "a", "type": "string"}, {"key": "b", "type": "string"}],
            "groups": [{"label": "G", "parameters": ["a", "b"]}]
        })") == 0);
    }
    SECTION("two groups with one key") {
        CHECK(manifests_scanned(R"({
            "name": "c", "library": "c",
            "parameters": [{"key": "a", "type": "string"}, {"key": "b", "type": "string"},
                           {"key": "d", "type": "string"}, {"key": "e", "type": "string"}],
            "groups": [{"key": "g", "label": "G", "parameters": ["a", "b"]},
                       {"key": "g", "label": "H", "parameters": ["d", "e"]}]
        })") == 0);
    }
}

TEST_CASE("PluginLoader load_extension fails naming the plugin and path when a "
          "declared ROM is absent", "[extension][plugin][rom]") {
    // The presence check runs before dlopen, so no real library is needed: a
    // manifest declaring a ROM that does not exist beside it must fail the load.
    beebium::ExtensionManifest m;
    m.name = "broken-firmware";
    m.library_stem = "broken-firmware";
    m.manifest_dirpath = beebium::test::unique_temp_path("beebium-broken-firmware-plugin-");
    m.roms.push_back(beebium::RomImage{"client", "absent.rom", 2048, ""});

    beebium::PluginLoader loader;
    try {
        loader.load_extension(m, {}, {});
        FAIL("expected load_extension to throw for a missing declared ROM");
    } catch (const std::exception& e) {
        std::string msg = e.what();
        INFO("message: " << msg);
        CHECK(msg.find("broken-firmware") != std::string::npos);
        CHECK(msg.find("absent.rom") != std::string::npos);
    }
}

TEST_CASE("PluginLoader load_extension fails when a declared ROM is the wrong size",
          "[extension][plugin][rom]") {
    // The size check, like the presence check, runs before dlopen. A manifest
    // declaring a 4096-byte ROM (the 2732 device size) with a present but
    // wrong-size file beside it must fail the load naming the sizes -- there is
    // no half-size or padded acceptance.
    auto dir = beebium::test::unique_temp_path("beebium-wrong-size-rom-plugin-");
    std::filesystem::create_directories(dir / "roms");
    {
        std::ofstream f(dir / "roms" / "fw.rom", std::ios::binary);
        std::vector<char> half(2048, '\xFF');  // half the declared size
        f.write(half.data(), static_cast<std::streamsize>(half.size()));
    }

    beebium::ExtensionManifest m;
    m.name = "wrong-size-firmware";
    m.library_stem = "wrong-size-firmware";
    m.manifest_dirpath = dir;
    m.roms.push_back(beebium::RomImage{"client", "fw.rom", 4096, ""});

    beebium::PluginLoader loader;
    try {
        loader.load_extension(m, {}, {});
        FAIL("expected load_extension to throw for a wrong-size declared ROM");
    } catch (const std::exception& e) {
        std::string msg = e.what();
        INFO("message: " << msg);
        CHECK(msg.find("2048") != std::string::npos);
        CHECK(msg.find("4096") != std::string::npos);
    }
    std::filesystem::remove_all(dir);
}

// ============================================================================
// Extension ABI version (#176): a plugin built for another ABI is refused
// ============================================================================

#ifdef BEEBIUM_ABI_STUB_DIR
namespace {

beebium::ExtensionManifest stub_manifest(const std::string& stem) {
    beebium::ExtensionManifest m;
    m.name = stem;
    m.library_stem = stem;
    m.manifest_dirpath = BEEBIUM_ABI_STUB_DIR;
    return m;
}

}  // namespace

TEST_CASE("PluginLoader refuses a plugin built for another extension ABI",
          "[plugin][abi]") {
    beebium::PluginLoader loader;
    try {
        (void)loader.load_extension(stub_manifest("abi_mismatch_plugin"), {}, {});
        FAIL("the mismatched plugin was loaded");
    } catch (const std::runtime_error& e) {
        const std::string message = e.what();
        INFO(message);
        CHECK(message.find("abi_mismatch_plugin") != std::string::npos);
        CHECK(message.find("ABI version " + std::to_string(beebium::kExtensionAbiVersion + 1)) !=
              std::string::npos);
        CHECK(message.find("version " + std::to_string(beebium::kExtensionAbiVersion)) !=
              std::string::npos);
        CHECK(message.find("rebuild") != std::string::npos);
    }
}

TEST_CASE("PluginLoader refuses a plugin that carries no extension ABI version",
          "[plugin][abi]") {
    beebium::PluginLoader loader;
    try {
        (void)loader.load_extension(stub_manifest("abi_unstamped_plugin"), {}, {});
        FAIL("the unstamped plugin was loaded");
    } catch (const std::runtime_error& e) {
        const std::string message = e.what();
        INFO(message);
        CHECK(message.find("abi_unstamped_plugin") != std::string::npos);
        CHECK(message.find("no extension ABI version") != std::string::npos);
    }
}
#endif
