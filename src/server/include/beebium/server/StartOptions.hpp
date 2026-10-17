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

// The start options: the command line (and a --preset) parsed into a
// ServerConfig, each option by the parser for the part of the configuration
// it sets; the extension search that tells an extension's flag from an
// unknown one; and the usage text.

#pragma once

#include "beebium/server/SubcommandCommon.hpp"
#include "beebium/server/BuiltinExtensions.hpp"
#include "beebium/server/ServerConfig.hpp"
#include "beebium/server/PresetLoader.hpp"
#include "beebium/server/PresetResolution.hpp"
#include "beebium/server/PresetPaths.hpp"
#include "beebium/extension/ExtensionArgParser.hpp"
#include "beebium/extension/ExtensionResolver.hpp"
#include "beebium/extension/PluginLoader.hpp"
#include "beebium/econet/StationSelection.hpp"
#include "AunMapFile.hpp"

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

template<typename MachineType>
void print_usage(const char* program_name) {
    using Memory = typename MachineType::Memory;
    std::cerr << "Usage: " << program_name << " [options]\n"
              << "\n"
              << "Machine: " << Memory::MACHINE_DISPLAY_NAME << "\n"
              << "\n"
              << "Optional:\n"
              << "  --preset <id|filepath>   Load configuration from a preset file, or from\n"
              << "                           a system or user preset by id (see list-presets;\n"
              << "                           CLI options override preset values)\n"
              << "  --mos <filepath>         MOS ROM filepath (default: " << Memory::DEFAULT_MOS_ROM << ")\n"
              << "  --language-rom <filepath> Language ROM image for this machine's default\n"
              << "                           language slot (slot " << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT)
              << "; default: " << Memory::DEFAULT_LANGUAGE_ROM << ")\n"
              << "  --sideways slot=<0-15>:type=<rom|ram|empty>[:image=<path>][:write-protect]\n"
              << "                           Configure a sideways slot (repeatable). Colon-\n"
              << "                           separated key=value (quote a value containing ':').\n"
              << "                           image required for rom, optional for ram, none for empty.\n"
              << "                           write-protect (ram only) engages the write-protect\n"
              << "                           switch at startup; runtime-toggleable via\n"
              << "                           SidewaysService.SetSlotProtection.\n"
              << "  --write-protect <group>  Engage a protection group's write-protect switch at\n"
              << "                           startup (repeatable). <group> is a protection-group\n"
              << "                           id (e.g. slot-15, board; a running machine\n"
              << "                           lists them in SidewaysService.GetSlotStatus).\n"
              << "  --hide <group>           Engage a protection group's hide switch at startup:\n"
              << "                           its slots read as a constant (repeatable; e.g. the\n"
              << "                           Watford slot-14 / S1 switch).\n"
              << "  --rom-dir <dirpath>      ROM directory (auto-detected if not specified)\n"
              << "  --port <port>            gRPC port (default: " << DEFAULT_GRPC_PORT << ")\n"
              << "  --floppy <drive>:<filepath|url>\n"
              << "                           Load disc image into floppy drive (0 or 1)\n";
    if constexpr (requires { Memory::BOARD_RTC_OPTION; }) {
        std::cerr << "  --" << Memory::BOARD_RTC_OPTION
                  << " clock=<host|emulated>[:time=<YYYY-MM-DDThh:mm[:ss]>|:offset=<e.g. -10y>]\n"
                  << "                           The board's real-time clock. clock=host (default)\n"
                  << "                           follows the host's local time; clock=emulated\n"
                  << "                           advances only with emulated time (deterministic).\n"
                  << "                           time/offset set the start (offset: y M d h m s).\n";
    }

    // Show --fdc option only for machines with disc controller sockets
    if constexpr (HasDiscControllerSocket<Memory>) {
        std::cerr << "  --fdc <type>             Disc controller to install in socket:\n"
                  << "                           acorn-1770 - Acorn WD1770 controller\n"
                  << "                           none - leave socket empty (no disc)\n";
    }

    if constexpr (HasEconetSocket<Memory>) {
        std::cerr << "  --station <n>|auto[:lo-hi]  Econet station number (enables Econet);\n"
                  << "                           'auto' picks a free number in lo-hi (default 1-253)\n";
    }


    std::cerr << "  --screen-mode <0-7>      Startup screen mode (default: 7)\n"
              << "  --auto-boot              A reset or BREAK boots the disc and SHIFT+reset\n"
              << "                           or SHIFT+BREAK does not (the reverse of usual)\n"
              << "  --links <0-255>          Raw startup options byte (mutually exclusive\n"
              << "                           with --screen-mode and --auto-boot)\n";

    // --motherboard-link KEY=VALUE: only relevant on machines whose slot
    // mapping depends on physical jumpers (Model B+ S13 today; others to
    // come). For machines with no such links the option is rejected at
    // parse time, so we hide it from --help to avoid confusion.
    if constexpr (Memory::MotherboardLinks::has_slot_links) {
        std::cerr << "  --motherboard-link KEY=VALUE\n"
                  << "                           Set a motherboard jumper position\n"
                  << "                           (case-insensitive). Repeat for multiple\n"
                  << "                           links. Available on this machine:\n";
        for (const auto& line : Memory::MotherboardLinks::help_lines()) {
            std::cerr << "                             " << line << "\n";
        }
    }

    std::cerr
              << "  --wait[=<mode>]          Wait before starting emulation:\n"
              << "                           cli - wait for RETURN keypress (default if TTY)\n"
              << "                           api - wait for Run() RPC (default if not TTY)\n"
              << "  --speed <multiplier>     Emulation speed as a multiple of real-time\n"
              << "                           (1.0 = real-time, 2.0 = double; 'unlimited' to\n"
              << "                           run flat out)\n"
              << "  --provenance-type <type> Provenance type (e.g., python-client, macos-gui)\n"
              << "  --provenance-uuid <uuid> Provenance instance UUID (RFC 4122)\n"
              << "  --provenance-version <v> Provenance version string\n"
              << "  --machine-uuid <uuid>    Machine identity UUID (RFC 4122, default: auto-generated)\n"
              << "  --machine-name <template> Machine name: text with {key} placeholders\n"
              << "                           filled from the machine's state, e.g.\n"
              << "                           'Station {econet-station}'; see\n"
              << "                           list-name-placeholders (default: from model)\n"
              << "  --machine-ordinal <n>    This machine's instance number from its\n"
              << "                           launcher, shown by {machine-ordinal} as '#n'\n"
              << "  --allow-shutdown         Allow any client to shut down the server\n"
              << "  --advertise              Enable mDNS service advertisement\n"
              << "  --extension-dir <path>   Add an extension search directory. Repeatable;\n"
              << "                           later paths override earlier (and the auto-\n"
              << "                           detected <exe-dir>/extensions) for matching\n"
              << "                           --<cli-name>. Use list-extensions to see what\n"
              << "                           a given combination resolves to.\n"
              << "  --help                   Show this help message\n";

    // Auto-generated extensions section. Built-ins + auto-default extension
    // dir; user --extension-dir paths aren't scanned here (they may not have
    // been parsed yet, and we want --help to be cheap and side-effect-free).
    {
        ServerConfig<MachineType> tmp;
        if (!populate_extension_resolver(program_name, {}, tmp)) {
            const auto& entries = tmp.launch.extension_resolver.entries();
            if (!entries.empty()) {
                std::cerr << "\n"
                          << "Extensions -- configure as: --<name> key=value:key=value ...\n"
                          << "(run 'describe-extension <name>' for each one's parameters):\n";
                for (const auto& e : entries) {
                    std::cerr << "  --" << e.manifest->effective_cli_name();
                    if (!e.manifest->description.empty()) {
                        std::cerr << "\n      " << e.manifest->description;
                    }
                    std::cerr << "\n";
                }
            }
        }
    }

    std::cerr << "\n"
              << "Default sideways ROMs:\n"
              << "  Slot " << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT) << ": "
              << Memory::DEFAULT_LANGUAGE_ROM << " (language ROM)\n";

    // Show DFS default if machine has it
    if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
        std::cerr << "  Slot " << static_cast<int>(Memory::DEFAULT_DFS_SLOT) << ": "
                  << Memory::DEFAULT_DFS_ROM << " (disc filing system)\n";
    }

    // Show the expansion board's own ROM if the machine has one
    if constexpr (requires { Memory::DEFAULT_BOARD_ROM; }) {
        std::cerr << "  Slot " << static_cast<int>(Memory::DEFAULT_BOARD_ROM_SLOT) << ": "
                  << Memory::DEFAULT_BOARD_ROM << " (expansion board ROM)\n";
    }

    std::cerr << "\n"
              << "Examples:\n"
              << "  " << program_name << "                                  # Use all defaults\n"
              << "  " << program_name << " --sideways slot=" << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT)
              << ":type=rom:image=forth.rom   # Replace BASIC with Forth\n"
              << "  " << program_name << " --floppy 0:game.ssd              # Load disc image in floppy 0\n";

    if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
        std::cerr << "  " << program_name << " --sideways slot=" << static_cast<int>(Memory::DEFAULT_DFS_SLOT)
                  << ":type=empty       # No DFS (leave that slot empty)\n";
    }

    if constexpr (HasDiscControllerSocket<Memory>) {
        std::cerr << "  " << program_name << " --fdc acorn-1770               # Install disc controller\n";
    }
}

