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
#include <beebium/econet/StationSelection.hpp>
#include <beebium/extension/ExtensionArgParser.hpp>
#include <beebium/server/CliArgParsers.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

// Preset storage configuration
struct PresetStorageConfig {
    std::optional<std::string> fdc_socket_id;     // e.g., "acorn-1770", "none"
    std::map<uint8_t, std::optional<std::string>> floppy_drives;  // drive -> image_uri (nullopt = empty)
};

// Generic Econet transport sub-configuration: names a transport
// extension (built-in or plugin) and carries its parameters as a flat
// key/value map. AUN, Piconet, and any future transport flow through
// this single shape.
//
// is_list schema params may be given as a JSON array, each item a string or,
// for a parameter with fields, an object; these land in list_parameters.
// Scalar values land in parameters. Both are normalised downstream against
// the manifest schema.
struct PresetTransportConfig {
    std::string name;                            // e.g. "aun" or "piconet"
    std::map<std::string, std::string> parameters;
    std::map<std::string, std::vector<beebium::ConfigListItem>> list_parameters;
};

// Preset Econet configuration
struct PresetEconetConfig {
    int station = 0;                                 // 1-254; unused when auto_range is set
    // Set when the preset's station is "auto" / "auto:lo-hi" (issue #67): the
    // number is chosen at launch, so `station` is unused.
    std::optional<beebium::econet::StationRange> auto_range;
    std::optional<PresetTransportConfig> transport;  // present means "use this transport"
};

// Top-level preset configuration
// Extension instance in a preset file
struct PresetExtensionConfig {
    std::string name;                                              // canonical extension name
    std::map<std::string, std::string> config;                     // scalar key-value configuration
    std::map<std::string, std::vector<beebium::ConfigListItem>> list_config;  // is_list params (JSON arrays)
};

// The startup keyboard links (the options byte read at power-up), in one of
// two forms: screen_mode and auto_boot, or the raw links byte. A preset gives
// one form, never both.
struct PresetStartupConfig {
    std::optional<int> screen_mode;               // 0-7
    std::optional<bool> auto_boot;                // boot on power-up (reversed SHIFT-BREAK)
    std::optional<int> links;                     // 0-255, the whole byte
};

struct PresetConfig {
    std::string name;
    std::optional<std::string> description;       // One-line description shown in the picker
    std::optional<std::string> release_date;      // YYYY, YYYY-MM or YYYY-MM-DD
    std::optional<std::string> machine_name;      // Machine label the preset launches with
    std::optional<std::string> model;             // For validation against executable
    std::optional<PresetStorageConfig> storage;
    std::optional<PresetEconetConfig> econet;
    std::optional<double> thumbnail_capture_delay_seconds;  // For capture-screenshot subcommand
    std::vector<PresetExtensionConfig> extensions{};
    std::vector<SidewaysConfig> sideways{};       // sideways_bank.slots (slot/type/image)
    std::optional<double> speed;                  // multiplier; 0.0 = unlimited (as --speed)
    std::optional<PresetStartupConfig> startup;
    std::optional<std::string> mos_image_uri;     // MOS ROM name or path, as --mos
};

// A remark about one place in a preset file, located by a JSON pointer
// (e.g. "/startup/links"; "" for the file as a whole): an error, which stops
// the preset being used, or a warning, which does not. The one shape every
// stage of loading and resolving a preset reports in, and the one
// resolve-preset prints.
struct Diagnostic {
    enum class Severity { Error, Warning };

    std::string path;
    std::string message;
    Severity severity = Severity::Error;

    bool is_error() const { return severity == Severity::Error; }
    std::string_view severity_name() const { return is_error() ? "error" : "warning"; }
};

// Result of loading a preset file: the configuration, or the error that
// stopped it, and any warnings (unknown keys, keys in a form that is read but
// no longer written).
struct PresetLoadResult {
    std::optional<PresetConfig> config;
    std::vector<Diagnostic> diagnostics{};
    // The file could not be read as a JSON object at all (as against holding
    // something invalid).
    bool unreadable = false;

