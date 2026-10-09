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
    config.extension_resolver.add_source("test", manifests());
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
    REQUIRE(config.extension_instances.size() == 3);
    CHECK(config.extension_instances[0].config.at("id") == "adapter");
    CHECK(config.extension_instances[1].config.at("scsi-id") == "3");
    CHECK(config.extension_instances[2].config.at("baud") == "19200");  // default
    CHECK(config.extension_instances[2].explicit_keys.empty());
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
    config.screen_mode = 1;  // as a CLI --screen-mode 1 would have left it
    config.screen_mode_set = true;

    CHECK(fold_in_preset(config).empty());
    CHECK(config.screen_mode == 1);
    CHECK(config.auto_boot);
    CHECK(config.rom_slots.at(14) == EMPTY_SLOT_MARKER);
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
    CHECK(config.mos_filepath == MachineType::Memory::DEFAULT_MOS_ROM);
    CHECK(config.rom_slots.at(MachineType::Memory::DEFAULT_LANGUAGE_SLOT) ==
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
    config.station_number = 80;
    CHECK(station_json(config) == 80);
    config.station_auto_range = beebium::econet::StationRange{40, 49};
    CHECK(station_json(config) == "auto:40-49");

    const auto& serial = manifests()[2];
    CHECK(extension_config_json(&serial, {{"baud", "9600"}}, {}, false) ==
          nlohmann::ordered_json{{"baud", 9600}});
}
