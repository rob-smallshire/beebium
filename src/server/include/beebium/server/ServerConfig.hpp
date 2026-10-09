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

// Configuration struct for server initialization
// Aggregates all parsed command-line arguments
template<typename MachineType>
struct ServerConfig {
    // ROM configuration
    std::string mos_filepath;
    // Optional override for the default language ROM image. When set, it is
    // loaded into the machine's own DEFAULT_LANGUAGE_SLOT instead of the
    // built-in DEFAULT_LANGUAGE_ROM, so callers can supply a custom language
    // ROM without knowing which slot this machine variant uses for it.
    std::string language_rom_filepath;
    std::string rom_dirpath;
    std::map<uint8_t, std::string> rom_slots;
    // Sideways slot configuration (type, image, and the RAM write-protect
    // switch's power-on position). The switch is also runtime-toggleable via
    // SidewaysService.SetSlotProtection (its slot-N write group); write_protected
    // here is just its launch state.
    std::vector<SidewaysConfig> sideways_configs;
    // Sideways slots supplied by a --preset, applied as a baseline after CLI
    // parsing. A CLI --sideways for the same slot takes precedence. See the
    // merge step at the end of parse_start_arguments().
    std::vector<SidewaysConfig> preset_sideways_configs;
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

    // Network
    uint16_t port = DEFAULT_GRPC_PORT;

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
    // ServerConfig has no transport-specific fields; the values live
    // in the relevant ExtensionInstance::config map and are consumed
    // by the corresponding EconetTransportExtension::create_backend.

    // Startup options (keyboard links)
    // -1 means not set; we use int to detect if user specified a value
    int screen_mode = -1;
    bool auto_boot = false;
    int raw_links = -1;
    bool screen_mode_set = false;
    bool auto_boot_set = false;
    // The preset's startup links, folded in after the CLI's by
    // merge_preset_startup (the CLI wins).
    std::optional<PresetStartupConfig> preset_startup;

    // Wait mode for controlled startup
    WaitMode wait_mode = WaitMode::None;

    // Optional runtime speed override applied before PacingClock construction.
    std::optional<double> speed_multiplier_override;

    // Provenance
    std::string provenance_type;
    std::string provenance_uuid;
    std::string provenance_version;

    // Machine identity
    std::string machine_uuid;
    std::string machine_name;

    // Shutdown policy
    bool allow_shutdown = false;

    // mDNS advertisement
    bool advertise = false;

    // Preset file path
    std::optional<std::filesystem::path> preset_filepath;
    // The launching preset's display name ("" without a preset), for the
    // machine-preset name placeholder.
    std::string preset_name;
    // The launcher's instance number for this machine (--machine-ordinal),
    // for the machine-ordinal name placeholder. Launch state, not preset
    // state: a preset cannot set it.
    std::optional<unsigned> machine_ordinal;

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
    // ServerConfig. Populated by parse_start_arguments; consumed by
    // run_server.
    std::vector<beebium::ExtensionManifest> builtin_manifests;
    std::vector<std::vector<beebium::ExtensionManifest>> extension_scan_results;
    beebium::ExtensionResolver extension_resolver;

    // Extension instances from --<cli-name> flags and from the preset's
    // extensions and econet.transport. By the end of parse_start_arguments
    // every instance, whichever its source, has been normalised against its
    // manifest (defaults filled, required and types checked, list parameters
    // in list_config) and the preset's merged with the CLI's.
    struct ExtensionInstance {
        std::string name;                                               // canonical extension name
        std::map<std::string, std::string> config;                      // parsed KV pairs (scalar params)
        std::map<std::string, std::vector<std::string>> list_config;    // list params (is_list=true)
        bool from_preset = false;                                       // supplied by a preset, overridable by the CLI
        std::set<std::string> explicit_keys{};                          // keys the source gave (not defaulted) (#166)
        std::string preset_pointer{};                                   // JSON pointer of the preset entry it came from
    };
    std::vector<ExtensionInstance> extension_instances;
};

} // namespace beebium::server
