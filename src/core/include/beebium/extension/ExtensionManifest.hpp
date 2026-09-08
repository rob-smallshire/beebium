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
    std::string description;  // human-readable, shown by describe-extension
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
    std::vector<std::string> provides{};       // extension points this extension creates (e.g. ["scsi"])
    std::vector<std::string> attaches_to{};    // extension points this extension attaches to (e.g. ["serial-port"]); may be several
    std::vector<RomImage> roms{};              // firmware images shipped in the plugin's roms/ directory (plugins only)

    // Effective CLI name: cli_name if set, otherwise name
    std::string_view effective_cli_name() const {
        return cli_name.empty() ? std::string_view(name) : std::string_view(cli_name);
    }
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_MANIFEST_HPP