// The command-line flag for a machine's built-in real-time clock (e.g.
// "--integra-rtc"), or empty if the machine has none.
template<typename Memory>
std::string board_rtc_flag() {
    if constexpr (requires { Memory::BOARD_RTC_OPTION; }) {
        return "--" + std::string(Memory::BOARD_RTC_OPTION);
    } else {
        return {};
    }
}

// Log an extension instance's configuration and create it: from its plugin
// library, or from the built-in factory for one with no library. The instance's
// config is moved into the extension. Returns nullptr, having said why on
// stderr, when there is nothing to create it from. `what` names the kind for
// the log ("extension", "transport").
template<typename Instance>
std::unique_ptr<beebium::Extension> instantiate_extension(
        const beebium::ExtensionManifest& manifest, Instance& inst,
        beebium::PluginLoader& plugin_loader, std::string_view what) {
    std::cout << "Loading " << what << ": " << manifest.name << " (id=" << inst.config["id"]
              << ")\n";
    for (const auto& [key, value] : inst.config) {
        if (key != "id" && key != "machine_uuid") {
            std::cout << "  " << key << "=" << value << "\n";
        }
    }
    for (const auto& [key, values] : inst.list_config) {
        for (const auto& v : values) {
            std::cout << "  " << key << "=" << v << "\n";
        }
    }

    if (!manifest.library_stem.empty()) {
        return plugin_loader.load_extension(manifest, std::move(inst.config),
                                            std::move(inst.list_config));
    }
    const auto* builtin_entry = beebium::builtin_extensions::find(inst.name);
    if (!builtin_entry) {
        std::cerr << "Error: Extension '" << inst.name
                  << "' has no library and no built-in factory.\n";
        return nullptr;
    }
    auto loaded = builtin_entry->factory();
    loaded->set_manifest(manifest);
    loaded->set_config(std::move(inst.config));
    loaded->set_list_config(std::move(inst.list_config));
    return loaded;
}

