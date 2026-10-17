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

// The preset subcommands: describe-preset-schema, list-presets,
// show-preset, report-presets-dirpath, create-preset, resolve-preset,
// delete-preset, import-preset and export-preset.

#pragma once

#include "beebium/server/ExtensionSubcommands.hpp"
#include "beebium/server/RomPaths.hpp"
#include "beebium/disc/DiscControllerRegistry.hpp"
#include "beebium/disc/DiscLoader.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

// DescribePresetSchema subcommand - output preset configuration schema as JSON
template<typename MachineType>
class DescribePresetSchemaSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-preset-schema"; }
    std::string_view description() const override { return "Output preset configuration schema as JSON"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " describe-preset-schema\n"
                  << "\n"
                  << "Outputs JSON describing what a preset for this model may contain, so a\n"
                  << "front-end can build a configuration UI before any machine is launched.\n"
                  << "\n"
                  << "Output includes:\n"
                  << "  - schema_version: Schema format version\n"
                  << "  - model: Machine identification (id, name, description)\n"
                  << "  - sections: one per preset key this model accepts, its 'type' being\n"
                  << "    that key: speed, startup, mos_image_uri, storage, sideways_bank,\n"
                  << "    econet, extensions\n"
                  << "\n"
                  << "The extensions section's catalogue describes each extension this\n"
                  << "model can take as describe-extension --json does; list-fdcs and\n"
                  << "list-disc-formats give the controller and image catalogues.\n"
                  << "resolve-preset shows what a given preset resolves to.\n"
                  << "\n"
                  << "Output is always JSON regardless of --format option.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help for this subcommand
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Check for subcommand-specific --help
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            // Unknown argument
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Build JSON output using ordered_json to preserve key insertion order
        // (standard json sorts keys alphabetically, making output harder to read)
        using ojson = nlohmann::ordered_json;

        ojson output;
        output["schema_version"] = 2;
        output["model"] = ojson{
            {"id", Memory::MACHINE_TYPE},
            {"name", Memory::MACHINE_DISPLAY_NAME},
            {"description", Memory::MACHINE_DESCRIPTION}
        };

        // Build storage section - "type" first for readability
        ojson storage;
        storage["type"] = "storage";

        // Built-in hardware - all current models have cassette
        storage["builtin"]["cassette"] = true;

        if constexpr (HasDiscControllerSocket<Memory>) {
            // Model B: socketed FDC, no built-in controller
            storage["builtin"]["fdc"] = nullptr;

            // List available FDC options for the socket - "id" first in each option
            ojson fdc_options = ojson::array();
            fdc_options.push_back(ojson{
                {"id", "none"},
                {"label", "Empty"},
                {"device", nullptr},
                {"description", "No disc controller"}
            });
            for (const auto& info : DiscControllerRegistry::available()) {
                fdc_options.push_back(ojson{
                    {"id", info.id},
                    {"label", info.display_name},
                    {"device", info.fdc_chip},
                    {"description", info.description}
                });
            }
            storage["fdc_socket"]["options"] = fdc_options;
        } else if constexpr (HasDiscDrives<Memory>) {
            // Model B+: built-in FDC, no socket
            storage["builtin"]["fdc"] = ojson{
                {"device", "WD1770"},
                {"label", "Built-in WD1770"}
            };
            // No fdc_socket key for machines with built-in FDC
        }

        // Floppy drives (if hardware has them) - "drive" first in each entry
        if constexpr (HasDiscDrives<Memory>) {
            // Build image_types from the format registry so new formats appear automatically
            ojson image_types = ojson::array();
            for (const auto& handler : default_format_registry().handlers()) {
                for (auto ext : handler->file_extensions()) {
                    // Strip the leading dot for the schema
                    std::string_view ext_no_dot = ext.substr(1);
                    image_types.push_back(std::string(ext_no_dot));
                }
            }
            storage["floppy_drives"] = ojson::array();
            for (std::size_t drive : {0, 1}) {
                storage["floppy_drives"].push_back(ojson{{"drive", drive},
                                                         {"label", floppy_drive_label(drive)},
                                                         {"tracks", {40, 80}},
                                                         {"sides", 2},
                                                         {"image_types", image_types}});
            }
        }

        ServerConfig<MachineType> resolver_cfg;
        if (auto err = populate_extension_resolver(argv[0], {}, resolver_cfg)) {
            return *err;
        }
        const auto& extension_entries = resolver_cfg.launch.extension_resolver.entries();

        // Each section, by the preset key it describes; written out below in
        // the registry's order.
        std::map<PresetKey, ojson> section_for;

        section_for[PresetKey::Speed] = ojson{
            {"type", "speed"},
            {"default", 1.0},
            {"unlimited", "unlimited"},
            {"description", "Emulation speed as a positive multiple of real time, or "
                            "\"unlimited\" to run flat out"}};

        section_for[PresetKey::Startup] = ojson{
            {"type", "startup"},
            {"screen_mode", ojson{{"min", 0}, {"max", 7}, {"default", 7}}},
            {"auto_boot", ojson{{"default", false}}},
            {"links", startup_links_schema()},
            {"description", "The startup keyboard links: screen_mode and auto_boot, or the "
                            "whole links byte"}};

        section_for[PresetKey::MosImageUri] = ojson{
            {"type", "mos_image_uri"},
            {"default", Memory::DEFAULT_MOS_ROM},
            {"description", "The MOS ROM: a ROM name (found on the ROM search path) or a path"}};

        section_for[PresetKey::Storage] = storage;

        if constexpr (HasEconetSocket<Memory>) {
            const beebium::econet::StationRange auto_range{};
            ojson transports = ojson::array();
            for (const auto& e : extension_entries) {
                if (e.manifest->extension_kind == "econet-transport") {
                    transports.push_back(e.manifest->name);
                }
            }
            section_for[PresetKey::Econet] = ojson{
                {"type", "econet"},
                {"station", ojson{{"min", 1},
                                  {"max", 254},
                                  {"auto", true},
                                  {"auto_default_range",
                                   ojson::array({auto_range.lo, auto_range.hi})}}},
                {"transports", transports}};
        }

        // Sideways ROM/RAM bank section, derived from the machine's slot
        // topology so frontends can render a slot grid (the Memory tab) and
        // show what each socket can hold. A catalogue of known ROMs and
        // categories is deferred until a ROM library exists; for now we expose
        // the topology and the machine's own default ROM placements.
        if constexpr (requires { Memory::slot_topology(typename Memory::MotherboardLinks{}); }) {
            auto topo = Memory::slot_topology(typename Memory::MotherboardLinks{});

            ojson sideways;
            sideways["type"] = "sideways_bank";
            sideways["has_aliasing"] = topo.has_aliasing;

            ojson sockets = ojson::array();
            for (const auto& sock : topo.sockets) {
                ojson capabilities = ojson::array();
                if (sock.supports_rom) capabilities.push_back("rom");
                if (sock.supports_ram) capabilities.push_back("ram");
                if (sock.supports_empty) capabilities.push_back("empty");
                if (sock.supports_write_protect) capabilities.push_back("write_protect");

                sockets.push_back(ojson{
                    {"label", sock.label},
                    {"slots", sock.slots},
                    {"capabilities", capabilities},
                    {"runtime_configurable", sock.runtime_configurable}
                });
            }
            sideways["sockets"] = sockets;

            // Default ROM placements the machine applies unless a preset or
            // --sideways overrides them, so the frontend can show what is
            // already in a slot before the user changes anything.
            ojson default_roms = ojson::array();
            if constexpr (requires { Memory::DEFAULT_LANGUAGE_ROM; }) {
                default_roms.push_back(ojson{
                    {"slot", Memory::DEFAULT_LANGUAGE_SLOT},
                    {"image", Memory::DEFAULT_LANGUAGE_ROM},
                    {"role", "language"}
                });
            }
            if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
                default_roms.push_back(ojson{
                    {"slot", Memory::DEFAULT_DFS_SLOT},
                    {"image", Memory::DEFAULT_DFS_ROM},
                    {"role", "filing"}
                });
            }
            if constexpr (requires { Memory::DEFAULT_BOARD_ROM; }) {
                default_roms.push_back(ojson{
                    {"slot", Memory::DEFAULT_BOARD_ROM_SLOT},
                    {"image", Memory::DEFAULT_BOARD_ROM},
                    {"role", "board"}
                });
            }
            sideways["default_roms"] = default_roms;

            section_for[PresetKey::SidewaysBank] = sideways;
        }

        // The attachment points a preset's extensions may use on this model:
        // the machine's own connectors, then any point an extension provides
        // (a SCSI bus from a host adapter), each with the extensions that
        // attach to it; and the catalogue describing each of those
        // extensions, and each Econet transport, as describe-extension --json
        // does.
        {
            const auto machine_points = machine_attachment_points<Memory>();

            std::vector<std::pair<std::string, std::vector<std::string>>> points;
            for (const auto& id : machine_points) points.push_back({id, {}});
            for (const auto& e : extension_entries) {
                for (const auto& provided : e.manifest->provides) {
                    auto it = std::find_if(points.begin(), points.end(),
                                           [&](const auto& p) { return p.first == provided; });
                    if (it == points.end()) {
                        points.push_back({provided, {e.manifest->name}});
                    } else if (std::find(machine_points.begin(), machine_points.end(),
                                         provided) == machine_points.end()) {
                        it->second.push_back(e.manifest->name);
                    }
                }
            }
            ojson attachment_points = ojson::array();
            for (const auto& [id, providers] : points) {
                ojson attaching = ojson::array();
                for (const auto& e : extension_entries) {
                    const auto& at = e.manifest->attaches_to;
                    if (std::find(at.begin(), at.end(), id) != at.end()) {
                        attaching.push_back(e.manifest->name);
                    }
                }
                const auto* catalogued = find_attachment_point(id);
                ojson max_occupancy = nullptr;
                if (catalogued && catalogued->max_occupancy) {
                    max_occupancy = *catalogued->max_occupancy;
                }
                attachment_points.push_back(ojson{
                    {"id", id},
                    {"display_name", std::string(attachment_point_display_name(id))},
                    {"description", catalogued ? catalogued->description : std::string()},
                    {"min_occupancy", catalogued ? catalogued->min_occupancy : 0},
                    {"max_occupancy", max_occupancy},
                    {"provided_by", providers.empty() ? ojson(nullptr) : ojson(providers)},
                    {"extensions", attaching}});
            }
            ojson catalogue = ojson::array();
            for (const auto& e : extension_entries) {
                const auto& at = e.manifest->attaches_to;
                const bool attachable =
                    std::any_of(at.begin(), at.end(), [&](const std::string& point) {
                        return std::any_of(points.begin(), points.end(),
                                           [&](const auto& p) { return p.first == point; });
                    });
                bool transport = false;
                if constexpr (HasEconetSocket<Memory>) {
                    transport = e.manifest->extension_kind == "econet-transport";
                }
                if (attachable || transport) {
                    catalogue.push_back(extension_manifest_json(*e.manifest, e.source_label));
                }
            }
            // The images a parameter declared bundled may name by bare name,
            // by kind (bundled_images): the hard-disc masters the server ships,
            // and each plugin's own ROMs. Floppy discs have none.
            ojson by_kind = ojson::object();
            ojson kinds = ojson::array();
            for (const auto& k : kBundledImageKinds) {
                by_kind[std::string(k.kind)] = ojson::array();
                kinds.push_back({{"kind", std::string(k.kind)},
                                 {"display_name", std::string(k.display_name)}});
            }
            for (const auto& image : bundled_images(resolver_cfg.launch.extension_resolver)) {
                by_kind[image.kind].push_back(ojson{
                    {"image", image.image},
                    {"display_name", image.display_name},
                    {"size_bytes", image.size_bytes},
                    {"extension", image.extension.empty() ? ojson(nullptr)
                                                          : ojson(image.extension)}});
            }
            section_for[PresetKey::Extensions] = ojson{{"type", "extensions"},
                                                       {"attachment_points", attachment_points},
                                                       {"catalogue", catalogue},
                                                       {"bundled_images", by_kind},
                                                       {"bundled_image_kinds", kinds}};
        }

        ojson sections = ojson::array();
        for (const auto& info : kPresetKeys) {
            if (!describes(info.key)) continue;
            if (auto it = section_for.find(info.key); it != section_for.end()) {
                // Each section names its key as a user calls it, after its type.
                ojson section{{"type", it->second.at("type")}, {"label", std::string(info.label)}};
                for (auto& [key, value] : it->second.items()) {
                    if (key != "type") section[key] = std::move(value);
                }
                sections.push_back(std::move(section));
            }
        }
        output["sections"] = sections;

        std::cout << output.dump(2) << "\n";
        return ExitCode::OK;
    }