    bool success() const { return config.has_value(); }
    explicit operator bool() const { return success(); }

    // The error that stopped the load, or nullptr.
    const Diagnostic* error() const {
        for (const auto& d : diagnostics) {
            if (d.is_error()) return &d;
        }
        return nullptr;
    }
    std::vector<Diagnostic> warnings() const {
        std::vector<Diagnostic> result;
        for (const auto& d : diagnostics) {
            if (!d.is_error()) result.push_back(d);
        }
        return result;
    }
};

// The top-level keys of a preset file: the one list of them. The loader reads
// the keys in this order and warns of any other, apply_preset folds each into
// a ServerConfig, preset_document writes them in this order, and
// describe-preset-schema describes the machine settings among them in this
// order. Each of those handles every key in a switch over PresetKey, so a key
// added here and not handled somewhere is a compiler warning (-Wswitch).
enum class PresetKey {
    Model,
    Name,
    ReleaseDate,
    Description,
    MachineName,
    Speed,
    Startup,
    MosImageUri,
    Storage,
    SidewaysBank,
    Econet,
    Extensions,
    ThumbnailCaptureDelaySeconds,
    AutoBoot,  // the older place for startup.auto_boot: read, never written
};

struct PresetKeyInfo {
    PresetKey key;
    std::string_view name;    // the key in a preset file
    std::string_view type;    // its JSON shape
    std::string_view cli;     // the start options that set the same thing
    std::string_view config;  // the ServerConfig fields it lands in
    bool written;             // preset_document writes it
};

inline constexpr std::array<PresetKeyInfo, 14> kPresetKeys{{
    {PresetKey::Model, "model", "string", "", "(checked against the executable)", true},
    {PresetKey::Name, "name", "string", "", "preset_name", true},
    {PresetKey::ReleaseDate, "release_date", "string", "", "", true},
    {PresetKey::Description, "description", "string", "", "", true},
    {PresetKey::MachineName, "machine_name", "string", "--machine-name", "machine_name", true},
    {PresetKey::Speed, "speed", "number or \"unlimited\"", "--speed",
     "speed_multiplier_override", true},
    {PresetKey::Startup, "startup", "object", "--screen-mode, --auto-boot, --links",
     "screen_mode, auto_boot, raw_links", true},
    {PresetKey::MosImageUri, "mos_image_uri", "string", "--mos", "mos_filepath", true},
    {PresetKey::Storage, "storage", "object", "--fdc, --floppy", "fdc_type, floppy_filepaths",
     true},
    {PresetKey::SidewaysBank, "sideways_bank", "object", "--sideways, --language-rom",
     "sideways_configs, rom_slots", true},
    {PresetKey::Econet, "econet", "object", "--station, an Econet transport (--aun, ...)",
     "station_number, station_auto_range, extension_instances", true},
    {PresetKey::Extensions, "extensions", "array", "--<extension>", "extension_instances",
     true},
    {PresetKey::ThumbnailCaptureDelaySeconds, "thumbnail_capture_delay_seconds", "number", "",
     "", true},
    {PresetKey::AutoBoot, "auto_boot", "bool", "", "(read as startup.auto_boot)", false},
}};

// One entry per key, in PresetKey's order.
static_assert([] {
    for (std::size_t i = 0; i < kPresetKeys.size(); ++i) {
        if (static_cast<std::size_t>(kPresetKeys[i].key) != i) return false;
    }
    return kPresetKeys.size() == static_cast<std::size_t>(PresetKey::AutoBoot) + 1;
}());

// The registry entry for a key name, or nullptr if the key is not one.
inline const PresetKeyInfo* find_preset_key(std::string_view name) {
    for (const auto& info : kPresetKeys) {
        if (info.name == name) return &info;
    }
    return nullptr;
}

