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

#include <beebium/disc/DiscUrl.hpp>
#include <beebium/server/CliArgParsers.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace beebium::server {

// Preset storage configuration (matches docs/plans/preset-schema/storage.md)
struct PresetStorageConfig {
    std::optional<std::string> fdc_socket_id;     // e.g., "acorn-1770", "none"
    std::optional<std::string> fdc_socket_mode;   // e.g., "1770" for Solidisk DFDC
    std::map<uint8_t, std::optional<std::string>> floppy_drives;  // drive -> image_uri (nullopt = empty)
    std::optional<std::string> cassette_image_uri;
};

// Generic Econet transport sub-configuration: names a transport
// extension (built-in or plugin) and carries its parameters as a flat
// key/value map. AUN, Piconet, and any future transport flow through
// this single shape.
//
// is_list schema params may be given as a JSON array of strings; these
// land in list_parameters. Scalar values land in parameters and are
// normalised downstream against the manifest schema.
struct PresetTransportConfig {
    std::string name;                            // e.g. "aun" or "piconet"
    std::map<std::string, std::string> parameters;
    std::map<std::string, std::vector<std::string>> list_parameters;
};

// Preset Econet configuration
struct PresetEconetConfig {
    int station;                                     // 1-254
    std::optional<PresetTransportConfig> transport;  // present means "use this transport"
};

// Top-level preset configuration
// Extension instance in a preset file
struct PresetExtensionConfig {
    std::string name;                                              // canonical extension name
    std::map<std::string, std::string> config;                     // scalar key-value configuration
    std::map<std::string, std::vector<std::string>> list_config;   // is_list params (JSON arrays)
};

struct PresetConfig {
    std::string name;
    std::optional<std::string> machine_name;      // Machine label the preset launches with
    std::optional<bool> auto_boot;                // Startup keyboard link: boot on power-up (runs !BOOT)
    std::optional<std::string> model;             // For validation against executable
    std::optional<PresetStorageConfig> storage;
    std::optional<PresetEconetConfig> econet;
    std::optional<double> thumbnail_capture_delay_seconds;  // For capture-screenshot subcommand
    std::vector<PresetExtensionConfig> extensions{};
    std::vector<SidewaysConfig> sideways{};       // sideways_bank.slots (slot/type/image)
    // Future: startup_options, coprocessor, os_rom
};

// Result of loading a preset file
struct PresetLoadResult {
    std::optional<PresetConfig> config;
    std::string error;

    bool success() const { return config.has_value(); }
    explicit operator bool() const { return success(); }
};

// Check if a path string represents a Unix-style absolute path.
// On Unix, paths starting with '/' are absolute.
// On Windows, std::filesystem considers '/path' relative (no drive letter),
// but for preset portability we treat paths starting with '/' as absolute.
inline bool is_unix_absolute_path(const std::string& path_str) {
    return !path_str.empty() && path_str[0] == '/';
}

// Normalize a path or URI to a file:// URI.
//
// If the input contains "://", it's treated as a URI and returned unchanged.
// Otherwise, it's treated as a filesystem path:
// - Absolute paths are converted to file:// URIs directly
// - Relative paths are resolved relative to base_dirpath, then converted
// - Path components like ".." are resolved to produce clean URIs
// - Unix-style absolute paths (starting with /) are treated as absolute on all platforms
//
// This allows presets to use relative paths (resolved from preset file location)
// or absolute paths, while storing everything as URIs internally.
inline std::string normalize_image_uri(const std::string& uri_or_path,
                                       const std::filesystem::path& base_dirpath) {
    // Check if it's already a URI
    if (uri_or_path.find("://") != std::string::npos) {
        return uri_or_path;
    }

    // Treat as filesystem path
    std::filesystem::path path(uri_or_path);

    // Resolve relative paths against base directory.
    // On Windows, treat Unix-style absolute paths (starting with /) as absolute
    // for preset portability, even though std::filesystem considers them relative.
    bool is_absolute = path.is_absolute() || is_unix_absolute_path(uri_or_path);
    if (!is_absolute) {
        path = base_dirpath / path;
    }

    // Normalize the path to resolve .. and . components
    // Use lexically_normal for textual normalization (no filesystem access)
    path = path.lexically_normal();

    // Convert to file:// URI
    // Use generic_string() to ensure forward slashes in the URI on all platforms
    std::string path_str = path.generic_string();
    if (path_str.empty() || path_str[0] != '/') {
        // Ensure path starts with / for proper file:// URI format
        return "file:///" + path_str;
    }
    return "file://" + path_str;
}