private:
    // The keyboard links byte as a preset writes it, which is the byte the
    // machine reads: the eight start-up option links on the keyboard circuit
    // board (SW2, "START UP OPTIONS", in the Advanced User Guide's Appendix J),
    // Link 1 being bit 7 and Link 8 bit 0. A set bit is an open link, a clear
    // bit a made one. screen_mode and auto_boot write bits 0-2 and 3 of
    // `default`; the bit meanings are OSBYTE &FF's.
    static nlohmann::ordered_json startup_links_schema() {
        using ojson = nlohmann::ordered_json;
        struct LinkBit {
            int bit;
            const char* label;
            const char* description;
        };
        static constexpr LinkBit kBits[] = {
            {7, "Filing system",
             "The DNFS ROM starts in the network filing system when this link is made "
             "and in the disc filing system when it is open; the MOS does not read it, "
             "and other ROMs may."},
            {6, "Not used",
             "The MOS does not use this link; it is reserved for applications, which "
             "may read it."},
            {5, "Disc timing (link 3)",
             "With link 4, sets the disc drive step and settle times; the times depend "
             "on the disc controller and the filing system ROM fitted."},
            {4, "Disc timing (link 4)",
             "With link 3, sets the disc drive step and settle times; the times depend "
             "on the disc controller and the filing system ROM fitted."},
            {3, "SHIFT+BREAK action",
             "When this link is made, a reset or BREAK boots the disc and SHIFT+reset or "
             "SHIFT+BREAK does not; when it is open, SHIFT+reset or SHIFT+BREAK boots as "
             "usual."},
            {2, "Screen mode bit 2",
             "Bit 2 of the screen mode the MOS selects at power-up and after BREAK."},
            {1, "Screen mode bit 1",
             "Bit 1 of the screen mode the MOS selects at power-up and after BREAK."},
            {0, "Screen mode bit 0",
             "Bit 0 of the screen mode the MOS selects at power-up and after BREAK."},
        };
        ojson bits = ojson::array();
        for (const auto& b : kBits) {
            bits.push_back(ojson{{"bit", b.bit},
                                 {"link", "Link " + std::to_string(8 - b.bit)},
                                 {"label", b.label},
                                 {"description", b.description}});
        }
        return ojson{{"min", 0},
                     {"max", 255},
                     {"default", 0xFF},
                     {"excludes", ojson::array({"screen_mode", "auto_boot"})},
                     {"link_made_when", "clear"},
                     {"screen_mode_bits", ojson::array({0, 1, 2})},
                     {"auto_boot_bit", 3},
                     {"auto_boot_true_when", "clear"},
                     {"bits", bits}};
    }

    // Whether a preset key has a section here: the machine settings do, the
    // preset's own descriptive keys do not.
    static bool describes(PresetKey key) {
        switch (key) {
            case PresetKey::Speed:
            case PresetKey::Startup:
            case PresetKey::MosImageUri:
            case PresetKey::Storage:
            case PresetKey::SidewaysBank:
            case PresetKey::Econet:
            case PresetKey::Extensions:
                return true;
            case PresetKey::Model:
            case PresetKey::Name:
            case PresetKey::ReleaseDate:
            case PresetKey::Description:
            case PresetKey::MachineName:
            case PresetKey::ThumbnailCaptureDelaySeconds:
            case PresetKey::AutoBoot:
                return false;
        }
        return false;
    }
};

