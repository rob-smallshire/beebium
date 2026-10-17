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

// ServerConfig: everything a launch is configured with, gathered from the
// command line and a preset before a machine is built. Separate from
// ServerMain.hpp so the preset machinery (PresetResolution.hpp) and its tests
// need none of the server's service and machine-running code.

#include "beebium/SlotProtection.hpp"
#include "beebium/econet/StationSelection.hpp"
#include "beebium/extension/ExtensionManifest.hpp"
#include "beebium/extension/ExtensionResolver.hpp"
#include "beebium/server/CliArgParsers.hpp"
#include "beebium/server/PresetLoader.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace beebium::server {

// Types used by ServerConfig (must be outside anonymous namespace to avoid -Wsubobject-linkage)
constexpr uint16_t DEFAULT_GRPC_PORT = 0xBEEB;  // 48875

// Sentinel values to mark slots that should not have default ROMs loaded
inline constexpr const char* EMPTY_SLOT_MARKER = "\x01EMPTY\x01";
inline constexpr const char* RAM_SLOT_MARKER = "\x01RAM\x01";

// An extension instance from a --<cli-name> flag or from a preset's
// extensions and econet.transport. By the end of parse_start_arguments every
// instance, whichever its source, has been normalised against its manifest
// (defaults filled, required and types checked, list parameters in
// list_config) and the preset's merged with the CLI's.
struct ExtensionInstance {
    std::string name;                                               // canonical extension name
    std::map<std::string, std::string> config;                      // parsed KV pairs (scalar params)
    std::map<std::string, std::vector<std::string>> list_config;    // list params (is_list=true)
    bool from_preset = false;                                       // supplied by a preset, overridable by the CLI
    std::set<std::string> explicit_keys{};                          // keys the source gave (not defaulted) (#166)
    std::string preset_pointer{};                                   // JSON pointer of the preset entry it came from
    // A preset instance's list items as the preset gave them, each a
    // string or an object of fields: normalised into list_config, and
    // written back in the form given.
    std::map<std::string, std::vector<beebium::ConfigListItem>> preset_list_items{};
};

// The machine as a preset records it: everything preset_document writes and
// nothing else. A start option that sets one of these is a machine option,
// which create-preset records; parse_machine_option is the only option
// parser that can write here.
struct MachineDescription {
    // ROM configuration
    std::string mos_filepath;
    // Optional override for the default language ROM image. When set, it is
    // loaded into the machine's own DEFAULT_LANGUAGE_SLOT instead of the
    // built-in DEFAULT_LANGUAGE_ROM, so callers can supply a custom language
    // ROM without knowing which slot this machine variant uses for it.
    std::string language_rom_filepath;
    std::map<uint8_t, std::string> rom_slots;
    // Sideways slot configuration (type, image, and the RAM write-protect
    // switch's power-on position). The switch is also runtime-toggleable via
    // SidewaysService.SetSlotProtection (its slot-N write group); write_protected
    // here is just its launch state.
    std::vector<SidewaysConfig> sideways_configs;

    // Disc configuration
    std::array<std::string, 2> floppy_filepaths;
    std::string fdc_type;

    // Econet configuration
    int station_number = -1;                          // -1 = Econet not fitted
    // When the station is requested as `auto[:lo-hi]` (issue #67) the number is
    // chosen at launch by the transport, not now: station_number stays -1 and
    // resolve_auto_station turns this range into the number install_econet fits.
    std::optional<beebium::econet::StationRange> station_auto_range;
    // Both AUN (built-in) and Piconet (plugin) are selected via the
    // generic extension dispatch:
    //
    //   --aun     port=...:map=...
    //   --piconet device_path=/dev/ttyX
    //
    // There are no transport-specific fields; the values live in the relevant
    // ExtensionInstance::config map and are consumed by the corresponding
    // EconetTransportExtension::create_backend.

    // Startup options (keyboard links)
    // -1 means not set; we use int to detect if user specified a value
    int screen_mode = -1;
    bool auto_boot = false;
    int raw_links = -1;
    bool screen_mode_set = false;
    bool auto_boot_set = false;

    // Optional runtime speed override applied before PacingClock construction.
    std::optional<double> speed_multiplier_override;

    // The label the machine carries.
    std::string machine_name;