// Parse the startup section. Returns the config, or nullopt with an error.
inline std::pair<std::optional<PresetStartupConfig>, Diagnostic> parse_startup_section(
    const nlohmann::json& startup_json) {
    PresetStartupConfig startup;
    auto int_in_range = [&](const char* key, int lo, int hi,
                            std::optional<int>& out) -> std::string {
        if (!startup_json.contains(key)) return {};
        const auto& v = startup_json[key];
        if (!v.is_number_integer() || v.get<int64_t>() < lo || v.get<int64_t>() > hi) {
            return std::string("startup '") + key + "' must be an integer " +
                   std::to_string(lo) + "-" + std::to_string(hi);
        }
        out = v.get<int>();
        return {};
    };
    if (auto e = int_in_range("screen_mode", 0, 7, startup.screen_mode); !e.empty()) {
        return {std::nullopt, {"/startup/screen_mode", e}};
    }
    if (auto e = int_in_range("links", 0, 255, startup.links); !e.empty()) {
        return {std::nullopt, {"/startup/links", e}};
    }
    if (startup_json.contains("auto_boot")) {
        if (!startup_json["auto_boot"].is_boolean()) {
            return {std::nullopt,
                    {"/startup/auto_boot", "startup 'auto_boot' must be true or false"}};
        }
        startup.auto_boot = startup_json["auto_boot"].get<bool>();
    }
    if (startup.links && (startup.screen_mode || startup.auto_boot)) {
        return {std::nullopt,
                {"/startup/links",
                 "startup 'links' cannot be combined with 'screen_mode' or 'auto_boot'"}};
    }
    return {startup, {}};
}

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

// Resolve a file path parameter of a preset's extension config against the
// directory holding the preset. A relative path with a directory component
// (e.g. "discs/l3fs.dat", "../maps/aun.json") is joined to the preset's
// directory. A bare name (no directory component) is left as given: it names
// a resource the extension finds on its own search path (a bundled hard-disc
// image, a plugin ROM) or a keyword such as "none". Absolute paths, Unix-style
// absolute paths on Windows, URIs and the empty string are unchanged.
inline std::string resolve_preset_relative_filepath(const std::string& value,
                                                    const std::filesystem::path& preset_dirpath) {
    if (value.empty() || value.find("://") != std::string::npos ||
        is_unix_absolute_path(value)) {
        return value;
    }
    std::filesystem::path path(value);
    if (path.is_absolute() || !path.has_parent_path()) {
        return value;
    }
    return (preset_dirpath / path).lexically_normal().string();
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

    return storage;
}

// A preset config value as a parameter's value is written: a string as it is,
// an integer in decimal, a boolean as true or false; nullopt for anything else.
inline std::optional<std::string> preset_config_text(const nlohmann::json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<int64_t>());
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    return std::nullopt;
}

// An extension's config object, or an Econet transport's parameters, read into
// scalar values and list items: an array is a list parameter's items, each a
// string (or a number or boolean, as text) or an object of fields. The
// manifest checks them downstream (normalise_preset_extensions).
inline void read_preset_extension_config(
        const nlohmann::json& object, std::map<std::string, std::string>& config,
        std::map<std::string, std::vector<beebium::ConfigListItem>>& list_config) {
    for (const auto& [key, value] : object.items()) {
        if (auto text = preset_config_text(value)) {
            config[key] = std::move(*text);
        } else if (value.is_array()) {
            auto& items = list_config[key];
            for (const auto& element : value) {
                if (auto item_text = preset_config_text(element)) {
                    items.emplace_back(std::move(*item_text));
                } else if (element.is_object()) {
                    // A field of any other JSON type keeps its JSON text, which
                    // the field's type check then refuses.
                    beebium::ListItemFields fields;
                    for (const auto& [field, field_value] : element.items()) {
                        if (field_value.is_null()) continue;
                        fields[field] =
                            preset_config_text(field_value).value_or(field_value.dump());
                    }
                    items.emplace_back(std::move(fields));
                }
            }
        }
    }
}