// ListPresets subcommand - list available presets for this model
template<typename MachineType>
class ListPresetsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-presets"; }
    std::string_view description() const override { return "List available presets for this model"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " [--format pretty|tsv|jsonl] list-presets [--json]\n"
                  << "\n"
                  << "Lists presets compatible with this machine model. The output format\n"
                  << "follows the global --format option (pretty on a terminal, tsv when\n"
                  << "piped); rows carry the preset id, name and source (system or user).\n"
                  << "\n"
                  << "Options:\n"
                  << "  --json    Same as --format jsonl\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        bool json_output = false;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--json") {
                json_output = true;
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        // Get model ID for this executable
        std::string_view model_id = Memory::MACHINE_TYPE;

        // Collect the stored presets for this model, system then user.
        struct PresetEntry {
            std::string id;
            std::string name;
            std::string source;  // "system" or "user"
        };
        std::vector<PresetEntry> system_presets;
        std::vector<PresetEntry> user_presets;
        for (const auto& entry : PresetPaths::enumerate()) {
            auto result = load_preset(entry.filepath);
            if (!result) continue;
            if (result.config->model && *result.config->model != model_id) {
                continue;  // Skip presets for other models
            }
            if (entry.origin == PresetPaths::Origin::System) {
                system_presets.push_back({entry.id, result.config->name, "system"});
            } else {
                user_presets.push_back({entry.id, result.config->name, "user"});
            }
        }

        OutputFormat format = json_output ? OutputFormat::Jsonl
                                          : resolve_output_format(global.output_format);

        switch (format) {
            case OutputFormat::Pretty: {
                // The id column is as wide as the longest id plus a two-space gap.
                size_t id_width = 0;
                for (const auto* presets : {&system_presets, &user_presets}) {
                    for (const auto& p : *presets) {
                        id_width = std::max(id_width, p.id.size());
                    }
                }
                id_width += 2;
                auto print_section = [&](std::string_view heading,
                                         const std::vector<PresetEntry>& presets) {
                    std::cout << heading << "\n";
                    if (presets.empty()) {
                        std::cout << "  (none)\n";
                        return;
                    }
                    for (const auto& p : presets) {
                        std::cout << "  " << p.id << std::string(id_width - p.id.size(), ' ')
                                  << p.name << "\n";
                    }
                };
                print_section("Built-in presets:", system_presets);
                std::cout << "\n";
                print_section("User presets:", user_presets);
                break;
            }

            case OutputFormat::Tsv:
                std::cout << "id\tname\tsource\n";
                for (const auto* presets : {&system_presets, &user_presets}) {
                    for (const auto& p : *presets) {
                        std::cout << p.id << "\t" << p.name << "\t" << p.source << "\n";
                    }
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto* presets : {&system_presets, &user_presets}) {
                    for (const auto& p : *presets) {
                        nlohmann::json row{{"id", p.id}, {"name", p.name}, {"source", p.source}};
                        std::cout << row.dump() << "\n";
                    }
                }
                break;

            case OutputFormat::Auto:
                // Should not reach here after resolve_output_format
                break;
        }

        return ExitCode::OK;
    }
};

