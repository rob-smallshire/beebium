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

#pragma once

// The preset machinery, from a loaded PresetConfig to the configuration a
// launch uses, and back to a preset document: applying a preset under the
// command line (apply_preset, fold_in_preset and the merges), normalising,
// naming and attaching its extensions, validating the result against the
// model, the bundled-image catalogue, and the JSON helpers the preset writers
// share. start, resolve-preset and create-preset all go through this. It needs
// no service or machine-running code, so it is unit-tested on its own
// (test_preset_resolution).

#include "beebium/server/DiscPaths.hpp"
#include "beebium/server/PresetLoader.hpp"
#include "beebium/server/PresetPaths.hpp"
#include "beebium/server/ServerConfig.hpp"
#include "beebium/server/SidewaysValidation.hpp"

#include "beebium/disc/DiscConcepts.hpp"
#include "beebium/econet/EconetConcepts.hpp"
#include "beebium/econet/StationSelection.hpp"
#include "beebium/extension/AttachmentPlan.hpp"
#include "beebium/extension/Extension.hpp"
#include "beebium/extension/ExtensionArgParser.hpp"
#include "beebium/extension/ExtensionManifest.hpp"
#include "beebium/extension/ExtensionResolver.hpp"
#include "beebium/extension/OneMHzBusPort.hpp"
#include "beebium/extension/PluginLoader.hpp"
#include "beebium/extension/SerialPort.hpp"
#include "beebium/extension/UserPort.hpp"
#include "beebium/tube/TubeConcepts.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

// A sideways slot as a preset writes it (sideways_bank.slots[]). With
// every_key, a RAM slot always carries image_uri (null when there is none)
// and write_protected; without it, they appear only when set.
inline nlohmann::ordered_json sideways_slot_json(const SidewaysConfig& slot, bool every_key) {
    nlohmann::ordered_json entry{{"slot", slot.slot}};
    switch (slot.type) {
        case SidewaysSlotType::Rom:
            entry["type"] = "rom";
            entry["image_uri"] = slot.image_filepath;
            break;
        case SidewaysSlotType::Ram:
            entry["type"] = "ram";
            if (!slot.image_filepath.empty()) {
                entry["image_uri"] = slot.image_filepath;
            } else if (every_key) {
                entry["image_uri"] = nullptr;
            }
            if (slot.write_protected || every_key) {
                entry["write_protected"] = slot.write_protected;
            }
            break;
        case SidewaysSlotType::Empty:
            entry["type"] = "empty";
            break;
    }
    return entry;
}

// The Econet station as a preset writes it: the number, or "auto" (the
// default range) / "auto:lo-hi" for one chosen at launch.
inline nlohmann::ordered_json station_json(const MachineDescription& machine) {
    if (!machine.station_auto_range) {
        return machine.station_number;
    }
    const auto& r = *machine.station_auto_range;
    const beebium::econet::StationRange defaults{};
    if (r.lo == defaults.lo && r.hi == defaults.hi) {
        return "auto";
    }
    return "auto:" + std::to_string(r.lo) + "-" + std::to_string(r.hi);
}

// An extension instance's config as a preset writes it: each value typed by
// its manifest parameter (an integer parameter as a JSON number, a boolean as
// true/false, anything else a string) and each list parameter as an array.
// A list parameter in `given_items` is written as those items, each in the
// form it was given: a string, or an object whose fields are typed by the
// manifest's. With every_parameter, a parameter with no value is written too
// ("" or []), so the object names every parameter the manifest declares;
// without it, empty values are left out. A null manifest types everything as
// a string.
inline nlohmann::ordered_json extension_config_json(
        const beebium::ExtensionManifest* manifest,
        const std::map<std::string, std::string>& config,
        const std::map<std::string, std::vector<std::string>>& list_config,
        bool every_parameter,
        const std::map<std::string, std::vector<beebium::ConfigListItem>>& given_items = {}) {
    auto find_param = [&](const std::string& key) -> const beebium::ParameterSchema* {
        if (manifest) {
            for (const auto& p : manifest->parameters) {
                if (p.key == key) return &p;
            }
        }
        return nullptr;
    };
    auto param_type = [&](const std::string& key) -> std::string {
        const auto* p = find_param(key);
        return p ? p->type : "string";
    };
    auto to_value = [](const std::string& type, const std::string& v) -> nlohmann::ordered_json {
        if (type == "integer") {
            try {
                std::size_t consumed = 0;
                long long n = std::stoll(v, &consumed);
                if (consumed == v.size()) return nlohmann::ordered_json(n);
            } catch (...) {}
        } else if (type == "boolean") {
            if (v == "true" || v == "1") return nlohmann::ordered_json(true);
            if (v == "false" || v == "0") return nlohmann::ordered_json(false);
        }
        return nlohmann::ordered_json(v);
    };
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    for (const auto& [key, value] : config) {
        if (value.empty() && !every_parameter) continue;
        out[key] = to_value(param_type(key), value);
    }
    auto item_json = [&](const beebium::ParameterSchema* p,
                         const beebium::ConfigListItem& item) -> nlohmann::ordered_json {
        if (!item.fields) return item.text;
        nlohmann::ordered_json object = nlohmann::ordered_json::object();
        if (p) {
            for (const auto& f : p->fields) {
                auto it = item.fields->find(f.key);
                if (it != item.fields->end()) object[f.key] = to_value(f.type, it->second);
            }
        }
        for (const auto& [field, value] : *item.fields) {
            if (!object.contains(field)) object[field] = value;
        }
        return object;
    };
    for (const auto& [key, values] : list_config) {
        if (given_items.count(key) == 0) out[key] = values;
    }
    for (const auto& [key, items] : given_items) {
        const auto* p = find_param(key);
        nlohmann::ordered_json array = nlohmann::ordered_json::array();
        for (const auto& item : items) array.push_back(item_json(p, item));
        out[key] = std::move(array);
    }
    if (every_parameter && manifest) {
        for (const auto& p : manifest->parameters) {
            if (!out.contains(p.key)) {
                out[p.key] = p.is_list ? nlohmann::ordered_json::array()
                                       : nlohmann::ordered_json("");
            }
        }
    }
    return out;
}

