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

// The preset machinery on its own: PresetResolution.hpp with no server, no
// services and no built-in extensions -- the manifests here are synthetic.

#include <beebium/Machines.hpp>
#include <beebium/server/PresetResolution.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "test_temp_paths.hpp"

#include <fstream>
#include <set>
#include <string_view>

using namespace beebium::server;
using Catch::Matchers::ContainsSubstring;
using MachineType = beebium::ModelB;
using Config = ServerConfig<MachineType>;

namespace {

// A SCSI adapter on the 1 MHz bus, a disc on its bus, a serial device and an
// Econet transport, as a resolver lists them.
const std::vector<beebium::ExtensionManifest>& manifests() {
    static const std::vector<beebium::ExtensionManifest> kManifests = [] {
        beebium::ExtensionManifest adapter;
        adapter.name = "adapter";
        adapter.attaches_to = {"1mhz-bus"};
        adapter.provides = {"scsi"};

        beebium::ExtensionManifest disc;
        disc.name = "disc";
        disc.attaches_to = {"scsi"};
        beebium::ParameterSchema id{"scsi-id", "integer", "Target", -1, false, false, "0"};
        id.min = 0;
        id.max = 7;
        id.unique = true;
        disc.parameters = {id};

        beebium::ExtensionManifest serial;
        serial.name = "serial";
        serial.attaches_to = {"serial-port"};
        serial.parameters = {{"baud", "integer", "Baud", -1, false, false, "19200"}};

        beebium::ExtensionManifest transport;
        transport.name = "net";
        transport.extension_kind = "econet-transport";
        transport.parameters = {{"port", "string", "Port", -1, false, false, "32768"}};

        return std::vector<beebium::ExtensionManifest>{adapter, disc, serial, transport};
    }();
    return kManifests;
}

Config config_with_manifests() {
    Config config;
    config.launch.extension_resolver.add_source("test", manifests());
    return config;
}

PresetConfig load(const beebium::test::ScopedTempDir& dir, const std::string& body) {
    auto filepath = dir / "test.preset.beebium";
    std::ofstream(filepath) << body;
    auto result = load_preset(filepath);
    REQUIRE(result.success());
    return *result.config;
}

}  // namespace

TEST_CASE("fold_in_preset normalises, names and attaches a preset's extensions",
          "[preset-resolution]") {
    beebium::test::ScopedTempDir dir("beebium-preset-resolution-");
    auto config = config_with_manifests();
    apply_preset(config, load(dir, R"({"extensions": [
        {"name": "adapter"}, {"name": "disc", "config": {"scsi-id": 3}},
        {"name": "serial"}]})"));

    auto problems = fold_in_preset(config);

    CHECK(problems.empty());
    REQUIRE(config.machine.extension_instances.size() == 3);
    CHECK(config.machine.extension_instances[0].config.at("id") == "adapter");
    CHECK(config.machine.extension_instances[1].config.at("scsi-id") == "3");
    CHECK(config.machine.extension_instances[2].config.at("baud") == "19200");  // default
    CHECK(config.machine.extension_instances[2].explicit_keys.empty());
}

TEST_CASE("fold_in_preset locates each problem at its preset entry",
          "[preset-resolution]") {
    beebium::test::ScopedTempDir dir("beebium-preset-resolution-");

    auto bad_value = config_with_manifests();
    apply_preset(bad_value, load(dir, R"({"extensions": [
        {"name": "adapter"}, {"name": "disc", "config": {"scsi-id": 9}}]})"));
    auto problems = fold_in_preset(bad_value);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].path == "/extensions/1");
    CHECK_THAT(problems[0].message, ContainsSubstring("must be 0-7"));

    auto no_adapter = config_with_manifests();
    apply_preset(no_adapter, load(dir, R"({"extensions": [{"name": "disc"}]})"));
    problems = fold_in_preset(no_adapter);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].path == "/extensions/0");
    CHECK_THAT(problems[0].message, ContainsSubstring("adapter"));
}