// ShowPreset subcommand - output preset file contents
template<typename MachineType>
class ShowPresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "show-preset"; }
    std::string_view description() const override { return "Output preset file contents"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " show-preset <id-or-path>\n"
                  << "\n"
                  << "Outputs the JSON contents of a preset: a preset file, or the\n"
                  << "system or user preset with that id.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::string preset_arg;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (!arg.empty() && arg[0] != '-') {
                if (preset_arg.empty()) {
                    preset_arg = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (preset_arg.empty()) {
            std::cerr << "Error: preset ID or path required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        std::string open_error;
        auto source = PresetPaths::open(preset_arg, open_error);
        if (!source) {
            std::cerr << "Error: " << open_error << "\n";
            return ExitCode::NOINPUT;
        }

        // The document as stored
        std::cout << source->document_text();
        return ExitCode::OK;
    }
};

// ReportPresetsDirpath subcommand - report user presets directory
template<typename MachineType>
class ReportPresetsDirpathSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "report-presets-dirpath"; }
    std::string_view description() const override { return "Report user presets directory path"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " report-presets-dirpath\n"
                  << "\n"
                  << "Outputs the directory path where user presets are stored.\n"
                  << "It is presets/ in the per-user directory, which the\n"
                  << "BEEBIUM_USER_DIRPATH environment variable overrides.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Check for subcommand-specific --help
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        std::cout << PresetPaths::get_user_presets_dirpath().string() << "\n";
        return ExitCode::OK;
    }
};