// Apply preset configuration to ServerConfig.
// Preset values provide defaults that can be overridden by CLI arguments.
template<typename MachineType>
void apply_preset(ServerConfig<MachineType>& config, const PresetConfig& preset) {
    using Instance = typename ServerConfig<MachineType>::ExtensionInstance;
    // CLI parsing runs after this and overwrites what it names; the sideways
    // slots and startup links are staged and merged afterwards
    // (fold_in_preset), per slot and per form.
    for (const auto& info : kPresetKeys) {
        switch (info.key) {
            case PresetKey::Model:  // checked against the executable when loaded
            case PresetKey::ReleaseDate:
            case PresetKey::Description:
            case PresetKey::ThumbnailCaptureDelaySeconds:  // read by capture-screenshot
            case PresetKey::AutoBoot:  // already folded into startup by the loader
                break;
            case PresetKey::Name:
                config.launch.preset_name = preset.name;
                break;
            case PresetKey::MachineName:
                if (preset.machine_name && config.machine.machine_name.empty()) {
                    config.machine.machine_name = *preset.machine_name;
                }
                break;
            case PresetKey::Speed:
                if (preset.speed) config.machine.speed_multiplier_override = *preset.speed;
                break;
            case PresetKey::Startup:
                config.preset_startup = preset.startup;
                break;
            case PresetKey::MosImageUri:
                if (preset.mos_image_uri) config.machine.mos_filepath = *preset.mos_image_uri;
                break;
            case PresetKey::Storage:
                if (!preset.storage) break;
                if (preset.storage->fdc_socket_id && config.machine.fdc_type.empty()) {
                    config.machine.fdc_type = *preset.storage->fdc_socket_id;
                }
                for (const auto& [drive, uri_opt] : preset.storage->floppy_drives) {
                    // nullopt means an empty drive: left as the empty string.
                    if (drive < config.machine.floppy_filepaths.size() &&
                        config.machine.floppy_filepaths[drive].empty() && uri_opt) {
                        config.machine.floppy_filepaths[drive] = *uri_opt;
                    }
                }
                break;
            case PresetKey::SidewaysBank:
                for (const auto& slot : preset.sideways) {
                    config.preset_sideways_configs.push_back(slot);
                }
                break;
            case PresetKey::Econet: {
                if (!preset.econet) break;
                const auto& econet = *preset.econet;
                if (econet.auto_range.has_value()) {
                    // The number is chosen at launch (issue #67); install_econet
                    // resolves it. A later --station overrides this.
                    config.machine.station_auto_range = econet.auto_range;
                    config.machine.station_number = -1;
                } else {
                    config.machine.station_number = econet.station;
                    config.machine.station_auto_range.reset();
                }
                // The transport becomes an ExtensionInstance, so it flows
                // through the same dispatch as a CLI --aun or --piconet.
                if (econet.transport) {
                    Instance inst;
                    inst.name = econet.transport->name;
                    inst.config = econet.transport->parameters;
                    inst.preset_list_items = econet.transport->list_parameters;
                    inst.from_preset = true;
                    inst.preset_pointer = "/econet/transport";
                    config.machine.extension_instances.push_back(std::move(inst));
                }
                break;
            }
            case PresetKey::Extensions:
                for (std::size_t i = 0; i < preset.extensions.size(); ++i) {
                    const auto& ext = preset.extensions[i];
                    Instance inst;
                    inst.name = ext.name;
                    inst.config = ext.config;
                    inst.preset_list_items = ext.list_config;
                    inst.from_preset = true;
                    inst.preset_pointer = "/extensions/" + std::to_string(i);
                    config.machine.extension_instances.push_back(std::move(inst));
                }
                break;
        }
    }
}

// Fold preset-supplied sideways slots (collected by apply_preset into
// config.preset_sideways_configs) into the active configuration as a
// baseline. A CLI --sideways for the same slot takes precedence: those
// entries are already in config.machine.sideways_configs, so a preset slot is applied
// only when no CLI --sideways targets that exact slot. This keeps CLI-vs-CLI
// duplicate detection in validate_config intact (we never add a second entry
// for a slot the CLI already claimed) while letting presets supply defaults
// the user can still override per slot. Cross-aliased conflicts (preset and
// CLI naming different slots wired to one physical socket) are still surfaced
// by validate_sideways_configs.
//
// Must run after CLI parsing; called at the end of parse_start_arguments.
template<typename MachineType>
void merge_preset_sideways_configs(ServerConfig<MachineType>& config) {
    for (const auto& preset_slot : config.preset_sideways_configs) {
        bool cli_claims_slot = std::any_of(
            config.machine.sideways_configs.begin(), config.machine.sideways_configs.end(),
            [&](const SidewaysConfig& c) { return c.slot == preset_slot.slot; });
        if (cli_claims_slot) {
            continue;
        }
        config.machine.sideways_configs.push_back(preset_slot);
        // Mirror the CLI --sideways path so the machine's default ROM for this
        // slot is suppressed and the preset's choice is what gets loaded.
        if (preset_slot.type == SidewaysSlotType::Rom) {
            config.machine.rom_slots[preset_slot.slot] = preset_slot.image_filepath;
        } else if (preset_slot.type == SidewaysSlotType::Empty) {
            config.machine.rom_slots[preset_slot.slot] = EMPTY_SLOT_MARKER;
        } else if (preset_slot.type == SidewaysSlotType::Ram) {
            config.machine.rom_slots[preset_slot.slot] = RAM_SLOT_MARKER;
        }
    }
}