TEST_CASE("fold_in_preset merges the sideways and startup baselines under the CLI",
          "[preset-resolution]") {
    beebium::test::ScopedTempDir dir("beebium-preset-resolution-");
    auto config = config_with_manifests();
    apply_preset(config, load(dir, R"({"startup": {"screen_mode": 3, "auto_boot": true},
        "sideways_bank": {"slots": [{"slot": 14, "type": "empty"}]}})"));
    config.machine.screen_mode = 1;  // as a CLI --screen-mode 1 would have left it
    config.machine.screen_mode_set = true;

    CHECK(fold_in_preset(config).empty());
    CHECK(config.machine.screen_mode == 1);
    CHECK(config.machine.auto_boot);
    CHECK(config.machine.rom_slots.at(14) == EMPTY_SLOT_MARKER);
}

TEST_CASE("preset_model_error names both models", "[preset-resolution]") {
    PresetConfig preset;
    CHECK_FALSE(preset_model_error<MachineType::Memory>(preset).has_value());
    preset.model = "model-b-plus";
    auto error = preset_model_error<MachineType::Memory>(preset);
    REQUIRE(error.has_value());
    CHECK_THAT(*error, ContainsSubstring("model-b-plus"));
    CHECK_THAT(*error, ContainsSubstring("model-b'"));
}

TEST_CASE("apply_rom_defaults gives the model's MOS and language ROM", "[preset-resolution]") {
    Config config;
    apply_rom_defaults(config);
    CHECK(config.machine.mos_filepath == MachineType::Memory::DEFAULT_MOS_ROM);
    CHECK(config.machine.rom_slots.at(MachineType::Memory::DEFAULT_LANGUAGE_SLOT) ==
          MachineType::Memory::DEFAULT_LANGUAGE_ROM);
}

TEST_CASE("the preset JSON helpers", "[preset-resolution]") {
    SidewaysConfig ram{};
    ram.slot = 4;
    ram.type = SidewaysSlotType::Ram;
    CHECK(sideways_slot_json(ram, false) == nlohmann::ordered_json{{"slot", 4}, {"type", "ram"}});
    CHECK(sideways_slot_json(ram, true) ==
          nlohmann::ordered_json{{"slot", 4}, {"type", "ram"}, {"image_uri", nullptr},
                                 {"write_protected", false}});

    Config config;
    config.machine.station_number = 80;
    CHECK(station_json(config.machine) == 80);
    config.machine.station_auto_range = beebium::econet::StationRange{40, 49};
    CHECK(station_json(config.machine) == "auto:40-49");

    const auto& serial = manifests()[2];
    CHECK(extension_config_json(&serial, {{"baud", "9600"}}, {}, false) ==
          nlohmann::ordered_json{{"baud", 9600}});
}

// ---------------------------------------------------------------------------
// preset_document: the one writer, in either mode
// ---------------------------------------------------------------------------

namespace {

std::vector<std::string> keys_of(const nlohmann::ordered_json& doc) {
    std::vector<std::string> keys;
    for (const auto& item : doc.items()) keys.push_back(item.key());
    return keys;
}

// A configuration that sets something in every section.
Config populated_config() {
    auto config = config_with_manifests();
    config.machine.machine_name = "Station {econet-station}";
    config.machine.screen_mode = 3;
    config.machine.screen_mode_set = true;
    config.machine.speed_multiplier_override = 2.0;
    config.machine.mos_filepath = "my-mos.rom";
    config.machine.fdc_type = "acorn-1770";
    config.machine.floppy_filepaths[0] = "file:///discs/game.ssd";
    config.machine.sideways_configs.push_back({14, SidewaysSlotType::Rom, "dfs.rom", false});
    config.machine.rom_slots[14] = "dfs.rom";
    config.machine.station_number = 80;
    Config::ExtensionInstance net{"net", {{"port", "0"}}, {}, false, {"port"}};
    Config::ExtensionInstance serial{"serial", {{"baud", "9600"}}, {}, false, {"baud"}};
    config.machine.extension_instances = {net, serial};
    return config;
}

}  // namespace