// CreatePreset subcommand - create a new preset
template<typename MachineType>
class CreatePresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "create-preset"; }
    std::string_view description() const override { return "Create a new preset"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " create-preset --name <name> [options]\n"
                  << "\n"
                  << "Creates a new preset for this machine model, recording the machine the\n"
                  << "options describe. The options are parsed exactly as 'start' parses them,\n"
                  << "and only what is given is recorded: a default stays a default.\n"
                  << "\n"
                  << "Preset options:\n"
                  << "  --name <name>         Display name for the preset (required)\n"
                  << "  --description <text>  Short one-line description shown in the picker\n"
                  << "  --release-date <date> Release date (YYYY, YYYY-MM, or YYYY-MM-DD)\n"
                  << "  --from <id|filepath>  Source preset: the baseline the machine options\n"
                  << "                        override, as 'start --preset' does\n"
                  << "  --output <path>       Write to this path instead of the user directory\n"
                  << "\n"
                  << "Machine options, as for 'start' (see 'start --help'):\n"
                  << "  --machine-name, --speed, --screen-mode, --auto-boot, --links, --mos,\n"
                  << "  --language-rom (recorded as the model's language slot), --fdc,\n"
                  << "  --floppy (a relative path is taken from the working directory),\n"
                  << "  --sideways, --station, and --<extension> [k=v:...] for any extension\n"
                  << "  (an Econet transport such as --aun is recorded as the station's).\n"
                  << "Options that configure the launch rather than the machine (--port,\n"
                  << "--wait, --advertise, ...) and those with no preset form\n"
                  << "(--write-protect, --hide, --motherboard-link, a board RTC) are refused.\n"
                  << "\n"
                  << "The preset ID is derived by slugifying the name.\n"
                  << "Outputs the created preset ID (or path if --output used).\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Separate the preset's own options from the machine options, which
        // go to parse_start_arguments as `start` would receive them (with
        // --from as its --preset), but MachineOnly: an option that configures
        // the launch or the board, which a preset cannot record, is refused.
        std::string preset_name;
        std::string preset_description;
        std::string release_date;
        std::string output_filepath;
        std::vector<std::string> machine_args{argv[0]};
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--name" && i + 1 < argc) {
                preset_name = argv[++i];
            } else if (arg == "--description" && i + 1 < argc) {
                preset_description = argv[++i];
            } else if (arg == "--release-date" && i + 1 < argc) {
                release_date = argv[++i];
            } else if (arg == "--output" && i + 1 < argc) {
                output_filepath = argv[++i];
            } else if (arg == "--from" && i + 1 < argc) {
                machine_args.push_back("--preset");
                machine_args.push_back(argv[++i]);
            } else {
                machine_args.push_back(std::move(arg));
            }
        }
        if (preset_name.empty()) {
            std::cerr << "Error: --name is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        ServerConfig<MachineType> config;
        {
            std::vector<char*> machine_argv;
            for (auto& a : machine_args) machine_argv.push_back(a.data());
            machine_argv.push_back(nullptr);
            if (auto exit_code = parse_start_arguments<MachineType>(
                    static_cast<int>(machine_args.size()), machine_argv.data(), 1, config,
                    StartOptionScope::MachineOnly)) {
                return *exit_code;
            }
        }
        if (auto err = validate_config(config)) {
            std::cerr << "Error: " << *err << "\n";
            return ExitCode::USAGE;
        }

        // The source preset's descriptive fields are the baseline for the
        // copy's (its machine settings arrived through parse_start_arguments).
        std::optional<PresetConfig> source;
        if (config.launch.preset) {
            auto loaded = load_preset(*config.launch.preset);
            if (loaded) source = std::move(*loaded.config);
        }
        if (preset_description.empty() && source && source->description) {
            preset_description = *source->description;
        }
        if (release_date.empty() && source && source->release_date) {
            release_date = *source->release_date;
        }

        PresetDocumentHeader header{preset_name, preset_description, release_date,
                                    source ? source->thumbnail_capture_delay_seconds
                                           : std::nullopt};
        std::string write_error;
        auto document = preset_document<MachineType>(config.machine, config.launch.extension_resolver, header, DocumentKeys::Given, write_error);
        if (!document) {
            std::cerr << "Error: " << write_error << "\n";
            return ExitCode::USAGE;
        }
        const auto& preset = *document;

        // Determine output path
        std::filesystem::path target_filepath;
        std::string preset_id;

        if (!output_filepath.empty()) {
            target_filepath = output_filepath;
            // For --output, just output the path
            preset_id = output_filepath;
        } else {
            // Create in user presets directory
            if (!PresetPaths::ensure_user_presets_dirpath_exists()) {
                std::cerr << "Error: cannot create user presets directory\n";
                return ExitCode::IOERR;
            }

            // Generate unique ID
            preset_id = PresetPaths::generate_unique_preset_id(PresetPaths::slugify(preset_name));
            target_filepath = PresetPaths::user_filepath_for(preset_id);
        }

        // Write preset file
        std::ofstream output_file(target_filepath);
        if (!output_file) {
            std::cerr << "Error: cannot write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        output_file << preset.dump(2) << "\n";
        output_file.close();

        if (!output_file) {
            std::cerr << "Error: failed to write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        // Output the created preset ID (or path)
        std::cout << preset_id << "\n";
        return ExitCode::OK;
    }
};

// ResolvePreset subcommand - load a preset exactly as `start` would and print
// the machine it describes, with every key filled in
template<typename MachineType>
class ResolvePresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "resolve-preset"; }
    std::string_view description() const override {
        return "Print a preset as start would load it, with every key filled in";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " resolve-preset <id|filepath> [--json]\n"
                  << "\n"
                  << "Loads a preset the way 'start --preset' does -- the same loader, the same\n"
                  << "extension normalisation and validation, including each extension plugin's\n"
                  << "own check of its configuration -- without building a machine, and\n"
                  << "prints the machine it describes as a preset document in which every key\n"
                  << "is present: defaults filled in, relative paths resolved, the model's\n"
                  << "default ROMs as explicit sideways slots, and every extension parameter\n"
                  << "given (each extension also lists its attaches_to points).\n"
                  << "\n"
                  << "Without --json, the document goes to stdout and any diagnostics to stderr.\n"
                  << "With --json, stdout is one object:\n"
                  << "  {\"preset\": <document, or null if none could be loaded>,\n"
                  << "   \"diagnostics\": [{\"severity\": \"error\"|\"warning\",\n"
                  << "                    \"path\": <JSON pointer into the preset>,\n"
                  << "                    \"location\": <where that is, as a user calls it>,\n"
                  << "                    \"message\": <text>}, ...]}\n"
                  << "\n"
                  << "Exits 0 when there are no errors, 66 when the preset cannot be found or\n"
                  << "read, and 78 when it does not describe a valid machine for this model.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        std::string preset_arg;
        bool json_output = false;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--json") {
                json_output = true;
            } else if (!arg.empty() && arg[0] != '-' && preset_arg.empty()) {
                preset_arg = std::string(arg);
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }
        if (preset_arg.empty()) {
            std::cerr << "Error: a preset id or file path is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        std::vector<Diagnostic> diagnostics;
        auto error = [&](std::string path, std::string message) {
            diagnostics.push_back({std::move(path), std::move(message)});
        };
        auto warning = [&](std::string path, std::string message) {
            diagnostics.push_back(
                {std::move(path), std::move(message), Diagnostic::Severity::Warning});
        };

        std::optional<nlohmann::ordered_json> document;
        PresetLocations locations;
        int exit_code = resolve(argv[0], preset_arg, document, locations, error, warning);

        if (json_output) {
            nlohmann::ordered_json out;
            out["preset"] = document ? *document : nlohmann::ordered_json(nullptr);
            out["diagnostics"] = nlohmann::ordered_json::array();
            for (const auto& d : diagnostics) {
                out["diagnostics"].push_back({{"severity", std::string(d.severity_name())},
                                              {"path", d.path},
                                              {"location", locations.describe(d.path)},
                                              {"message", d.message}});
            }
            std::cout << out.dump(2) << "\n";
        } else {
            if (document) {
                std::cout << document->dump(2) << "\n";
            }
            for (const auto& d : diagnostics) {
                std::cerr << (d.is_error() ? "Error: " : "Warning: ");
                if (!d.path.empty()) std::cerr << d.path << ": ";
                std::cerr << d.message << "\n";
            }
        }
        return exit_code;
    }