    std::vector<ExtensionInstance> extension_instances;
};

// Settings of the machine's board that no preset has a form for yet: start
// options create-preset refuses ("no preset form yet"). parse_board_option is
// the only option parser that can write here.
template<typename MachineType>
struct BoardSettings {
    // Protection groups to engage at launch, from --write-protect <group> /
    // --hide <group>: a group id (see GetSlotStatus.protection_groups) and
    // the control to engage. Applied after slot config via
    // Memory::set_protection. Expresses the board-wide / hide switches that the
    // per-slot --sideways ...:write-protect flag cannot (e.g. Watford S1/S2).
    std::vector<std::pair<std::string, ProtectionKind>> launch_protections;

    // The board's built-in real-time clock (e.g. --integra-rtc): its time
    // source and start time. Only machines declaring BOARD_RTC_OPTION accept it.
    std::optional<BoardRtcConfig> board_rtc;

    // Motherboard link state (jumpers that affect sideways slot mapping).
    // Default-constructed to factory positions. For machine variants with no
    // such links this is EmptyMotherboardLinks and accepts no assignments.
    typename MachineType::Memory::MotherboardLinks motherboard_links{};
};

// How this one launch runs, and where it finds what it loads: never part of
// the machine, so never in a preset. A start option that sets one of these
// configures the launch, which create-preset refuses; parse_launch_option is
// the only option parser that can write here (but for --preset and
// --extension-dir, which name create-preset's inputs and are read first).
struct LaunchSettings {
    // Network
    uint16_t port = DEFAULT_GRPC_PORT;

    // Wait mode for controlled startup
    WaitMode wait_mode = WaitMode::None;

    // Provenance
    std::string provenance_type;
    std::string provenance_uuid;
    std::string provenance_version;

    // Machine identity
    std::string machine_uuid;

    // Shutdown policy
    bool allow_shutdown = false;

    // mDNS advertisement
    bool advertise = false;

    // The preset launched from, as opened
    std::optional<PresetSource> preset;
    // The launching preset's display name ("" without a preset), for the
    // machine-preset name placeholder.
    std::string preset_name;
    // The launcher's instance number for this machine (--machine-ordinal),
    // for the machine-ordinal name placeholder.
    std::optional<unsigned> machine_ordinal;

    // Where ROMs given by bare name are looked for first (--rom-dir).
    std::string rom_dirpath;

    // Extension configuration: search paths added via --extension-dir, in order.
    // Later paths override earlier ones (and the auto-resolved default
    // <exe-dir>/extensions) when manifests share the same cli_name.
    std::vector<std::string> extension_dirpaths;

    // Auto-resolved default extension directory (<exe-dir>/extensions),
    // populated during parse_start_arguments if it exists. Lower priority
    // than any explicit --extension-dir paths.
    std::optional<std::string> default_extension_dirpath;

    // Internal: built-in manifests and per-source scan results, owned so
    // that ExtensionResolver's pointers stay valid for the lifetime of
    // the settings. Populated by parse_start_arguments; consumed by
    // run_server.
    std::vector<beebium::ExtensionManifest> builtin_manifests;
    std::vector<std::vector<beebium::ExtensionManifest>> extension_scan_results;
    beebium::ExtensionResolver extension_resolver;

    // The directory this launch's file references are written in: the
    // preset's, when launched from one (values given on the command line
    // beside --preset resolve as if written in it), else the working directory.
    std::filesystem::path reference_container_dirpath() const {
        return preset ? preset->container_dirpath() : std::filesystem::current_path();
    }
};

// Everything a launch is configured with, gathered from the command line and
// a preset: the machine a preset records, the board settings it has no form
// for, and the launch's own settings, each its own type so that what may
// reach a preset is decided by type, not by a list of flags.
template<typename MachineType>
struct ServerConfig {
    using ExtensionInstance = beebium::server::ExtensionInstance;

    MachineDescription machine;
    BoardSettings<MachineType> board;
    LaunchSettings launch;

    // A preset's values that merge into `machine` after the CLI's, which win:
    // its sideways slots (folded per slot at the end of
    // parse_start_arguments) and its startup links (merge_preset_startup).
    std::vector<SidewaysConfig> preset_sideways_configs;
    std::optional<PresetStartupConfig> preset_startup;
};

} // namespace beebium::server