// Parse command-line arguments for the 'start' subcommand into a ServerConfig struct.
// Returns:
//   - std::nullopt on success (continue execution)
//   - ExitCode::OK for --help (exit successfully)
//   - ExitCode::USAGE or ExitCode::CONFIG for errors
//
// Preset merge semantics: If --preset is specified, the preset file is loaded first
// and its values populate the config. Then CLI arguments are parsed and can override
// any preset value. This allows presets to provide defaults while CLI provides overrides.
// Resolve the default extension directory <exe-dir>/extensions if it
// exists. beebium_finalize_plugin() (cmake/BeebiumPlugin.cmake) post-
// build-copies every dynamic plugin to this layout so the same rule
// works on every platform and every build configuration -- including
// MSBuild / Xcode multi-config where the exe sits in a $(Configuration)
// subdirectory. The canonical install layout mirrors this: plugins
// ship alongside the server binary. Absent on stripped-down installs.
inline std::optional<std::string> resolve_default_extension_dirpath(const char* argv0) {
    // Resolve the real executable directory from the OS (/proc/self/exe and
    // platform equivalents), which is robust to a bare argv[0]. This matters
    // for the installed layout: a /usr/bin/beebium-model-b symlink invoked by
    // name gives argv[0] == "beebium-model-b", which canonical() cannot resolve
    // against the cwd -- so a plain canonical(argv0) silently finds no
    // extensions. Fall back to argv0 only if the OS lookup fails.
    auto exe_dirpath = beebium::platform::executable_directory();
    if (!exe_dirpath) {
        std::error_code ec;
        auto exe_path = std::filesystem::canonical(std::filesystem::path(argv0), ec);
        if (ec) {
            return std::nullopt;
        }
        exe_dirpath = exe_path.parent_path();
    }
    auto candidate = *exe_dirpath / "extensions";
    if (!std::filesystem::exists(candidate)) {
        return std::nullopt;
    }
    return candidate.string();
}