TEST_CASE("preset_document writes every key in the one order, Given a subsequence of Every",
          "[preset-resolution][document]") {
    auto config = populated_config();
    PresetDocumentHeader header{"Test", "A test", "1985", 1.5};
    std::string error;

    auto every = preset_document<MachineType>(config.machine, config.launch.extension_resolver, header, DocumentKeys::Every, error);
    auto given = preset_document<MachineType>(config.machine, config.launch.extension_resolver, header, DocumentKeys::Given, error);
    REQUIRE(every.has_value());
    REQUIRE(given.has_value());

    const std::vector<std::string> all_keys{
        "model", "name", "release_date", "description", "machine_name", "speed", "startup",
        "mos_image_uri", "storage", "sideways_bank", "econet", "extensions",
        "thumbnail_capture_delay_seconds"};
    CHECK(keys_of(*every) == all_keys);
    // A fully populated configuration gives every key in both modes.
    CHECK(keys_of(*given) == all_keys);
}

TEST_CASE("preset_document with Given leaves defaults out; with Every fills them in",
          "[preset-resolution][document]") {
    Config config = config_with_manifests();
    apply_rom_defaults(config);
    config.machine.mos_filepath.clear();  // as if no --mos was given
    PresetDocumentHeader header;
    header.name = "Bare";
    std::string error;

    auto given = preset_document<MachineType>(config.machine, config.launch.extension_resolver, header, DocumentKeys::Given, error);
    REQUIRE(given.has_value());
    CHECK(keys_of(*given) == std::vector<std::string>{"model", "name"});

    auto every = preset_document<MachineType>(config.machine, config.launch.extension_resolver, header, DocumentKeys::Every, error);
    REQUIRE(every.has_value());
    CHECK(every->at("speed") == 1.0);
    CHECK(every->at("startup") ==
          nlohmann::ordered_json{{"screen_mode", 7}, {"auto_boot", false}});
    CHECK(every->at("econet").is_null());
    CHECK(every->at("storage").at("floppy_drives").size() == 2);
}

TEST_CASE("preset_document refuses a transport with no station", "[preset-resolution][document]") {
    auto config = config_with_manifests();
    config.machine.extension_instances.push_back({"net", {}, {}, false, {}});
    std::string error;
    PresetDocumentHeader header;
    header.name = "X";
    CHECK_FALSE(preset_document<MachineType>(config.machine, config.launch.extension_resolver, header, DocumentKeys::Given, error).has_value());
    CHECK_THAT(error, ContainsSubstring("requires --station"));
}

TEST_CASE("an extension that fails normalisation keeps the config the preset gave",
          "[preset-resolution]") {
    beebium::test::ScopedTempDir dir("beebium-preset-resolution-");
    auto config = config_with_manifests();
    apply_preset(config, load(dir, R"({"extensions": [
        {"name": "serial", "config": {"baud": "fast"}}]})"));

    REQUIRE(fold_in_preset(config).size() == 1);
    CHECK(config.machine.extension_instances[0].config.at("baud") == "fast");
}

TEST_CASE("the preset key registry names every key the loader accepts, once",
          "[preset-resolution][registry]") {
    std::set<std::string_view> names;
    for (const auto& info : kPresetKeys) {
        CHECK(names.insert(info.name).second);
        CHECK(find_preset_key(info.name) == &info);
    }
    CHECK(find_preset_key("colour") == nullptr);
    // Every key but the legacy auto_boot is written.
    for (const auto& info : kPresetKeys) {
        CHECK(info.written == (info.key != PresetKey::AutoBoot));
    }
}
