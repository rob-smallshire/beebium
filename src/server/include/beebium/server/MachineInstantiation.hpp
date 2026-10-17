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

// What a machine directory is made of when a machine is made from a preset:
// its preset.json, recording the configuration, and its identity record.
// create-machine makes one before the machine runs; a Save of a machine not
// yet saved makes one while it runs.

#pragma once

#include "beebium/server/MachineDocument.hpp"
#include "beebium/server/PresetSubcommands.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace beebium::server {

// The preset.json of a machine made from `config`: what the configuration
// records (recorded_preset_document), named as its preset or its model, with
// every extension instance's id pinned -- the one it was given or the one
// generated -- so the instance finds the same extensions/<id>/ on every run,
// whatever is added to the machine later. nullopt, with `error`, when it
// cannot be made.
template<typename MachineType>
std::optional<nlohmann::ordered_json> machine_preset_document(
    const ServerConfig<MachineType>& config, std::string& error) {
    using Memory = typename MachineType::Memory;
    auto machine = config.machine;
    for (auto& inst : machine.extension_instances) {
        const auto* manifest = config.launch.extension_resolver.find_by_name(inst.name);
        const bool transport = manifest && manifest->extension_kind == "econet-transport";
        if (!transport && inst.config.count("id") != 0) inst.explicit_keys.insert("id");
    }
    auto header = recorded_preset_header(config, "", "", "");
    if (header.name.empty()) header.name = std::string(Memory::MACHINE_DISPLAY_NAME);
    return recorded_preset_document(config, machine, header, error);
}

// The id of the preset `source` was read from, as the user presets
// directory names it (<id>.beebium-preset), or a file's or directory's
// own name.
inline std::string preset_id_of(const PresetSource& source) {
    auto path = source.path().lexically_normal();
    if (path.filename().empty()) path = path.parent_path();
    return path.stem().string();
}

// The identity record of a new machine made from `config`, named
// `name_template` (empty: the configuration's name, else the model's).
template<typename MachineType>
MachineRecord new_machine_record(const ServerConfig<MachineType>& config, std::string uuid,
                                 const std::string& name_template) {
    using Memory = typename MachineType::Memory;
    MachineRecord record;
    record.uuid = std::move(uuid);
    record.name = !name_template.empty()                ? name_template
                  : !config.machine.machine_name.empty() ? config.machine.machine_name
                                                         : std::string(Memory::MACHINE_DISPLAY_NAME);
    record.model = std::string(Memory::MACHINE_TYPE);
    record.created = iso8601_utc_now();
    if (config.launch.preset) record.source_preset = preset_id_of(*config.launch.preset);
    return record;
}

}  // namespace beebium::server