// Populate the four extension-resolution fields on a ServerConfig. The
// resolver is rebuilt with built-ins (lowest priority), the auto-default
// extension directory if it exists, and each user_dirs entry in order
// (highest priority last). User-supplied paths use the Throw policy --
// a typo is reported up the stack as ExitCode::CONFIG.
template<typename MachineType>
std::optional<int> populate_extension_resolver(
        const char* argv0,
        const std::vector<std::string>& user_dirs,
        ServerConfig<MachineType>& config) {
    config.launch.default_extension_dirpath = resolve_default_extension_dirpath(argv0);

    const auto& builtin_entries = beebium::builtin_extensions::entries();
    config.launch.builtin_manifests.clear();
    config.launch.builtin_manifests.reserve(builtin_entries.size());
    for (const auto& e : builtin_entries) {
        config.launch.builtin_manifests.push_back(e.manifest);
    }

    config.launch.extension_resolver = beebium::ExtensionResolver{};
    config.launch.extension_resolver.add_source("built-in", config.launch.builtin_manifests);
    config.launch.extension_scan_results.clear();

    auto scan = [&](const std::string& path,
                    beebium::PluginLoader::MissingDirPolicy policy) -> std::optional<int> {
        beebium::PluginLoader scanner;
        try {
            config.launch.extension_scan_results.push_back(scanner.scan_manifests(path, policy));
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::CONFIG;
        }
        config.launch.extension_resolver.add_source(path, config.launch.extension_scan_results.back());
        return std::nullopt;
    };

    if (config.launch.default_extension_dirpath) {
        if (auto err = scan(*config.launch.default_extension_dirpath,
                beebium::PluginLoader::MissingDirPolicy::ReturnEmpty)) {
            return err;
        }
    }
    for (const auto& user_path : user_dirs) {
        if (auto err = scan(user_path,
                beebium::PluginLoader::MissingDirPolicy::Throw)) {
            return err;
        }
    }
    return std::nullopt;
}

// Which start options parse_start_arguments accepts: all of them (start), or
// only those that describe the machine, refusing the rest (create-preset).
enum class StartOptionScope { Everything, MachineOnly };

// What an option parser made of argv[i]: whether the option is one of its
// own (i then advanced past its value), and the exit code when it was
// malformed.
struct OptionParse {
    bool recognised = false;
    std::optional<int> exit_code;
};

