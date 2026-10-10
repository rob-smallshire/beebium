// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_EXTENSION_MANIFEST_HPP
#define BEEBIUM_EXTENSION_MANIFEST_HPP

#include "Export.hpp"
#include "ExtensionArgParser.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace beebium {

// A firmware ROM image a plugin ships in its own directory and declares in its
// manifest's `roms` array. The file lives at <manifest_dirpath>/roms/<filename>
// and is resolved through Extension::rom_filepath()/load_rom(). Coprocessor
// firmware belongs to the coprocessor, not the shared host ROM set.
struct RomImage {
    std::string key;          // stable identifier used by the extension (e.g. "client")
    std::string filename;     // basename within the plugin's roms/ directory
    std::uint64_t size = 0;   // expected image size in bytes
    std::string display_name; // human-readable name, e.g. "Acorn 6502 Tube ROM 1.10"
};

// How a configuration UI names the state of a ParameterGroup in which none of
// its parameters is given, e.g. "Host time" for a clock with no start time or
// offset.
struct ParameterGroupNone {
    std::string label;
    std::string description;
};

// Parameters that are alternatives to one another: at most one may be given.
// A configuration UI presents the group as one choice -- the none state (if
// any) and then each member in order -- and shows the chosen member's field.
// The group is where the alternatives are declared: each member's `excludes`
// gains the other members when the manifest is read (apply_parameter_groups).
// A parameter belongs to at most one group, and a member is neither required
// nor defaulted, since leaving every member out must stay possible.
struct ParameterGroup {
    std::string key;                       // stable identifier, e.g. "start"
    std::string label;                     // e.g. "Start time"
    std::string description;
    std::vector<std::string> parameters;   // member parameter keys, in display order
    std::optional<ParameterGroupNone> none;  // absent: no state is named for none given
};

// Metadata describing a peripheral extension.
// For dynamically loaded extensions, this is read from manifest.json.
// For built-in extensions, this is constructed programmatically.
// The manifest is the single source of truth for extension metadata.
struct BEEBIUM_EXT_API ExtensionManifest {
    std::string name{};           // e.g. "scsi-hard-disc" (canonical type name)
    std::string display_name{};   // short noun-phrase used as the default per-instance label
    std::string description{};    // human-readable description (longer prose / catalogue line)
    std::string library_stem{};   // shared library filename stem (platform adds suffix)
    std::string cli_name{};       // short CLI alias (e.g. "scsi-hdd"); defaults to name
    std::string extension_kind = "peripheral";  // "peripheral" | "econet-transport"
    std::filesystem::path manifest_dirpath{};  // directory containing manifest.json (empty for built-in)
    std::vector<ParameterSchema> parameters{}; // parameter schema for CLI/preset/gRPC
    std::vector<ParameterGroup> groups{};      // parameters that are alternatives to one another
    std::vector<std::string> provides{};       // extension points this extension creates (e.g. ["scsi"])
    std::vector<std::string> attaches_to{};    // extension points this extension attaches to (e.g. ["serial-port"]); may be several
    std::vector<RomImage> roms{};              // firmware images shipped in the plugin's roms/ directory (plugins only)

    // Effective CLI name: cli_name if set, otherwise name
    std::string_view effective_cli_name() const {
        return cli_name.empty() ? std::string_view(name) : std::string_view(cli_name);
    }
};

// Check the manifest's groups and give each member's `excludes` the group's
// other members (those it does not already name). Returns why the groups are
// malformed -- a group without a key, two groups with one key, a member that
// is not a parameter, a parameter in two groups, a required or defaulted
// member -- or nullopt when they are sound. Every manifest passes through
// this once, as it is read or built.
BEEBIUM_EXT_API std::optional<std::string> apply_parameter_groups(ExtensionManifest& manifest);

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_MANIFEST_HPP