// Fold the preset's startup links in under the CLI's. Within one form the CLI
// overrides per key (--screen-mode keeps the preset's auto_boot); a CLI option
// of the other form replaces the preset's outright (--links drops the preset's
// screen_mode and auto_boot, and --screen-mode or --auto-boot drops its links),
// so the two forms never meet unless the command line itself mixes them.
template<typename MachineType>
void merge_preset_startup(ServerConfig<MachineType>& config) {
    if (!config.preset_startup) return;
    const auto& preset = *config.preset_startup;
    const bool cli_links = config.machine.raw_links >= 0;
    const bool cli_semantic = config.machine.screen_mode_set || config.machine.auto_boot_set;
    if (preset.links) {
        if (!cli_links && !cli_semantic) {
            config.machine.raw_links = *preset.links;
        }
        return;
    }
    if (cli_links) return;
    if (preset.screen_mode && !config.machine.screen_mode_set) {
        config.machine.screen_mode = *preset.screen_mode;
        config.machine.screen_mode_set = true;
    }
    if (preset.auto_boot && !config.machine.auto_boot_set) {
        config.machine.auto_boot = *preset.auto_boot;
        config.machine.auto_boot_set = true;
    }
}

// The JSON pointer of one of a preset extension instance's parameters: under
// config/ for an extensions[] entry, under parameters/ for the Econet
// transport. "" names the entry itself; a key may go on into the value, as
// "map/1/station" does into a list item.
template<typename Instance>
std::string instance_parameter_pointer(const Instance& inst, const std::string& key) {
    if (key.empty()) return inst.preset_pointer;
    const bool transport = inst.preset_pointer == "/econet/transport";
    return inst.preset_pointer + (transport ? "/parameters/" : "/config/") + key;
}

// Normalise the preset's extension instances (its extensions[] and its
// econet.transport) against their manifests, exactly as parse_extension_args
// normalises a CLI flag's: unknown parameters are rejected, defaults filled,
// required parameters and types checked, and a single value for a list
// parameter made a one-element list. The keys the preset gave become the
// instance's explicit_keys. An extension name no manifest has is an error.
// A relative path the preset gives for a "filepath" parameter is resolved
// against the preset's directory (anchor_reference), as its
// floppy images are; a bare name is left for the extension's own search.
//
// Runs after CLI parsing, when the resolver is populated.
// Returns one error per instance that fails, located by its preset entry, or
// by the parameter, list item or field at fault when the error names one.
template<typename MachineType>
std::vector<Diagnostic> normalise_preset_extensions(ServerConfig<MachineType>& config) {
    std::vector<Diagnostic> errors;
    for (auto& inst : config.machine.extension_instances) {
        if (!inst.from_preset) continue;
        const auto* manifest = config.launch.extension_resolver.find_by_name(inst.name);
        if (!manifest) {
            errors.push_back({inst.preset_pointer,
                              "preset extension '" + inst.name
                                  + "' is not a known extension (see list-extensions)"});
            continue;
        }
        const std::string source =
            (manifest->extension_kind == "econet-transport" ? "preset Econet transport '"
                                                            : "preset extension '")
            + inst.name + "'";
        // Copies: on failure the instance keeps what the preset said, so
        // resolve-preset can still show it.
        auto normalised = beebium::normalise_extension_config(
            source, inst.config, inst.preset_list_items, manifest->parameters);
        if (!normalised.ok) {
            errors.push_back(
                {instance_parameter_pointer(inst, normalised.error_pointer), normalised.error});
            continue;
        }
        inst.config = std::move(normalised.config);
        inst.list_config = std::move(normalised.list_config);
        inst.explicit_keys = std::move(normalised.explicit_keys);
        if (config.launch.preset) {
            const auto& preset_dirpath = config.launch.preset->container_dirpath();
            for (const auto& p : manifest->parameters) {
                auto it = inst.config.find(p.key);
                if (p.type == "filepath" && it != inst.config.end() &&
                    inst.explicit_keys.count(p.key) != 0) {
                    it->second = anchor_reference(it->second, preset_dirpath);
                }
            }
        }
    }
    return errors;
}

// Give every extension instance without a framework `id` one: its extension's
// name, or name-1, name-2, ... when that is taken (make_extension_id), never
// an id another instance was given explicitly. This is the one place an
// instance is named -- the registries refuse an instance without an id -- so
// the same configuration always names its instances the same way: a
// provider_of parameter can name an instance whose author gave it no id, and
// resolve-preset writes the ids into the document so a saved preset stays
// addressable.
template<typename MachineType>
void assign_extension_ids(ServerConfig<MachineType>& config) {
    std::vector<std::string> taken;
    for (const auto& inst : config.machine.extension_instances) {
        if (auto it = inst.config.find("id"); it != inst.config.end()) {
            taken.push_back(it->second);
        }
    }
    for (auto& inst : config.machine.extension_instances) {
        if (inst.config.count("id") != 0) continue;
        auto id = beebium::make_extension_id(inst.name, taken);
        taken.push_back(id);
        inst.config["id"] = std::move(id);
    }
}

// A kind of bundled image, and what a user calls one: a noun to fit a
// sentence ("Choose a hard disc") or, with an "s", a heading ("Bundled ROMs").
struct BundledImageKind {
    std::string_view kind;          // as a parameter's `bundled` names it
    std::string_view display_name;  // e.g. "hard disc"
};

// The kinds of bundled image, in the order describe-preset-schema gives them.
inline constexpr std::array<BundledImageKind, 2> kBundledImageKinds{{
    {"hard-disc", "hard disc"},
    {"rom", "ROM"},
}};