// An option that configures how this launch runs: it writes only `launch`.
inline OptionParse parse_launch_option(const std::string& arg, int& i, int argc, char* argv[],
                                       LaunchSettings& launch) {
    auto usage_error = [](const std::string& message) {
        std::cerr << "Error: " << message << "\n";
        return OptionParse{true, ExitCode::USAGE};
    };
    if (arg == "--rom-dir" && i + 1 < argc) {
        launch.rom_dirpath = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
        try {
            launch.port = static_cast<uint16_t>(parse_int(argv[++i], "--port"));
        } catch (const std::runtime_error& e) {
            return usage_error(e.what());
        }
    } else if (arg == "--wait") {
        // Bare --wait: use TTY detection to choose default
        launch.wait_mode = platform::is_stdin_tty() ? WaitMode::Cli : WaitMode::Api;
    } else if (arg.rfind("--wait=", 0) == 0) {
        // --wait=cli or --wait=api
        try {
            launch.wait_mode = parse_wait_arg(arg.substr(7));  // Skip "--wait="
        } catch (const std::runtime_error& e) {
            return usage_error(e.what());
        }
    } else if (arg == "--provenance-type" && i + 1 < argc) {
        launch.provenance_type = argv[++i];
    } else if (arg == "--provenance-uuid" && i + 1 < argc) {
        launch.provenance_uuid = argv[++i];
    } else if (arg == "--provenance-version" && i + 1 < argc) {
        launch.provenance_version = argv[++i];
    } else if (arg == "--machine-uuid" && i + 1 < argc) {
        launch.machine_uuid = argv[++i];
        if (!is_valid_uuid(launch.machine_uuid)) {
            return usage_error("--machine-uuid must be a valid UUID");
        }
    } else if (arg == "--machine-ordinal") {
        if (i + 1 >= argc) {
            return usage_error("--machine-ordinal requires a positive integer");
        }
        const std::string value = argv[++i];
        unsigned long ordinal = 0;
        const bool digits = !value.empty() &&
            value.find_first_not_of("0123456789") == std::string::npos &&
            value.size() <= 9;
        if (digits) {
            ordinal = std::stoul(value);
        }
        if (!digits || ordinal == 0) {
            return usage_error("--machine-ordinal must be a positive integer, got '" + value +
                               "'");
        }
        launch.machine_ordinal = static_cast<unsigned>(ordinal);
    } else if (arg == "--allow-shutdown") {
        launch.allow_shutdown = true;
    } else if (arg == "--advertise") {
        launch.advertise = true;
    } else {
        return {};
    }
    return {true, std::nullopt};
}

// An option that sets the board's switches, links or clock, which no preset
// has a form for yet: it writes only `board`.
template<typename MachineType>
OptionParse parse_board_option(const std::string& arg, int& i, int argc, char* argv[],
                               BoardSettings<MachineType>& board) {
    auto usage_error = [](const std::string& message) {
        std::cerr << "Error: " << message << "\n";
        return OptionParse{true, ExitCode::USAGE};
    };
    if (arg == "--write-protect" && i + 1 < argc) {
        board.launch_protections.emplace_back(argv[++i], ProtectionKind::WriteProtect);
    } else if (arg == "--hide" && i + 1 < argc) {
        board.launch_protections.emplace_back(argv[++i], ProtectionKind::Hide);
    } else if (arg == board_rtc_flag<typename MachineType::Memory>() && i + 1 < argc) {
        try {
            board.board_rtc = parse_board_rtc_arg(argv[++i], arg);
        } catch (const std::runtime_error& e) {
            return usage_error(e.what());
        }
    } else if (arg == "--motherboard-link" && i + 1 < argc) {
        // KEY=VALUE; key/value strings interpreted by the machine
        // variant's MotherboardLinks::parse method.
        std::string spec = argv[++i];
        auto eq = spec.find('=');
        if (eq == std::string::npos) {
            return usage_error("--motherboard-link expects KEY=VALUE, got '" + spec + "'");
        }
        std::string key = spec.substr(0, eq);
        std::string value = spec.substr(eq + 1);
        if (auto err = board.motherboard_links.parse(key, value)) {
            return usage_error(*err);
        }
    } else {
        return {};
    }
    return {true, std::nullopt};
}