// Parse the econet section from JSON.
// Returns the parsed config on success, or nullopt with error message on failure.
inline std::pair<std::optional<PresetEconetConfig>, Diagnostic> parse_econet_section(
    const nlohmann::json& econet_json) {

    PresetEconetConfig econet;

    // Station (required within econet section): an integer 1-254, or the string
    // "auto" / "auto:lo-hi" to choose a free number at launch (issue #67).
    if (!econet_json.contains("station")) {
        return {std::nullopt, {"/econet/station", "Econet section requires a 'station' field"}};
    }
    if (econet_json["station"].is_number_integer()) {
        econet.station = econet_json["station"].get<int>();
        if (econet.station < 1 || econet.station > 254) {
            return {std::nullopt, {"/econet/station",
                                   "Econet station number must be between 1 and 254, got " +
                                       std::to_string(econet.station)}};
        }
    } else if (econet_json["station"].is_string()) {
        std::string error;
        auto spec = beebium::econet::parse_station_spec(
            econet_json["station"].get<std::string>(), error);
        if (!spec.has_value()) {
            return {std::nullopt, {"/econet/station", "Econet 'station': " + error}};
        }
        if (spec->is_auto) {
            econet.auto_range = spec->range;
        } else {
            econet.station = spec->fixed;
        }
    } else {
        return {std::nullopt, {"/econet/station", "Econet 'station' must be an integer or \"auto\""}};
    }

    // Transport (optional): { name: "<extension-name>", parameters: {...} }.
    // null means no transport, as an absent key does.
    if (econet_json.contains("transport") && !econet_json["transport"].is_null()) {
        const auto& transport_json = econet_json["transport"];
        if (!transport_json.is_object()) {
            return {std::nullopt, {"/econet/transport", "Econet 'transport' must be an object"}};
        }
        if (!transport_json.contains("name") || !transport_json["name"].is_string()) {
            return {std::nullopt, {"/econet/transport/name", "Econet 'transport' requires a string 'name' field"}};
        }
        PresetTransportConfig transport;
        transport.name = transport_json["name"].get<std::string>();
        if (transport.name.empty()) {
            return {std::nullopt, {"/econet/transport/name", "Econet 'transport.name' must not be empty"}};
        }
        if (transport_json.contains("parameters") && transport_json["parameters"].is_object()) {
            read_preset_extension_config(transport_json["parameters"], transport.parameters,
                                         transport.list_parameters);
        }
        econet.transport = std::move(transport);
    }

    // Reject the legacy preset shape with a helpful error so users
    // migrating from older presets aren't left puzzled.
    if (econet_json.contains("piconet")) {
        return {std::nullopt,
                {"/econet/piconet",
                 "Econet 'piconet' is no longer accepted in presets; use "
                 "'transport': { 'name': 'piconet', 'parameters': "
                 "{ 'device_path': '/dev/ttyX' } }"}};
    }
    if (econet_json.contains("aun_port")) {
        return {std::nullopt,
                {"/econet/aun_port",
                 "Econet 'aun_port' is no longer accepted in presets; use "
                 "'transport': { 'name': 'aun', 'parameters': { 'port': 'N' } }"}};
    }

    // Unknown keys silently ignored for forward compatibility
    return {std::move(econet), {}};
}