// An image a parameter declared `bundled` may name by its bare name: a hard
// disc master the server ships (kind "hard-disc", any extension), or a ROM a
// plugin ships in its roms/ directory (kind "rom", that extension only).
struct BundledImage {
    std::string kind;
    std::string image;            // the bare name, e.g. "l3fs-v1_26b.dat"
    std::uintmax_t size_bytes = 0;
    std::string display_name;     // the name a user sees
    std::string extension;        // the extension it belongs to; "" = any
    std::filesystem::path filepath;  // the shipped file
};

// Every bundled image, of every kind: the one catalogue describe-preset-schema
// lists and the launch and resolve-preset check names against.
inline std::vector<BundledImage> bundled_images(const beebium::ExtensionResolver& resolver) {
    std::vector<BundledImage> images;
    for (const auto& disc : beebium::server::DiscPaths::bundled_hard_disc_images()) {
        images.push_back({"hard-disc", disc.image, disc.size_bytes, disc.display_name, "", {}});
    }
    for (const auto& e : resolver.entries()) {
        for (const auto& rom : e.manifest->roms) {
            images.push_back({"rom", rom.filename, rom.size,
                              rom.display_name.empty() ? rom.filename : rom.display_name,
                              e.manifest->name,
                              e.manifest->manifest_dirpath / "roms" / rom.filename});
        }
    }
    return images;
}

// The bundled image a bare value of `param` (of extension `manifest`) names,
// or nullptr.
inline const BundledImage* find_bundled_image(const std::vector<BundledImage>& images,
                                              const beebium::ExtensionManifest& manifest,
                                              const beebium::ParameterSchema& param,
                                              const std::string& value) {
    for (const auto& image : images) {
        if (image.kind == param.bundled && image.image == value &&
            (image.extension.empty() || image.extension == manifest.name)) {
            return &image;
        }
    }
    return nullptr;
}

// The attachment points this model's own hardware offers extensions.
template<typename Memory>
std::vector<std::string> machine_attachment_points() {
    std::vector<std::string> points;
    if constexpr (beebium::HasOneMHzBus<Memory>) points.push_back("1mhz-bus");
    if constexpr (beebium::HasUserPort<Memory>) points.push_back("user-port");
    if constexpr (beebium::HasTubeSocket<Memory>) points.push_back("tube");
    if constexpr (beebium::HasSerialPort<Memory>) points.push_back("serial-port");
    return points;
}

// Ask each extension instance's plugin to check its configuration (the
// plugin's optional configuration check, PluginLoader::validate_config), once
// the instances are normalised, merged and named. What a plugin finds keeps
// its severity: an instance from the preset is located at its parameter, one
// from the command line named by its flag. A plugin that cannot be loaded is
// an error at the instance, since the launch would fail to load it too.
// Built-in extensions have no plugin and no check.
template<typename MachineType>
std::vector<Diagnostic> validate_extension_configs(const ServerConfig<MachineType>& config) {
    std::vector<Diagnostic> found;
    beebium::PluginLoader loader;
    for (const auto& inst : config.machine.extension_instances) {
        const auto* m = config.launch.extension_resolver.find_by_name(inst.name);
        if (!m || m->library_stem.empty()) continue;
        const std::string source =
            inst.from_preset
                ? (m->extension_kind == "econet-transport" ? "preset Econet transport '"
                                                           : "preset extension '") +
                      inst.name + "': "
                : "--" + std::string(m->effective_cli_name()) + ": ";
        std::vector<beebium::ConfigDiagnostic> diagnostics;
        try {
            diagnostics = loader.validate_config(*m, inst.config, inst.list_config);
        } catch (const std::exception& e) {
            found.push_back({inst.from_preset ? inst.preset_pointer : std::string(),
                             source + e.what()});
            continue;
        }
        for (const auto& d : diagnostics) {
            found.push_back(
                {inst.from_preset ? instance_parameter_pointer(inst, d.parameter) : std::string(),
                 source + d.message,
                 d.severity == beebium::ConfigDiagnostic::Severity::Error
                     ? Diagnostic::Severity::Error
                     : Diagnostic::Severity::Warning});
        }
    }
    return found;
}

// Check, before anything is loaded, that every peripheral extension instance
// has somewhere to attach (check_attachments): its points exist, are within
// their occupancy, a provider-naming parameter names a provider, and a unique
// parameter is unique on its attachment. An instance from the preset is
// located by its entry (and parameter); one from the command line by its flag.
template<typename MachineType>
std::vector<Diagnostic> check_extension_attachments(
        const ServerConfig<MachineType>& config) {
    std::vector<const beebium::ExtensionManifest*> known;
    for (const auto& e : config.launch.extension_resolver.entries()) {
        known.push_back(e.manifest);
    }
    std::vector<const typename ServerConfig<MachineType>::ExtensionInstance*> instances;
    std::vector<beebium::AttachmentRequest> requests;
    for (const auto& inst : config.machine.extension_instances) {
        const auto* m = config.launch.extension_resolver.find_by_name(inst.name);
        if (m && m->extension_kind == "econet-transport") continue;
        instances.push_back(&inst);
        requests.push_back({inst.name, inst.config});
    }
    std::vector<Diagnostic> errors;
    for (const auto& problem : beebium::check_attachments(
             requests, known, machine_attachment_points<typename MachineType::Memory>())) {
        const auto& inst = *instances[problem.instance];
        if (inst.from_preset) {
            errors.push_back({instance_parameter_pointer(inst, problem.key),
                              "preset extension '" + inst.name + "': " + problem.message});
        } else {
            const auto* m = config.launch.extension_resolver.find_by_name(inst.name);
            errors.push_back({"", "--" + std::string(m ? m->effective_cli_name()
                                                       : std::string_view(inst.name)) +
                                      ": " + problem.message});
        }
    }
    return errors;
}