// An option that describes the machine, which a preset records: it writes
// only `machine`. An extension's --<cli-name> flag is one, found by
// `cli_name_to_manifest`. A file reference is recorded as what it means where
// the command line is written, `container_dirpath` (command_line_reference).
inline OptionParse parse_machine_option(
        const std::string& arg, int& i, int argc, char* argv[], MachineDescription& machine,
        const std::map<std::string, const beebium::ExtensionManifest*>& cli_name_to_manifest,
        const std::filesystem::path& container_dirpath) {
    auto reference = [&](const std::string& value) {
        return command_line_reference(value, container_dirpath);
    };
    auto usage_error = [](const std::string& message) {
        std::cerr << "Error: " << message << "\n";
        return OptionParse{true, ExitCode::USAGE};
    };
    if (arg == "--mos" && i + 1 < argc) {
        machine.mos_filepath = reference(argv[++i]);
    } else if (arg == "--language-rom" && i + 1 < argc) {
        machine.language_rom_filepath = reference(argv[++i]);
    } else if (arg == "--sideways" && i + 1 < argc) {
        std::string value = argv[++i];
        SidewaysConfig sideways_config;
        try {
            sideways_config = parse_sideways_arg(value);
        } catch (const std::runtime_error& e) {
            return usage_error(e.what());
        }
        if (!sideways_config.image_filepath.empty()) {
            sideways_config.image_filepath = reference(sideways_config.image_filepath);
        }
        machine.sideways_configs.push_back(sideways_config);
        // Add ALL slot types to rom_slots to prevent default ROM loading
        // (e.g. type=empty or type=ram prevents the default BASIC loading)
        if (sideways_config.type == SidewaysSlotType::Rom) {
            machine.rom_slots[sideways_config.slot] = sideways_config.image_filepath;
        } else if (sideways_config.type == SidewaysSlotType::Empty) {
            machine.rom_slots[sideways_config.slot] = EMPTY_SLOT_MARKER;
        } else if (sideways_config.type == SidewaysSlotType::Ram) {
            machine.rom_slots[sideways_config.slot] = RAM_SLOT_MARKER;
        }
    } else if (arg == "--floppy" && i + 1 < argc) {
        std::string value = argv[++i];
        complete_colon_arg(value, i, argc, argv);
        try {
            auto [drive, filepath] = parse_floppy_arg(value);
            machine.floppy_filepaths[drive] =
                filepath.find("://") == std::string::npos ? reference(filepath) : filepath;
        } catch (const std::runtime_error& e) {
            return usage_error(e.what());
        }
    } else if (arg == "--screen-mode" && i + 1 < argc) {
        try {
            machine.screen_mode = parse_int(argv[++i], "--screen-mode");
        } catch (const std::runtime_error& e) {
            return usage_error(e.what());
        }
        if (machine.screen_mode < 0 || machine.screen_mode > 7) {
            return usage_error("--screen-mode must be 0-7");
        }
        machine.screen_mode_set = true;
    } else if (arg == "--auto-boot") {
        machine.auto_boot = true;
        machine.auto_boot_set = true;
    } else if (arg == "--links" && i + 1 < argc) {
        try {
            machine.raw_links = parse_int(argv[++i], "--links");
        } catch (const std::runtime_error& e) {
            return usage_error(e.what());
        }
        if (machine.raw_links < 0 || machine.raw_links > 255) {
            return usage_error("--links must be 0-255");
        }
    } else if (arg == "--speed" || arg.rfind("--speed=", 0) == 0) {
        std::string speed_value;
        if (arg == "--speed") {
            if (i + 1 >= argc) {
                return usage_error("--speed requires a value "
                                   "(a positive multiplier or 'unlimited')");
            }
            speed_value = argv[++i];
        } else {
            speed_value = arg.substr(8);  // Skip "--speed="
        }
        try {
            machine.speed_multiplier_override = parse_speed_arg(speed_value);
        } catch (const std::runtime_error& e) {
            return usage_error(e.what());
        }
    } else if (arg == "--fdc" && i + 1 < argc) {
        machine.fdc_type = argv[++i];
    } else if (arg == "--station" && i + 1 < argc) {
        std::string error;
        auto spec = beebium::econet::parse_station_spec(argv[++i], error);
        if (!spec.has_value()) {
            return usage_error("--station " + error);
        }
        if (spec->is_auto) {
            machine.station_auto_range = spec->range;
            machine.station_number = -1;  // chosen at launch, in install_econet
        } else {
            machine.station_number = spec->fixed;
            machine.station_auto_range.reset();
        }
    } else if (arg == "--machine-name" && i + 1 < argc) {
        machine.machine_name = argv[++i];
    } else {
        // An extension CLI flag (e.g. --acorn-scsi, --scsi-hdd)?
        std::string lower = arg;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        auto manifest_it = cli_name_to_manifest.find(lower);
        if (manifest_it == cli_name_to_manifest.end()) {
            return {};
        }
        const auto* manifest = manifest_it->second;
        std::string arg_string;
        // Consume the next argv as the colon-separated argument string
        // (if it exists and doesn't start with --)
        if (i + 1 < argc && std::string_view(argv[i + 1]).substr(0, 2) != "--") {
            arg_string = argv[++i];
        }
        auto parse_result = beebium::parse_extension_args(
            manifest->effective_cli_name(), arg_string, manifest->parameters);
        if (!parse_result.ok) {
            return usage_error(parse_result.error);
        }
        // A file path parameter: a resource ("bundled") as what it means; a
        // location anchored only, a bare one being the extension's own (a
        // keyword such as "none").
        for (const auto& p : manifest->parameters) {
            auto it = parse_result.config.find(p.key);
            if (p.type != "filepath" || it == parse_result.config.end()) continue;
            it->second = p.bundled.empty() ? anchor_reference(it->second, container_dirpath)
                                           : reference(it->second);
        }
        machine.extension_instances.push_back({
            manifest->name,
            std::move(parse_result.config),
            std::move(parse_result.list_config),
            /*from_preset=*/false,
            std::move(parse_result.explicit_keys)
        });
    }
    return {true, std::nullopt};
}