private:
    // What the preset's entries are called, as far as resolving got: enough
    // to say where a diagnostic's path is in the words the schema uses.
    struct PresetLocations {
        std::vector<int> sideways_slots;                  // slot of sideways_bank.slots[i]
        std::map<std::string, std::string> instance_names;  // preset pointer -> name

        // Where `path` is, as a user calls it: the extension or transport
        // instance it is in, the floppy drive or sideways slot, else the
        // top-level key's label; empty for the whole document.
        std::string describe(const std::string& path) const {
            if (path.empty()) return {};
            std::vector<std::string> parts;
            for (std::size_t start = 1; start <= path.size();) {
                const auto end = std::min(path.find('/', start), path.size());
                parts.push_back(path.substr(start, end - start));
                start = end + 1;
            }
            auto index = [&](std::size_t i) -> std::optional<std::size_t> {
                if (i >= parts.size() || parts[i].empty() ||
                    parts[i].find_first_not_of("0123456789") != std::string::npos) {
                    return std::nullopt;
                }
                return std::stoul(parts[i]);
            };
            for (const auto& [pointer, name] : instance_names) {
                if (path == pointer || path.starts_with(pointer + "/")) return name;
            }
            if (parts[0] == "storage" && parts.size() > 1 && parts[1] == "floppy_drives") {
                if (auto drive = index(2)) return floppy_drive_label(*drive);
            }
            if (parts[0] == "sideways_bank" && parts.size() > 1 && parts[1] == "slots") {
                if (auto i = index(2); i && *i < sideways_slots.size()) {
                    return sideways_slot_label(sideways_slots[*i]);
                }
            }
            return preset_key_label(parts[0]);
        }
    };

    template<typename Error, typename Warning>
    static int resolve(const char* argv0, const std::string& preset_arg,
                       std::optional<nlohmann::ordered_json>& document,
                       PresetLocations& locations, Error error, Warning warning) {
        using Memory = typename MachineType::Memory;

        // Find and load the preset as `start --preset` does.
        std::string open_error;
        auto source = PresetPaths::open(preset_arg, open_error);
        if (!source) {
            error("", open_error);
            return ExitCode::NOINPUT;
        }
        auto loaded = load_preset(*source);
        for (const auto& w : loaded.warnings()) {
            warning(w.path, w.message);
        }
        if (!loaded) {
            for (const auto& e : loaded.errors()) error(e.path, e.message);
            return loaded.unreadable ? ExitCode::NOINPUT : ExitCode::CONFIG;
        }
        const PresetConfig& preset = *loaded.config;
        for (const auto& slot : preset.sideways) locations.sideways_slots.push_back(slot.slot);
        if (auto err = preset_model_error<Memory>(preset)) {
            error("/model", *err);
            return ExitCode::CONFIG;
        }

        // Build the configuration exactly as parse_start_arguments does with
        // no other arguments.
        ServerConfig<MachineType> config;
        config.launch.preset = std::move(*source);
        apply_preset(config, preset);
        if (auto err = populate_extension_resolver(argv0, {}, config)) {
            error("", "cannot scan the extension directories");
            return *err;
        }
        // Each extension instance from the preset, and its transport, by the
        // name its manifest gives it.
        for (const auto& inst : config.machine.extension_instances) {
            if (!inst.from_preset || inst.preset_pointer.empty()) continue;
            const auto* m = config.launch.extension_resolver.find_by_name(inst.name);
            locations.instance_names[inst.preset_pointer] =
                m && !m->display_name.empty() ? m->display_name : inst.name;
        }
        bool failed = false;
        for (const auto& d : fold_in_preset(config)) {
            if (d.is_error()) {
                error(d.path, d.message);
                failed = true;
            } else {
                warning(d.path, d.message);
            }
        }
        if (auto err = validate_startup_links(config)) {
            error("/startup", *err);
            failed = true;
        }
        if (auto err = validate_sideways_slots(config)) {
            // Locate it at the preset's slot entry for the request at fault.
            std::string path = "/sideways_bank";
            if (err->index) {
                const auto slot = config.machine.sideways_configs[*err->index].slot;
                for (std::size_t i = 0; i < preset.sideways.size(); ++i) {
                    if (preset.sideways[i].slot == slot) {
                        path = "/sideways_bank/slots/" + std::to_string(i);
                    }
                }
            }
            error(path, err->message);
            failed = true;
        }
        if constexpr (HasDiscControllerSocket<Memory>) {
            if (!config.machine.fdc_type.empty() && config.machine.fdc_type != "none" &&
                !DiscControllerRegistry::find(config.machine.fdc_type)) {
                error("/storage/fdc_socket/id",
                      "unknown disc controller '" + config.machine.fdc_type + "' (see list-fdcs)");
                failed = true;
            }
        } else if (!config.machine.fdc_type.empty()) {
            warning("/storage/fdc_socket/id",
                    "ignored: this model has a built-in disc controller");
        }
        apply_rom_defaults(config);
        if (check_bundled_images_exist(config, error, warning)) failed = true;
        check_roms_exist(config, preset, warning);
        check_floppies_exist(config, warning);

        std::string write_error;
        document = preset_document<MachineType>(
            config.machine, config.launch.extension_resolver,
            PresetDocumentHeader{preset.name, preset.description, preset.release_date,
                                 preset.thumbnail_capture_delay_seconds},
            DocumentKeys::Every, write_error);
        if (!document) {
            error("/econet", write_error);
            return ExitCode::CONFIG;
        }
        return failed ? ExitCode::CONFIG : ExitCode::OK;
    }

    // A ROM the machine would load but that cannot be found is reported, not
    // fatal: the ROM directory may differ where the preset is launched.
    template<typename Warning>
    static void check_roms_exist(const ServerConfig<MachineType>& config,
                                 const PresetConfig& preset, Warning warning) {
        auto check = [&](const std::string& path, const std::string& rom) {
            try {
                (void)RomPaths::find_rom(rom, config.launch.reference_container_dirpath());
            } catch (const std::exception& e) {
                warning(path, e.what());
            }
        };
        check(preset.mos_image_uri ? "/mos_image_uri" : "", config.machine.mos_filepath);
        for (const auto& [slot, image] : config.machine.rom_slots) {
            if (image == EMPTY_SLOT_MARKER) continue;
            std::string path;
            for (std::size_t i = 0; i < preset.sideways.size(); ++i) {
                if (preset.sideways[i].slot == slot) {
                    path = "/sideways_bank/slots/" + std::to_string(i) + "/image_uri";
                }
            }
            if (image == RAM_SLOT_MARKER) {
                for (const auto& c : config.machine.sideways_configs) {
                    if (c.slot == slot && !c.image_filepath.empty()) {
                        check(path, c.image_filepath);
                    }
                }
                continue;
            }
            check(path, image);
        }
    }

    // A bare name in a parameter declared bundled must name a ROM its plugin
    // ships, or the launch fails; a hard disc's is searched for (see
    // ResourcePaths.hpp), and one found nowhere is a warning, since the launch
    // may see the directories differently.
    template<typename Error, typename Warning>
    static bool check_bundled_images_exist(const ServerConfig<MachineType>& config,
                                           Error error, Warning warning) {
        bool failed = false;
        const auto images = bundled_images(config.launch.extension_resolver);
        const auto container_dirpath = config.launch.reference_container_dirpath();
        std::vector<std::filesystem::path> disc_dirpaths;
        if (auto master_dirpath = DiscPaths::find_disc_directory()) {
            disc_dirpaths.push_back(*master_dirpath);
        }
        for (const auto& inst : config.machine.extension_instances) {
            const auto* m = config.launch.extension_resolver.find_by_name(inst.name);
            if (!m) continue;
            for (const auto& p : m->parameters) {
                if (p.bundled.empty()) continue;
                auto it = inst.config.find(p.key);
                if (it == inst.config.end() || !is_bare_reference(it->second) ||
                    find_bundled_image(images, *m, p, it->second)) {
                    continue;
                }
                const auto path = inst.from_preset ? instance_parameter_pointer(inst, p.key)
                                                   : std::string();
                if (p.bundled == "rom") {
                    error(path, "'" + it->second + "' is not a ROM " + inst.name + " ships");
                    failed = true;
                    continue;
                }
                try {
                    (void)resolve_reference(it->second, ResourceKind::Disc, container_dirpath,
                                            disc_dirpaths);
                } catch (const std::exception& e) {
                    warning(path, e.what());
                }
            }
        }
        return failed;
    }

    template<typename Warning>
    static void check_floppies_exist(const ServerConfig<MachineType>& config, Warning warning) {
        std::vector<std::filesystem::path> disc_dirpaths;
        if (auto master_dirpath = DiscPaths::find_disc_directory()) {
            disc_dirpaths.push_back(*master_dirpath);
        }
        for (std::size_t drive = 0; drive < config.machine.floppy_filepaths.size(); ++drive) {
            const auto& uri = config.machine.floppy_filepaths[drive];
            if (uri.empty()) continue;
            const auto path = "/storage/floppy_drives/" + std::to_string(drive) + "/image_uri";
            std::filesystem::path filepath;
            if (uri.find("://") != std::string::npos) {
                auto url = beebium::DiscUrl::parse(uri);
                if (!url || !url->is_local()) continue;
                filepath = url->to_filepath();
            } else {
                try {
                    filepath = resolve_reference(uri, ResourceKind::Disc,
                                                 config.launch.reference_container_dirpath(),
                                                 disc_dirpaths).path;
                } catch (const std::exception& e) {
                    warning(path, e.what());
                    continue;
                }
            }
            std::error_code ec;
            if (!std::filesystem::exists(filepath, ec)) {
                warning(path, "disc image not found: " + filepath.string());
            }
        }
    }

};