// Reconcile the preset's extension instances with the command line's, so the
// rule everywhere is CLI-overrides-preset (sideways, station, disc) rather than
// a CLI flag counting as a second instance. Which preset instance a CLI
// instance overrides is decided by identity:
//
//  - An Econet transport is identified by its role: the machine has one Econet
//    socket, so a CLI transport always stands in for the preset's (#150). Of
//    the same name, it merges over the preset's; of a different name, it
//    replaces it outright.
//  - Any other extension may have several instances (two hard discs, say), so
//    its name does not identify one. A CLI instance overrides the preset
//    instance of the same name whose framework `id` it gives; a CLI instance
//    without an id, or with one no preset instance has, is added alongside.
//
// A merge keeps the preset's value for every key the CLI instance did not
// give explicitly -- both instances carry every defaulted key, so only the
// CLI's explicit_keys may win (#166) -- and leaves one instance, in the CLI's
// place. Two transports both from the CLI are left alone so the one-transport
// check still reports that as an error.
//
// Runs after normalise_preset_extensions and before the transport registry is
// built.
template<typename MachineType>
void merge_preset_extensions(ServerConfig<MachineType>& config) {
    using Instance = typename ServerConfig<MachineType>::ExtensionInstance;
    auto& insts = config.machine.extension_instances;
    auto is_transport = [&](const Instance& inst) {
        const auto* m = config.launch.extension_resolver.find_by_name(inst.name);
        return m && m->extension_kind == "econet-transport";
    };
    auto merge_into = [](Instance& cli, const Instance& preset) {
        for (const auto& [key, value] : preset.config) {
            if (cli.explicit_keys.count(key) == 0) {
                cli.config[key] = value;
            }
        }
        for (const auto& [key, values] : preset.list_config) {
            if (cli.explicit_keys.count(key) == 0) {
                cli.list_config[key] = values;
                if (auto it = preset.preset_list_items.find(key);
                    it != preset.preset_list_items.end()) {
                    cli.preset_list_items[key] = it->second;
                }
            }
        }
        // The merged instance was given what either source gave.
        cli.explicit_keys.insert(preset.explicit_keys.begin(), preset.explicit_keys.end());
    };

    std::vector<bool> superseded(insts.size(), false);

    // The Econet transport: the last CLI transport stands in for the preset's.
    int preset_transport = -1;
    int cli_transport = -1;
    for (int i = 0; i < static_cast<int>(insts.size()); ++i) {
        if (!is_transport(insts[i])) continue;
        (insts[i].from_preset ? preset_transport : cli_transport) = i;
    }
    if (preset_transport >= 0 && cli_transport >= 0) {
        if (insts[cli_transport].name == insts[preset_transport].name) {
            merge_into(insts[cli_transport], insts[preset_transport]);
        }
        superseded[preset_transport] = true;
    }

    // Other extensions: a CLI instance overrides the preset instance whose id
    // it gives.
    for (auto& cli : insts) {
        if (cli.from_preset || is_transport(cli) || cli.explicit_keys.count("id") == 0) {
            continue;
        }
        for (std::size_t i = 0; i < insts.size(); ++i) {
            const auto& preset = insts[i];
            if (preset.from_preset && !superseded[i] && preset.name == cli.name &&
                preset.config.count("id") != 0 &&
                preset.config.at("id") == cli.config.at("id")) {
                merge_into(cli, preset);
                superseded[i] = true;
                break;
            }
        }
    }

    std::vector<Instance> kept;
    kept.reserve(insts.size());
    for (std::size_t i = 0; i < insts.size(); ++i) {
        if (!superseded[i]) kept.push_back(std::move(insts[i]));
    }
    insts = std::move(kept);
}

// Why a preset cannot be used with this model's executable, if it names
// another model; a preset that names none suits any.
template<typename Memory>
std::optional<std::string> preset_model_error(const PresetConfig& preset) {
    if (preset.model && *preset.model != Memory::MACHINE_TYPE) {
        return "Preset is for model '" + *preset.model + "' but this executable is '" +
               std::string(Memory::MACHINE_TYPE) + "'";
    }
    return std::nullopt;
}

// Fold the preset staged by apply_preset in under the command line, once both
// are known and the extension resolver is populated: the sideways slots and
// startup links as baselines the CLI overrides, then the extensions --
// normalised as the CLI's were, merged with the CLI's (#150), named
// (assign_extension_ids), checked for somewhere to attach, and checked by
// their own plugins (validate_extension_configs). Returns the problems found,
// errors and warnings, each located in the preset where it can be; the
// extension steps after normalisation run only if it succeeds, and the
// plugins' checks only if the attachments are sound. start and resolve-preset
// both build their configuration through this.
template<typename MachineType>
std::vector<Diagnostic> fold_in_preset(ServerConfig<MachineType>& config) {
    merge_preset_sideways_configs(config);
    merge_preset_startup(config);
    if (auto errors = normalise_preset_extensions(config); !errors.empty()) {
        return errors;
    }
    merge_preset_extensions(config);
    assign_extension_ids(config);
    if (auto errors = check_extension_attachments(config); !errors.empty()) {
        return errors;
    }
    return validate_extension_configs(config);
}

// The startup links: --links excludes the semantic options.
template<typename MachineType>
std::optional<std::string> validate_startup_links(const ServerConfig<MachineType>& config) {
    if (config.machine.raw_links >= 0 && (config.machine.screen_mode_set || config.machine.auto_boot_set)) {
        return "--links cannot be combined with --screen-mode or --auto-boot";
    }
    return std::nullopt;
}

// A problem with the sideways slots: its message and, where it is about one
// slot request, that request's index in ServerConfig::sideways_configs.
struct SidewaysSlotsError {
    std::optional<std::size_t> index;
    std::string message;
};

template<typename MachineType>
std::optional<SidewaysSlotsError> validate_sideways_slots(const ServerConfig<MachineType>& config);