// Parse the sideways_bank section from JSON.
//
// Shape: { "slots": [ { "slot": <0-15>, "type": "rom"|"ram"|"empty",
//                       "image_uri": "<rom-name-or-path>" }, ... ] }
//
// ROM/RAM image references are not rewritten to file:// URIs the way disc
// images are: a bare name is resolved downstream via the ROM search path (like
// --sideways and the machine's default ROMs), and load_preset resolves a
// relative path with a directory against the preset's directory. Slot
// existence and per-machine topology are validated later against the
// configured machine variant; here we only check structural validity and
// the 0-15 slot range, mirroring parse_sideways_arg.
//
// Returns the parsed slots on success, or nullopt with an error message.
inline std::pair<std::optional<std::vector<SidewaysConfig>>, Diagnostic>
parse_sideways_section(const nlohmann::json& sideways_json) {
    std::vector<SidewaysConfig> slots;

    if (!sideways_json.contains("slots")) {
        return {std::move(slots), {}};  // No slots: nothing to configure.
    }
    if (!sideways_json["slots"].is_array()) {
        return {std::nullopt, {"/sideways_bank/slots", "Sideways 'slots' must be an array"}};
    }

    const auto& slots_json = sideways_json["slots"];
    for (std::size_t index = 0; index < slots_json.size(); ++index) {
        const auto& slot_json = slots_json[index];
        const std::string entry = "/sideways_bank/slots/" + std::to_string(index);
        if (!slot_json.is_object()) {
            return {std::nullopt, {entry, "Sideways slot entry must be an object"}};
        }

        SidewaysConfig cfg{};

        // Slot number (required, 0-15).
        if (!slot_json.contains("slot") || !slot_json["slot"].is_number_integer()) {
            return {std::nullopt, {entry + "/slot", "Sideways slot entry requires an integer 'slot' field"}};
        }
        int slot = slot_json["slot"].get<int>();
        if (slot < 0 || slot > 15) {
            return {std::nullopt, {entry + "/slot", "Sideways slot must be between 0 and 15, got " +
                                                        std::to_string(slot)}};
        }
        cfg.slot = static_cast<std::uint8_t>(slot);

        // Type (required).
        if (!slot_json.contains("type") || !slot_json["type"].is_string()) {
            return {std::nullopt, {entry + "/type", "Sideways slot entry requires a string 'type' field"}};
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
                return {std::nullopt, {entry + "/image_uri", slot_label + ": 'rom' type requires an 'image_uri'"}};
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
                return {std::nullopt, {entry + "/image_uri", slot_label + ": 'empty' type cannot have an 'image_uri'"}};
            }
        } else {
            return {std::nullopt, {entry + "/type", slot_label + ": invalid type '" + type_raw +
                                  "' (expected one of: rom, ram, empty)"}};
        }

        slots.push_back(std::move(cfg));
    }

    return {std::move(slots), {}};
}