// Parse the storage section from JSON
inline std::optional<PresetStorageConfig> parse_storage_section(
    const nlohmann::json& storage_json,
    const std::filesystem::path& preset_dirpath) {

    PresetStorageConfig storage;

    // Parse fdc_socket
    if (storage_json.contains("fdc_socket")) {
        const auto& fdc = storage_json["fdc_socket"];
        if (fdc.is_object()) {
            if (fdc.contains("id") && fdc["id"].is_string()) {
                storage.fdc_socket_id = fdc["id"].get<std::string>();
            }
            if (fdc.contains("mode") && fdc["mode"].is_string()) {
                storage.fdc_socket_mode = fdc["mode"].get<std::string>();
            }
        }
    }

    // Parse floppy_drives array
    if (storage_json.contains("floppy_drives") && storage_json["floppy_drives"].is_array()) {
        for (const auto& drive_json : storage_json["floppy_drives"]) {
            if (!drive_json.is_object() || !drive_json.contains("drive")) {
                continue;
            }

            uint8_t drive_num = drive_json["drive"].get<uint8_t>();

            if (drive_json.contains("image_uri")) {
                if (drive_json["image_uri"].is_null()) {
                    storage.floppy_drives[drive_num] = std::nullopt;  // Explicit empty
                } else if (drive_json["image_uri"].is_string()) {
                    std::string uri = drive_json["image_uri"].get<std::string>();
                    storage.floppy_drives[drive_num] = normalize_image_uri(uri, preset_dirpath);
                }
            }
        }
    }

    // Parse cassette
    if (storage_json.contains("cassette") && storage_json["cassette"].is_object()) {
        const auto& cassette = storage_json["cassette"];
        if (cassette.contains("image_uri")) {
            if (cassette["image_uri"].is_string()) {
                storage.cassette_image_uri =
                    normalize_image_uri(cassette["image_uri"].get<std::string>(), preset_dirpath);
            }
        }
    }

    return storage;
}

// Parse the econet section from JSON.
// Returns the parsed config on success, or nullopt with error message on failure.
inline std::pair<std::optional<PresetEconetConfig>, std::string> parse_econet_section(
    const nlohmann::json& econet_json) {

    PresetEconetConfig econet;

    // Station number (required within econet section)
    if (!econet_json.contains("station")) {
        return {std::nullopt, "Econet section requires a 'station' field"};
    }
    if (!econet_json["station"].is_number_integer()) {
        return {std::nullopt, "Econet 'station' must be an integer"};
    }
    econet.station = econet_json["station"].get<int>();
    if (econet.station < 1 || econet.station > 254) {
        return {std::nullopt, "Econet station number must be between 1 and 254, got " +
                              std::to_string(econet.station)};
    }

    // Transport (optional): { name: "<extension-name>", parameters: {...} }
    if (econet_json.contains("transport")) {
        const auto& transport_json = econet_json["transport"];
        if (!transport_json.is_object()) {
            return {std::nullopt, "Econet 'transport' must be an object"};
        }
        if (!transport_json.contains("name") || !transport_json["name"].is_string()) {
            return {std::nullopt, "Econet 'transport' requires a string 'name' field"};
        }
        PresetTransportConfig transport;
        transport.name = transport_json["name"].get<std::string>();
        if (transport.name.empty()) {
            return {std::nullopt, "Econet 'transport.name' must not be empty"};
        }
        if (transport_json.contains("parameters") && transport_json["parameters"].is_object()) {
            for (auto& [key, value] : transport_json["parameters"].items()) {
                if (value.is_string()) {
                    transport.parameters[key] = value.get<std::string>();
                } else if (value.is_number_integer()) {
                    transport.parameters[key] = std::to_string(value.get<int64_t>());
                } else if (value.is_boolean()) {
                    transport.parameters[key] = value.get<bool>() ? "true" : "false";
                } else if (value.is_array()) {
                    // Array form for is_list schema params. Schema validation
                    // happens downstream; here we just collect string elements.
                    std::vector<std::string> items;
                    for (const auto& elt : value) {
                        if (elt.is_string()) {
                            items.push_back(elt.get<std::string>());
                        } else if (elt.is_number_integer()) {
                            items.push_back(std::to_string(elt.get<int64_t>()));
                        } else if (elt.is_boolean()) {
                            items.push_back(elt.get<bool>() ? "true" : "false");
                        }
                    }
                    transport.list_parameters[key] = std::move(items);
                }
            }
        }
        econet.transport = std::move(transport);
    }

    // Reject the legacy preset shape with a helpful error so users
    // migrating from older presets aren't left puzzled.
    if (econet_json.contains("piconet")) {
        return {std::nullopt,
                "Econet 'piconet' is no longer accepted in presets; use "
                "'transport': { 'name': 'piconet', 'parameters': "
                "{ 'device_path': '/dev/ttyX' } }"};
    }
    if (econet_json.contains("aun_port")) {
        return {std::nullopt,
                "Econet 'aun_port' is no longer accepted in presets; use "
                "'transport': { 'name': 'aun', 'parameters': { 'port': 'N' } }"};
    }

    // Unknown keys silently ignored for forward compatibility
    return {std::move(econet), ""};
}