template<typename MachineType>
std::optional<std::string> validate_config(const ServerConfig<MachineType>& config) {
    if (auto err = validate_startup_links(config)) return err;
    if (auto err = validate_sideways_slots(config)) return err->message;
    return std::nullopt;
}

// The sideways slots against the model's topology, and their write-protect
// requests against its switches.
template<typename MachineType>
std::optional<SidewaysSlotsError> validate_sideways_slots(const ServerConfig<MachineType>& config) {
    // Validate --sideways arguments against the machine variant's slot
    // topology, taking into account any motherboard link state that affects
    // slot mapping (e.g. Model B+ S13). Reports nonexistent slots,
    // unsupported types, aliased-socket conflicts, duplicate slot specs.
    using Memory = typename MachineType::Memory;
    if constexpr (requires { Memory::slot_topology(config.board.motherboard_links); }) {
        const auto topology = Memory::slot_topology(config.board.motherboard_links);
        if (auto err = validate_sideways_configs(topology, config.machine.sideways_configs)) {
            return SidewaysSlotsError{
                sideways_config_problems(topology, config.machine.sideways_configs).front().index,
                *err};
        }
    }

    // Validate write-protect requests carried on --sideways slots: the machine
    // must have a write-protect control and each such slot's socket must have a
    // write-protect switch. (parse_sideways_arg already ensures write-protect is
    // only set on a RAM slot.)
    bool any_write_protect = std::any_of(
        config.machine.sideways_configs.begin(), config.machine.sideways_configs.end(),
        [](const SidewaysConfig& c) { return c.write_protected; });
    if (any_write_protect) {
        if constexpr (!requires(Memory m) {
                          m.set_slot_write_protected(uint8_t{0}, bool{});
                      }) {
            return SidewaysSlotsError{std::nullopt,
                                      "write-protect is not supported on this machine variant "
                                      "(no sideways write-protect control)"};
        } else if constexpr (requires {
                                 Memory::slot_topology(config.board.motherboard_links);
                             }) {
            auto topo = Memory::slot_topology(config.board.motherboard_links);
            for (std::size_t i = 0; i < config.machine.sideways_configs.size(); ++i) {
                const auto& c = config.machine.sideways_configs[i];
                if (!c.write_protected) continue;
                const auto* spec =
                    topo.find_socket_for_slot(static_cast<int>(c.slot));
                if (spec == nullptr) {
                    return SidewaysSlotsError{
                        i, "--sideways write-protect: slot " + std::to_string(c.slot)
                               + " does not exist on this machine variant"};
                }
                // Per-slot launch write-protect targets a per-slot switch (a
                // one-slot write-protect group, applied below via
                // set_slot_write_protected). Board-wide switches (e.g. the Watford
                // S2) are runtime controls via SidewaysService.SetSlotProtection,
                // not this flag - their sockets set supports_write_protect false.
                if (!spec->supports_write_protect) {
                    return SidewaysSlotsError{
                        i, "--sideways write-protect: slot " + std::to_string(c.slot)
                               + " has no per-slot write-protect switch on this "
                                 "machine variant"};
                }
            }
        }
    }

    return std::nullopt;  // Valid
}

// Fill in the ROMs the machine gets when the config names none: the model's
// MOS, and its language, DFS and expansion-board ROMs in their slots unless a
// sideways slot already claims that socket. Needs no machine, so the resolved
// configuration can be reported before launch (resolve-preset).
template<typename MachineType>
void apply_rom_defaults(ServerConfig<MachineType>& config) {
    using Memory = typename MachineType::Memory;

    // Apply default MOS ROM if not specified
    if (config.machine.mos_filepath.empty()) {
        config.machine.mos_filepath = std::string(Memory::DEFAULT_MOS_ROM);
    }

    // Apply default ROM fallbacks (BASIC into the language slot, DFS into
    // the DFS slot) when the user has not asked for something different.
    // "Different" is checked at the socket level, not the slot level: on
    // machines with aliasing or link-dependent slot mapping (Model B+ S13)
    // a user --sideways at any slot wired to the same physical chip
    // counts as overriding the default for that chip. This avoids two
    // pitfalls: (a) loading a default into a slot that does not exist on
    // the configured topology, and (b) silently overwriting a user's ROM
    // with the default because the default-slot's load_sideways_rom path
    // still dispatches to the same physical chip.
    auto user_targets_socket =
        [&](const SlotTopology& topo, int socket_index) -> bool {
        for (const auto& cfg : config.machine.sideways_configs) {
            const auto* s = topo.find_socket_for_slot(cfg.slot);
            if (s && s->socket_index == socket_index) return true;
        }
        return false;
    };

    auto skip_default_for_slot =
        [&](uint8_t default_slot) -> bool {
        if constexpr (requires {
                          Memory::slot_topology(config.board.motherboard_links);
                      }) {
            auto topo = Memory::slot_topology(config.board.motherboard_links);
            const auto* spec = topo.find_socket_for_slot(default_slot);
            // Default slot is not present on this configured topology.
            if (spec == nullptr) return true;
            // User already targets this socket via a different slot.
            return user_targets_socket(topo, spec->socket_index);
        } else {
            return false;
        }
    };

    // Load the language ROM into this machine's default language slot unless a
    // --sideways already targets that socket. A --language-rom overrides only
    // the image; the slot is still the machine's own DEFAULT_LANGUAGE_SLOT, so
    // a caller need not know which slot a given variant uses for its language
    // ROM (e.g. slot 14 on the ATPL Sidewise, 15 elsewhere).
    if (!skip_default_for_slot(Memory::DEFAULT_LANGUAGE_SLOT)
        && config.machine.rom_slots.find(Memory::DEFAULT_LANGUAGE_SLOT)
               == config.machine.rom_slots.end()) {
        config.machine.rom_slots[Memory::DEFAULT_LANGUAGE_SLOT] =
            config.machine.language_rom_filepath.empty()
                ? std::string(Memory::DEFAULT_LANGUAGE_ROM)
                : config.machine.language_rom_filepath;
    }

    // Load default DFS ROM if machine has one and slot not overridden
    if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
        if (!skip_default_for_slot(Memory::DEFAULT_DFS_SLOT)
            && config.machine.rom_slots.find(Memory::DEFAULT_DFS_SLOT)
                   == config.machine.rom_slots.end()) {
            config.machine.rom_slots[Memory::DEFAULT_DFS_SLOT] = std::string(Memory::DEFAULT_DFS_ROM);
        }
    }

    // Load the expansion board's own ROM (e.g. the Integra-B's IBOS) if the
    // machine has one and its slot is not overridden.
    if constexpr (requires { Memory::DEFAULT_BOARD_ROM; }) {
        if (!skip_default_for_slot(Memory::DEFAULT_BOARD_ROM_SLOT)
            && config.machine.rom_slots.find(Memory::DEFAULT_BOARD_ROM_SLOT)
                   == config.machine.rom_slots.end()) {
            config.machine.rom_slots[Memory::DEFAULT_BOARD_ROM_SLOT] =
                std::string(Memory::DEFAULT_BOARD_ROM);
        }
    }
}