// Load and parse a preset file.
//
// Returns a PresetLoadResult containing either the parsed configuration or an error message.
// Unknown top-level keys are ignored with a warning (PresetLoadResult::warnings).
//
// Usage:
//   auto result = load_preset("/path/to/game.preset.beebium");
//   if (result) {
//       // Use result.config
//   } else {
//       std::cerr << "Error: " << result.error << "\n";
//   }
inline PresetLoadResult load_preset(const std::filesystem::path& filepath) {
    auto failed = [](std::string path, std::string message, bool unreadable = false) {
        return PresetLoadResult{std::nullopt, {Diagnostic{std::move(path), std::move(message)}},
                                unreadable};
    };

    // Open the file
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return failed("", "Cannot open preset file: " + filepath.string(), true);
    }

    // Parse JSON
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        return failed("",
                      "Invalid JSON in preset file: " + filepath.string() +
                          ": parse error at byte " + std::to_string(e.byte),
                      true);
    }

    if (!json.is_object()) {
        return failed("", "Preset file must contain a JSON object: " + filepath.string(), true);
    }

    // Relative paths in the preset resolve from the directory holding it,
    // wherever the preset was named from.
    const auto preset_dirpath = std::filesystem::absolute(filepath).parent_path();

    // Parse into PresetConfig
    PresetConfig config;
    std::vector<Diagnostic> result_warnings;

    for (const auto& [key, value] : json.items()) {
        if (!find_preset_key(key)) {
            result_warnings.push_back({"/" + key, "unknown key '" + key + "' is ignored",
                                       Diagnostic::Severity::Warning});
        }
    }

    // The name falls back to the file's, so a preset always has one.
    config.name = filepath.stem().string();

    for (const auto& info : kPresetKeys) {
        const std::string name(info.name);
        if (!json.contains(name)) continue;
        const auto& value = json[name];
        switch (info.key) {
            case PresetKey::Model:
                // For validation against the executable.
                if (value.is_string()) config.model = value.get<std::string>();
                break;
            case PresetKey::Name:
                if (value.is_string()) config.name = value.get<std::string>();
                break;
            case PresetKey::ReleaseDate:
                // Descriptive, shown by front-ends' preset pickers.
                if (value.is_string()) config.release_date = value.get<std::string>();
                break;
            case PresetKey::Description:
                if (value.is_string()) config.description = value.get<std::string>();
                break;
            case PresetKey::MachineName:
                // The label the launched machine carries; a CLI --machine-name
                // overrides it.
                if (value.is_string()) config.machine_name = value.get<std::string>();
                break;
            case PresetKey::Speed:
                // A positive multiplier or "unlimited", as --speed.
                try {
                    if (value.is_number()) {
                        config.speed = parse_speed_arg(value.dump());
                    } else if (value.is_string()) {
                        config.speed = parse_speed_arg(value.get<std::string>());
                    } else {
                        throw std::runtime_error("");
                    }
                } catch (const std::runtime_error&) {
                    return failed("/speed",
                                  "Preset 'speed' must be a positive number or \"unlimited\": " +
                                      filepath.string());
                }
                break;
            case PresetKey::Startup: {
                // The startup keyboard links, as --screen-mode/--auto-boot or --links.
                if (!value.is_object()) {
                    return failed("/startup",
                                  "Preset 'startup' must be an object: " + filepath.string());
                }
                auto [startup, error] = parse_startup_section(value);
                if (!startup) {
                    return failed(error.path, "Preset " + error.message + ": " + filepath.string());
                }
                config.startup = startup;
                break;
            }
            case PresetKey::MosImageUri:
                // A ROM name or path, resolved like --mos.
                if (value.is_string()) {
                    config.mos_image_uri =
                        resolve_preset_relative_filepath(value.get<std::string>(), preset_dirpath);
                }
                break;
            case PresetKey::Storage:
                if (value.is_object()) {
                    config.storage = parse_storage_section(value, preset_dirpath);
                }
                break;
            case PresetKey::SidewaysBank: {
                if (!value.is_object()) break;
                auto [slots, error] = parse_sideways_section(value);
                if (!slots) {
                    return failed(error.path, error.message + ": " + filepath.string());
                }
                for (auto& slot : *slots) {
                    slot.image_filepath =
                        resolve_preset_relative_filepath(slot.image_filepath, preset_dirpath);
                }
                config.sideways = std::move(*slots);
                break;
            }
            case PresetKey::Econet: {
                if (!value.is_object()) break;
                auto [econet, error] = parse_econet_section(value);
                if (!econet) {
                    return failed(error.path, error.message + ": " + filepath.string());
                }
                config.econet = std::move(*econet);
                break;
            }
            case PresetKey::Extensions:
                if (!value.is_array()) break;
                for (const auto& ext_json : value) {
                    if (!ext_json.is_object() || !ext_json.contains("name")) continue;

                    PresetExtensionConfig ext_config;
                    ext_config.name = ext_json["name"].get<std::string>();

                    if (ext_json.contains("config") && ext_json["config"].is_object()) {
                        // A relative file path is resolved against the
                        // preset's directory once the manifest says which
                        // parameters are file paths (normalise_preset_extensions).
                        read_preset_extension_config(ext_json["config"], ext_config.config,
                                                     ext_config.list_config);
                    }

                    config.extensions.push_back(std::move(ext_config));
                }
                break;
            case PresetKey::ThumbnailCaptureDelaySeconds:
                // For the capture-screenshot subcommand.
                if (value.is_number()) {
                    config.thumbnail_capture_delay_seconds = value.get<double>();
                }
                break;
            case PresetKey::AutoBoot:
                // The form presets were written in before the startup section,
                // read as startup.auto_boot (after startup, so the two can be
                // checked against each other).
                if (!value.is_boolean()) break;
                if (config.startup && (config.startup->auto_boot || config.startup->links)) {
                    return failed("/auto_boot",
                                  "Preset gives both a top-level 'auto_boot' and a 'startup' "
                                  "section that sets the links; move it into 'startup': " +
                                      filepath.string());
                }
                if (!config.startup) config.startup.emplace();
                config.startup->auto_boot = value.get<bool>();
                result_warnings.push_back(
                    {"/auto_boot",
                     "a top-level 'auto_boot' is read as 'startup.auto_boot'; write it there "
                     "instead",
                     Diagnostic::Severity::Warning});
                break;
        }
    }

    return {std::move(config), std::move(result_warnings), false};
}

} // namespace beebium::server