// Parse the sideways_bank section from JSON.
//
// Shape: { "slots": [ { "slot": <0-15>, "type": "rom"|"ram"|"empty",
//                       "image_uri": "<rom-name-or-path>" }, ... ] }
//
// ROM/RAM image references are stored verbatim and resolved downstream via
// the ROM search path (like --sideways and the machine's default ROMs), so
// they are NOT rewritten to file:// URIs the way disc images are. Slot
// existence and per-machine topology are validated later against the
// configured machine variant; here we only check structural validity and
// the 0-15 slot range, mirroring parse_sideways_arg.
//
// Returns the parsed slots on success, or nullopt with an error message.
inline std::pair<std::optional<std::vector<SidewaysConfig>>, std::string>
parse_sideways_section(const nlohmann::json& sideways_json) {
    std::vector<SidewaysConfig> slots;

    if (!sideways_json.contains("slots")) {
        return {std::move(slots), ""};  // No slots: nothing to configure.
    }
    if (!sideways_json["slots"].is_array()) {
        return {std::nullopt, "Sideways 'slots' must be an array"};
    }

    for (const auto& slot_json : sideways_json["slots"]) {
        if (!slot_json.is_object()) {
            return {std::nullopt, "Sideways slot entry must be an object"};
        }

        SidewaysConfig cfg{};

        // Slot number (required, 0-15).
        if (!slot_json.contains("slot") || !slot_json["slot"].is_number_integer()) {
            return {std::nullopt, "Sideways slot entry requires an integer 'slot' field"};
        }
        int slot = slot_json["slot"].get<int>();
        if (slot < 0 || slot > 15) {
            return {std::nullopt, "Sideways slot must be between 0 and 15, got " +
                                  std::to_string(slot)};
        }
        cfg.slot = static_cast<std::uint8_t>(slot);

        // Type (required).
        if (!slot_json.contains("type") || !slot_json["type"].is_string()) {
            return {std::nullopt, "Sideways slot entry requires a string 'type' field"};
        }
        std::string type_raw = slot_json["type"].get<std::string>();
        std::string type_lc = ascii_to_lower(type_raw);

        // Image (optional string).
        std::string image;
        if (slot_json.contains("image_uri") && slot_json["image_uri"].is_string()) {
            image = slot_json["image_uri"].get<std::string>();
        }

        const std::string slot_label = "Sideways slot " + std::to_string(slot);
        if (type_lc == "rom") {
            cfg.type = SidewaysSlotType::Rom;
            if (image.empty()) {
                return {std::nullopt, slot_label + ": 'rom' type requires an 'image_uri'"};
            }
            cfg.image_filepath = image;
        } else if (type_lc == "ram") {
            cfg.type = SidewaysSlotType::Ram;
            cfg.image_filepath = image;  // Optional preload image.
            // Optional write-protect switch power-on position (RAM only).
            if (slot_json.contains("write_protected")
                && slot_json["write_protected"].is_boolean()) {
                cfg.write_protected = slot_json["write_protected"].get<bool>();
            }
        } else if (type_lc == "empty") {
            cfg.type = SidewaysSlotType::Empty;
            if (!image.empty()) {
                return {std::nullopt, slot_label + ": 'empty' type cannot have an 'image_uri'"};
            }
        } else {
            return {std::nullopt, slot_label + ": invalid type '" + type_raw +
                                  "' (expected one of: rom, ram, empty)"};
        }

        slots.push_back(std::move(cfg));
    }

    return {std::move(slots), ""};
}