// Which keys preset_document writes.
enum class DocumentKeys {
    // Only what was given: a setting left at its default, an extension
    // parameter not given, a generated instance id are left out, so the
    // preset keeps following the defaults (create-preset).
    Given,
    // Every key, defaults filled in, null where there is no value (the
    // resolved document resolve-preset prints).
    Every,
};

// What a preset says about itself rather than the machine.
struct PresetDocumentHeader {
    std::string name;
    std::optional<std::string> description;
    std::optional<std::string> release_date;
    std::optional<double> thumbnail_capture_delay_seconds;
};

// An extension instance's config for preset_document: with Given, only the
// parameters in its explicit_keys; with Every, every manifest parameter.
template<typename Instance>
nlohmann::ordered_json document_config_json(const beebium::ExtensionManifest* manifest,
                                            const Instance& inst, DocumentKeys keys) {
    if (keys == DocumentKeys::Every) {
        return extension_config_json(manifest, inst.config, inst.list_config,
                                     /*every_parameter=*/true, inst.preset_list_items);
    }
    std::map<std::string, std::string> config;
    std::map<std::string, std::vector<std::string>> list_config;
    for (const auto& [key, value] : inst.config) {
        if (inst.explicit_keys.count(key) != 0) config[key] = value;
    }
    std::map<std::string, std::vector<beebium::ConfigListItem>> given_items;
    for (const auto& [key, values] : inst.list_config) {
        if (inst.explicit_keys.count(key) == 0) continue;
        list_config[key] = values;
        if (auto it = inst.preset_list_items.find(key); it != inst.preset_list_items.end()) {
            given_items[key] = it->second;
        }
    }
    return extension_config_json(manifest, config, list_config, /*every_parameter=*/false,
                                 given_items);
}

