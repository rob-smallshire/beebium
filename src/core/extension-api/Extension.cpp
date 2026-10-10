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

// Vtable anchor + destructor definition for Extension. Class-level
// BEEBIUM_EXT_API was dropped so consumer DLLs (notably
// beebium_extension_ui_proto, which compiles ExtensionUiServiceImpl)
// can inline the small accessors without resolving cross-DLL imports.
// The out-of-line destructor here keeps the vtable anchored in
// beebium_extension_api.

#include "beebium/extension/Extension.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace beebium {

Extension::~Extension() = default;

void validate_rom_image(const std::filesystem::path& path,
                        std::uint64_t expected_size) {
    // A ROM image is the device's contents: exactly the declared size, with no
    // content rule for any part of it. There is no padded-dump or half-size
    // acceptance -- a file of any other size is a different (or fragmentary)
    // image, and synthesising the missing part would be a guess.
    std::error_code ec;
    const std::uint64_t actual = std::filesystem::file_size(path, ec);
    if (ec) {
        throw std::runtime_error("ROM image not found at " + path.string());
    }
    if (actual != expected_size) {
        throw std::runtime_error(
            "ROM image " + path.string() + " is " + std::to_string(actual)
            + " bytes; expected exactly " + std::to_string(expected_size));
    }
}

void read_rom_image(const std::filesystem::path& path,
                    std::span<std::uint8_t> dest) {
    validate_rom_image(path, dest.size());

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open ROM image " + path.string());
    }
    file.read(reinterpret_cast<char*>(dest.data()),
              static_cast<std::streamsize>(dest.size()));
    if (static_cast<std::uint64_t>(file.gcount()) != dest.size()) {
        throw std::runtime_error("Short read on ROM image " + path.string());
    }

    std::cout << "  ROM " << path.filename().string() << ": "
              << std::to_string(dest.size()) << "-byte image\n";
}

std::filesystem::path Extension::rom_filepath(std::string_view key) const {
    for (const auto& rom : manifest_.roms) {
        if (rom.key == key) {
            return manifest_.manifest_dirpath / "roms" / rom.filename;
        }
    }
    throw std::runtime_error(
        "Extension '" + manifest_.name + "' declares no ROM with key '"
        + std::string(key) + "'");
}

void Extension::load_rom(std::string_view key, std::span<std::uint8_t> dest) const {
    const RomImage* entry = nullptr;
    for (const auto& rom : manifest_.roms) {
        if (rom.key == key) {
            entry = &rom;
            break;
        }
    }
    if (!entry) {
        throw std::runtime_error(
            "Extension '" + manifest_.name + "' declares no ROM with key '"
            + std::string(key) + "'");
    }
    if (dest.size() != entry->size) {
        throw std::runtime_error(
            "Extension '" + manifest_.name + "' ROM '" + std::string(key)
            + "': buffer is " + std::to_string(dest.size())
            + " bytes but the manifest declares " + std::to_string(entry->size));
    }
    read_rom_image(rom_filepath(key), dest);
}

std::optional<std::string> apply_parameter_groups(ExtensionManifest& manifest) {
    auto find_parameter = [&](const std::string& key) -> ParameterSchema* {
        for (auto& p : manifest.parameters) {
            if (p.key == key) return &p;
        }
        return nullptr;
    };
    std::vector<std::string> group_keys;
    std::vector<std::string> grouped;
    for (const auto& group : manifest.groups) {
        const std::string what = "group '" + group.key + "'";
        if (group.key.empty()) return std::string("a group has no key");
        if (std::find(group_keys.begin(), group_keys.end(), group.key) != group_keys.end()) {
            return "two groups have the key '" + group.key + "'";
        }
        group_keys.push_back(group.key);
        for (const auto& key : group.parameters) {
            const auto* p = find_parameter(key);
            if (!p) return what + " names '" + key + "', which is not a parameter";
            if (std::find(grouped.begin(), grouped.end(), key) != grouped.end()) {
                return "parameter '" + key + "' is in more than one group";
            }
            grouped.push_back(key);
            if (p->required || !p->default_value.empty()) {
                return what + " member '" + key + "' is required or has a default, so it "
                       "could not be left out";
            }
        }
    }
    for (const auto& group : manifest.groups) {
        for (const auto& key : group.parameters) {
            auto& excludes = find_parameter(key)->excludes;
            for (const auto& other : group.parameters) {
                if (other != key &&
                    std::find(excludes.begin(), excludes.end(), other) == excludes.end()) {
                    excludes.push_back(other);
                }
            }
        }
    }
    return std::nullopt;
}

std::string make_extension_id(
    std::string_view manifest_name,
    std::span<const std::string> existing_ids) {
    auto is_taken = [&](std::string_view candidate) {
        return std::any_of(
            existing_ids.begin(), existing_ids.end(),
            [&](const std::string& id) { return id == candidate; });
    };

    std::string candidate(manifest_name);
    if (!is_taken(candidate)) {
        return candidate;
    }
    for (std::size_t n = 1; ; ++n) {
        candidate = std::string(manifest_name) + "-" + std::to_string(n);
        if (!is_taken(candidate)) {
            return candidate;
        }
    }
}

}  // namespace beebium