// Load and parse a preset file.
//
// Returns a PresetLoadResult containing either the parsed configuration or an error message.
// Unknown keys are ignored for forward compatibility.
//
// Usage:
//   auto result = load_preset("/path/to/game.preset.beebium");
//   if (result) {
//       // Use result.config
//   } else {
//       std::cerr << "Error: " << result.error << "\n";
//   }
inline PresetLoadResult load_preset(const std::filesystem::path& filepath) {
    // Open the file
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return {std::nullopt, "Cannot open preset file: " + filepath.string()};
    }

    // Parse JSON
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        return {std::nullopt, "Invalid JSON in preset file: " + filepath.string() +
                              ": parse error at byte " + std::to_string(e.byte)};
    }

    if (!json.is_object()) {
        return {std::nullopt, "Preset file must contain a JSON object: " + filepath.string()};
    }

    // Parse into PresetConfig
    PresetConfig config;

    // Name (required)
    if (json.contains("name") && json["name"].is_string()) {
        config.name = json["name"].get<std::string>();
    } else {
        config.name = filepath.stem().string();  // Use filename as fallback
    }

    // Machine name (optional): the label the launched machine carries. A CLI
    // --machine-name overrides it (applied in apply_preset).
    if (json.contains("machine_name") && json["machine_name"].is_string()) {
        config.machine_name = json["machine_name"].get<std::string>();
    }

    // Auto-boot (optional): whether the machine boots on power-up (reversed
    // SHIFT-BREAK link) so the disc's !BOOT runs unattended. A CLI --auto-boot
    // overrides it (applied in apply_preset).
    if (json.contains("auto_boot") && json["auto_boot"].is_boolean()) {
        config.auto_boot = json["auto_boot"].get<bool>();
    }

    // Model (optional, for validation)
    if (json.contains("model") && json["model"].is_string()) {
        config.model = json["model"].get<std::string>();
    }

    // Storage section
    if (json.contains("storage") && json["storage"].is_object()) {
        config.storage = parse_storage_section(json["storage"], filepath.parent_path());
    }

    // Econet section
    if (json.contains("econet") && json["econet"].is_object()) {
        auto [econet, error] = parse_econet_section(json["econet"]);
        if (!econet) {
            return {std::nullopt, error + ": " + filepath.string()};
        }
        config.econet = std::move(*econet);
    }

    // Thumbnail capture delay (for capture-screenshot subcommand)
    if (json.contains("thumbnail_capture_delay_seconds") &&
        json["thumbnail_capture_delay_seconds"].is_number()) {
        config.thumbnail_capture_delay_seconds =
            json["thumbnail_capture_delay_seconds"].get<double>();
    }

    // Extensions section
    if (json.contains("extensions") && json["extensions"].is_array()) {
        auto base_dirpath = filepath.parent_path();
        for (const auto& ext_json : json["extensions"]) {
            if (!ext_json.is_object() || !ext_json.contains("name")) continue;

            PresetExtensionConfig ext_config;
            ext_config.name = ext_json["name"].get<std::string>();

            if (ext_json.contains("config") && ext_json["config"].is_object()) {
                for (auto& [key, value] : ext_json["config"].items()) {
                    if (value.is_string()) {
                        std::string val = value.get<std::string>();
                        // Resolve relative filepaths against preset directory
                        // (heuristic: if the key ends with a path-like suffix)
                        ext_config.config[key] = val;
                    } else if (value.is_number_integer()) {
                        ext_config.config[key] = std::to_string(value.get<int64_t>());
                    } else if (value.is_boolean()) {
                        ext_config.config[key] = value.get<bool>() ? "true" : "false";
                    } else if (value.is_array()) {
                        std::vector<std::string> items;
                        for (const auto& elt : value) {
                            if (elt.is_string()) {
                                items.push_back(elt.get<std::string>());
                            } else if (elt.is_number_integer()) {
                                items.push_back(std::to_string(elt.get<int64_t>()));
                            } else if (elt.is_boolean()) {
                                items.push_back(elt.get<bool>() ? "true" : "false");
                            }
                        }
                        ext_config.list_config[key] = std::move(items);
                    }
                }
            }

            config.extensions.push_back(std::move(ext_config));
        }
    }

    // Sideways bank section
    if (json.contains("sideways_bank") && json["sideways_bank"].is_object()) {
        auto [slots, error] = parse_sideways_section(json["sideways_bank"]);
        if (!slots) {
            return {std::nullopt, error + ": " + filepath.string()};
        }
        config.sideways = std::move(*slots);
    }

    // Note: Unknown keys are silently ignored for forward compatibility

    return {std::move(config), ""};
}

} // namespace beebium::server