// The preset document describing `machine`, in the one key order every preset
// is written in, its extensions' parameters described by the manifests
// `resolver` finds. The only writer of preset documents: create-preset writes
// with Given, resolve-preset with Every, so a key added here is added to both.
// It sees only the machine description, so a launch setting cannot reach a
// preset. For Every, the machine must have had apply_rom_defaults, whose
// default ROMs become explicit slots. Returns nullopt, with `error` set, when
// the configuration cannot be written as a preset (an Econet transport with
// no station, or two transports).
template<typename MachineType>
std::optional<nlohmann::ordered_json> preset_document(const MachineDescription& machine,
                                                      const beebium::ExtensionResolver& resolver,
                                                      const PresetDocumentHeader& header,
                                                      DocumentKeys keys, std::string& error) {
    using Memory = typename MachineType::Memory;
    using ojson = nlohmann::ordered_json;
    const bool every = keys == DocumentKeys::Every;

    // The Econet transport belongs in the econet section, which needs a
    // station; with none (only possible through extensions[] in a preset),
    // Every writes it among the extensions.
    const bool has_station = machine.station_number >= 1 || machine.station_auto_range;
    const ExtensionInstance* transport = nullptr;
    for (const auto& inst : machine.extension_instances) {
        const auto* manifest = resolver.find_by_name(inst.name);
        if (!(manifest && manifest->extension_kind == "econet-transport") ||
            (!has_station && every)) {
            continue;
        }
        if (transport) {
            error = "at most one Econet transport may be given (already have '" +
                    transport->name + "')";
            return std::nullopt;
        }
        transport = &inst;
    }
    if (transport && !has_station) {
        error = "Econet transport '" + transport->name +
                "' requires --station <1-254> or --station auto";
        return std::nullopt;
    }

    ojson doc;
    auto optional_string = [&](std::string_view key, const std::optional<std::string>& value) {
        if (value && !value->empty()) {
            doc[std::string(key)] = *value;
        } else if (every) {
            doc[std::string(key)] = nullptr;
        }
    };

    for (const auto& info : kPresetKeys) {
        if (!info.written) continue;
        const std::string key(info.name);
        switch (info.key) {
            case PresetKey::Model:
                doc[key] = Memory::MACHINE_TYPE;
                break;
            case PresetKey::Name:
                doc[key] = header.name;
                break;
            case PresetKey::ReleaseDate:
                optional_string(key, header.release_date);
                break;
            case PresetKey::Description:
                optional_string(key, header.description);
                break;
            case PresetKey::MachineName:
                optional_string(key, machine.machine_name.empty()
                                         ? std::nullopt
                                         : std::optional<std::string>(machine.machine_name));
                break;
            case PresetKey::Speed:
                if (machine.speed_multiplier_override || every) {
                    const double speed = machine.speed_multiplier_override.value_or(1.0);
                    doc[key] = speed == 0.0 ? ojson("unlimited") : ojson(speed);
                }
                break;
            case PresetKey::Startup:
                // One form or the other. Every fills in the machine's own
                // (mode 7, SHIFT-BREAK boots) where the config sets none.
                if (machine.raw_links >= 0) {
                    doc[key] = {{"links", machine.raw_links}};
                } else if (every) {
                    doc[key] = {{"screen_mode", machine.screen_mode_set ? machine.screen_mode : 7},
                                {"auto_boot", machine.auto_boot_set && machine.auto_boot}};
                } else if (machine.screen_mode_set || machine.auto_boot_set) {
                    ojson startup = ojson::object();
                    if (machine.screen_mode_set) startup["screen_mode"] = machine.screen_mode;
                    if (machine.auto_boot_set) startup["auto_boot"] = machine.auto_boot;
                    doc[key] = std::move(startup);
                }
                break;
            case PresetKey::MosImageUri:
                if (!machine.mos_filepath.empty()) {
                    doc[key] = machine.mos_filepath;
                }
                break;
            case PresetKey::Storage: {
                // The socket's controller and the floppy drives.
                ojson storage = ojson::object();
                if constexpr (HasDiscControllerSocket<Memory>) {
                    if (!machine.fdc_type.empty()) {
                        storage["fdc_socket"] = {{"id", machine.fdc_type}};
                    } else if (every) {
                        storage["fdc_socket"] = {{"id", "none"}};
                    }
                } else {
                    if (!machine.fdc_type.empty() && !every) {
                        storage["fdc_socket"] = {{"id", machine.fdc_type}};
                    } else if (every) {
                        storage["fdc_socket"] = nullptr;
                    }
                }
                ojson drives = ojson::array();
                for (std::size_t drive = 0; drive < machine.floppy_filepaths.size(); ++drive) {
                    const auto& image = machine.floppy_filepaths[drive];
                    if (image.empty() && !every) continue;
                    drives.push_back(
                        {{"drive", drive},
                         {"image_uri", image.empty()
                                           ? ojson(nullptr)
                                           : ojson(normalize_image_uri(image, {}))}});
                }
                if (!drives.empty()) storage["floppy_drives"] = std::move(drives);
                if (!storage.empty()) doc[key] = std::move(storage);
                break;
            }
            case PresetKey::SidewaysBank: {
                // Given: the slots given, then --language-rom as the model's
                // language slot. Every: every loaded slot, defaults included,
                // in slot order.
                ojson slots = ojson::array();
                if (every) {
                    for (const auto& [slot, image] : machine.rom_slots) {
                        SidewaysConfig entry{};
                        entry.slot = slot;
                        if (image == EMPTY_SLOT_MARKER) {
                            entry.type = SidewaysSlotType::Empty;
                        } else if (image == RAM_SLOT_MARKER) {
                            entry.type = SidewaysSlotType::Ram;
                            for (const auto& c : machine.sideways_configs) {
                                if (c.slot == slot) entry = c;
                            }
                        } else {
                            entry.type = SidewaysSlotType::Rom;
                            entry.image_filepath = image;
                        }
                        slots.push_back(sideways_slot_json(entry, /*every_key=*/true));
                    }
                } else {
                    for (const auto& slot : machine.sideways_configs) {
                        if (!machine.language_rom_filepath.empty() &&
                            slot.slot == Memory::DEFAULT_LANGUAGE_SLOT) {
                            continue;
                        }
                        slots.push_back(sideways_slot_json(slot, /*every_key=*/false));
                    }
                    if (!machine.language_rom_filepath.empty()) {
                        SidewaysConfig language{};
                        language.slot = Memory::DEFAULT_LANGUAGE_SLOT;
                        language.type = SidewaysSlotType::Rom;
                        language.image_filepath = machine.language_rom_filepath;
                        slots.push_back(sideways_slot_json(language, /*every_key=*/false));
                    }
                }
                if (!slots.empty() || every) doc[key] = {{"slots", std::move(slots)}};
                break;
            }
            case PresetKey::Econet:
                if (has_station) {
                    ojson econet{{"station", station_json(machine)}};
                    if (transport) {
                        ojson t{{"name", transport->name}};
                        auto parameters = document_config_json(
                            resolver.find_by_name(transport->name), *transport,
                            keys);
                        if (!parameters.empty() || every) t["parameters"] = std::move(parameters);
                        econet["transport"] = std::move(t);
                    } else if (every) {
                        econet["transport"] = nullptr;
                    }
                    doc[key] = std::move(econet);
                } else if (every) {
                    doc[key] = nullptr;
                }
                break;
            case PresetKey::Extensions: {
                ojson extensions = ojson::array();
                for (const auto& inst : machine.extension_instances) {
                    if (&inst == transport) continue;
                    const auto* manifest = resolver.find_by_name(inst.name);
                    ojson entry{{"name", inst.name}};
                    auto ext_config = document_config_json(manifest, inst, keys);
                    if (!ext_config.empty() || every) entry["config"] = std::move(ext_config);
                    if (every) {
                        entry["attaches_to"] =
                            manifest ? manifest->attaches_to : std::vector<std::string>{};
                    }
                    extensions.push_back(std::move(entry));
                }
                if (!extensions.empty() || every) doc[key] = std::move(extensions);
                break;
            }
            case PresetKey::ThumbnailCaptureDelaySeconds:
                if (header.thumbnail_capture_delay_seconds) {
                    doc[key] = *header.thumbnail_capture_delay_seconds;
                } else if (every) {
                    doc[key] = nullptr;
                }
                break;
            case PresetKey::AutoBoot:  // never written (info.written is false)
                break;
        }
    }
    return doc;
}

} // namespace beebium::server