// DeletePreset subcommand - delete a user preset
template<typename MachineType>
class DeletePresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "delete-preset"; }
    std::string_view description() const override { return "Delete a user preset"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " delete-preset <id>\n"
                  << "\n"
                  << "Deletes a user preset. System presets cannot be deleted.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::string preset_id;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (!arg.empty() && arg[0] != '-') {
                if (preset_id.empty()) {
                    preset_id = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (preset_id.empty()) {
            std::cerr << "Error: preset ID required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Find preset file
        auto entry = PresetPaths::find(preset_id);
        if (!entry) {
            std::cerr << "Error: preset not found: " << preset_id << "\n";
            return ExitCode::NOINPUT;
        }

        if (entry->origin == PresetPaths::Origin::System) {
            std::cerr << "Error: cannot delete system preset: " << preset_id << "\n";
            return ExitCode::USAGE;
        }

        // Delete the file
        std::error_code ec;
        if (!std::filesystem::remove(entry->filepath, ec)) {
            std::cerr << "Error: cannot delete preset file: " << entry->filepath.string();
            if (ec) {
                std::cerr << " (" << ec.message() << ")";
            }
            std::cerr << "\n";
            return ExitCode::IOERR;
        }

        return ExitCode::OK;
    }
};

// ImportPreset subcommand - import a preset file into user directory
template<typename MachineType>
class ImportPresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "import-preset"; }
    std::string_view description() const override { return "Import a preset file into user directory"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " import-preset <filepath>\n"
                  << "\n"
                  << "Imports a preset file into the user presets directory.\n"
                  << "The preset's model field must match this executable's model.\n"
                  << "Outputs the imported preset ID.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::filesystem::path source_filepath;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (!arg.empty() && arg[0] != '-') {
                if (source_filepath.empty()) {
                    source_filepath = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (source_filepath.empty()) {
            std::cerr << "Error: filepath required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        std::error_code ec;
        if (std::filesystem::is_directory(source_filepath, ec)) {
            std::cerr << "Error: import-preset takes a preset file, not a directory: "
                      << source_filepath.string() << "\n";
            return ExitCode::USAGE;
        }
        std::string open_error;
        auto source = PresetSource::open(source_filepath, open_error);
        if (!source) {
            std::cerr << "Error: " << open_error << "\n";
            return ExitCode::NOINPUT;
        }
        std::string parse_error;
        auto parsed = source->parse_document(parse_error);
        if (!parsed) {
            std::cerr << "Error: " << parse_error << "\n";
            return ExitCode::DATAERR;
        }
        nlohmann::json& preset = *parsed;

        // Verify model field matches this executable
        if (!preset.contains("model") || !preset["model"].is_string()) {
            std::cerr << "Error: preset missing 'model' field\n";
            return ExitCode::DATAERR;
        }

        std::string preset_model = preset["model"].get<std::string>();
        if (preset_model != Memory::MACHINE_TYPE) {
            std::cerr << "Error: preset model '" << preset_model << "' does not match this executable's model '"
                      << Memory::MACHINE_TYPE << "'\n";
            return ExitCode::DATAERR;
        }

        // Determine ID from filename: a stored preset's id, else its stem.
        std::string preset_id = PresetPaths::slugify(
            PresetPaths::id_from_filename(source_filepath).value_or(source_filepath.stem().string()));
        if (preset_id.empty()) {
            // Fall back to using name field if present
            if (preset.contains("name") && preset["name"].is_string()) {
                preset_id = PresetPaths::slugify(preset["name"].get<std::string>());
            }
        }
        if (preset_id.empty()) {
            preset_id = "imported-preset";
        }

        // Ensure user presets directory exists
        if (!PresetPaths::ensure_user_presets_dirpath_exists()) {
            std::cerr << "Error: cannot create user presets directory\n";
            return ExitCode::IOERR;
        }

        // Generate unique ID to avoid conflicts
        preset_id = PresetPaths::generate_unique_preset_id(preset_id);

        // Write to user presets directory
        std::filesystem::path target_filepath = PresetPaths::user_filepath_for(preset_id);

        std::ofstream output_file(target_filepath);
        if (!output_file) {
            std::cerr << "Error: cannot write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        output_file << preset.dump(2) << "\n";
        output_file.close();

        if (!output_file) {
            std::cerr << "Error: failed to write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        std::cout << preset_id << "\n";
        return ExitCode::OK;
    }
};

// ExportPreset subcommand - export a preset to a file
template<typename MachineType>
class ExportPresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "export-preset"; }
    std::string_view description() const override { return "Export a preset to a file"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " export-preset <id-or-path> --output <filepath>\n"
                  << "\n"
                  << "Exports a preset (a preset file, or the system or user preset with\n"
                  << "that id) to the specified file path.\n"
                  << "\n"
                  << "Options:\n"
                  << "  --output <path>   Output file path (required)\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::string preset_arg;
        std::filesystem::path output_filepath;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--output" || arg == "-o") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --output requires a path\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
                output_filepath = argv[++i];
            } else if (!arg.empty() && arg[0] != '-') {
                if (preset_arg.empty()) {
                    preset_arg = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (preset_arg.empty()) {
            std::cerr << "Error: preset ID or path required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        if (output_filepath.empty()) {
            std::cerr << "Error: --output is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        std::string open_error;
        auto source = PresetPaths::open(preset_arg, open_error);
        if (!source) {
            std::cerr << "Error: " << open_error << "\n";
            return ExitCode::NOINPUT;
        }

        // Write the document as stored
        std::ofstream output_file(output_filepath, std::ios::binary);
        output_file << source->document_text();
        output_file.close();
        if (!output_file) {
            std::cerr << "Error: cannot write to output file: " << output_filepath.string()
                      << "\n";
            return ExitCode::IOERR;
        }

        return ExitCode::OK;
    }
};

} // namespace beebium::server