template<typename MachineType>
std::optional<int> parse_start_arguments(int argc, char* argv[], int start_index,
                                          ServerConfig<MachineType>& config,
                                          StartOptionScope scope = StartOptionScope::Everything) {
    using Memory = typename MachineType::Memory;

    // First pass: find --preset and --extension-dir before other options
    // --preset is loaded first so CLI can override; --extension-dir is needed
    // to scan manifests so extension CLI flags can be recognised in second pass
    for (int i = start_index; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--extension-dir" && i + 1 < argc) {
            config.launch.extension_dirpaths.push_back(argv[i + 1]);
            ++i;
            continue;
        }
        if (arg == "--preset" && i + 1 < argc) {
            if (config.launch.preset_filepath) {
                std::cerr << "Error: --preset may be given only once\n";
                return ExitCode::USAGE;
            }
            // The argument is a preset file path or, failing that, a preset id
            // resolved like show-preset: system presets, then user presets.
            std::string preset_arg = argv[++i];
            config.launch.preset_filepath = PresetPaths::locate(preset_arg);
            if (!config.launch.preset_filepath) {
                std::cerr << "Error: " << PresetPaths::not_found_message(preset_arg) << "\n";
                return ExitCode::NOINPUT;
            }
            auto result = load_preset(*config.launch.preset_filepath);
            if (!result) {
                for (const auto& e : result.errors()) {
                    std::cerr << "Error: " << e.message << "\n";
                }
                return result.unreadable ? ExitCode::NOINPUT : ExitCode::CONFIG;
            }

            if (auto err = preset_model_error<Memory>(*result.config)) {
                std::cerr << "Error: " << *err << "\n";
                return ExitCode::CONFIG;
            }

            // Apply preset as baseline configuration
            apply_preset(config, *result.config);
        }
    }

    // Build the extension resolver: built-ins (lowest priority), the
    // auto-resolved default <exe-dir>/extensions, then each user-
    // supplied --extension-dir in CLI order (later wins).
    if (auto err = populate_extension_resolver(argv[0], config.launch.extension_dirpaths, config)) {
        return err;
    }

    // Lowercase CLI flag map driven by the resolved manifest set.
    auto cli_name_to_manifest = config.launch.extension_resolver.cli_name_map();

    // Second pass: parse all CLI arguments (including --preset which we skip now)
    for (int i = start_index; i < argc; ++i) {
        std::string arg = argv[i];

        if ((arg == "--preset" || arg == "--extension-dir") && i + 1 < argc) {
            // Already handled in first pass, skip
            ++i;
            continue;
        } else if (arg == "--help" || arg == "-h") {
            print_usage<MachineType>(argv[0]);
            return ExitCode::OK;
        }

        // Each option is parsed by the one parser whose part of the
        // configuration it sets. With MachineOnly (create-preset), an option
        // the launch or board parser recognises is refused: the preset could
        // not record it.
        if (scope == StartOptionScope::MachineOnly) {
            const std::string flag = arg.substr(0, arg.find('='));
            LaunchSettings launch_scratch;
            BoardSettings<MachineType> board_scratch;
            int j = i;
            if (parse_launch_option(arg, j, argc, argv, launch_scratch).recognised) {
                std::cerr << "Error: " << flag << " configures the launch, not the machine; "
                             "a preset cannot record it\n";
                return ExitCode::USAGE;
            }
            j = i;
            if (parse_board_option<MachineType>(arg, j, argc, argv, board_scratch).recognised) {
                std::cerr << "Error: " << flag << " has no preset form yet\n";
                return ExitCode::USAGE;
            }
        } else {
            auto launch = parse_launch_option(arg, i, argc, argv, config.launch);
            if (launch.exit_code) return launch.exit_code;
            if (launch.recognised) continue;
            auto board = parse_board_option<MachineType>(arg, i, argc, argv, config.board);
            if (board.exit_code) return board.exit_code;
            if (board.recognised) continue;
        }
        auto machine = parse_machine_option(arg, i, argc, argv, config.machine,
                                            cli_name_to_manifest,
                                            config.launch.reference_container_dirpath());
        if (machine.exit_code) return machine.exit_code;
        if (!machine.recognised) {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage<MachineType>(argv[0]);
            return ExitCode::USAGE;
        }
    }

    // Fold preset-supplied sideways slots in as a baseline now that all CLI
    // --sideways flags have been parsed (CLI wins per slot).
    bool fold_failed = false;
    for (const auto& d : fold_in_preset(config)) {
        std::cerr << (d.is_error() ? "Error: " : "Warning: ") << d.message << "\n";
        fold_failed = fold_failed || d.is_error();
    }
    if (fold_failed) {
        return ExitCode::CONFIG;
    }

    return std::nullopt;  // Success, continue execution
}

// The value an extension uses for a parameter when none is given, on this
// host: the default, or, for a parameter naming a host-resolved default in
// default_from, what that resolves to here. A default_from this server does
// not know leaves the default.
inline std::string effective_default(const beebium::ParameterSchema& p) {
    if (p.default_from == "aun-map-file") return beebium::default_aun_map_filepath().string();
    return p.default_value;
}

} // namespace beebium::server
