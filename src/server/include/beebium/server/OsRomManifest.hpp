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


// What the OS ROMs the server knows are called, by the SHA-1 of their image,
// and the description of a fitted OS ROM that SidewaysService.GetOsRom
// reports. An image the manifest does not know is named by its file.

#pragma once

#include "beebium/Sha1.hpp"
#include "beebium/server/ResourcePaths.hpp"
#include "beebium/OsRom.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace beebium {

struct OsRomEntry {
    std::string_view sha1;   // 40 lower-case hex digits
    std::string_view title;
};

inline constexpr std::array<OsRomEntry, 4> OS_ROMS = {{
    {"0d9bcaf6a393c9ce2359ed700ddb53c232c2c45d", "Acorn MOS 1.20"},
    {"d9426dd7454ca199cdef9435daa332cdf405c965", "Acorn MOS 2.0"},
    {"04d5876999f93fc61a18f172b1fbb3b75db733ac", "Acorn MOS 3.20"},
    {"3b6f5a82a4581da9a0a4ccd203571141720ddbd2", "Acorn MOS 3.50"},
}};

inline const OsRomEntry* find_os_rom(std::span<const std::uint8_t> image) {
    const std::string digest = sha1_hex(image);
    for (const OsRomEntry& rom : OS_ROMS) {
        if (rom.sha1 == digest) return &rom;
    }
    return nullptr;
}

// The OS ROM a machine was given as `reference` (a bare name or a path),
// found at `filepath`, holding `image`.
inline OsRom describe_os_rom(const std::string& reference,
                                      const std::filesystem::path& filepath,
                                      std::span<const std::uint8_t> image) {
    OsRom os_rom;
    os_rom.image_uri = std::filesystem::absolute(filepath).lexically_normal().string();
    if (server::is_bare_reference(reference)) {
        constexpr std::string_view ROM_EXTENSION = ".rom";
        os_rom.rom_name = reference;
        if (os_rom.rom_name.size() > ROM_EXTENSION.size() &&
            os_rom.rom_name.ends_with(ROM_EXTENSION)) {
            os_rom.rom_name.resize(os_rom.rom_name.size() - ROM_EXTENSION.size());
        }
    }
    os_rom.size_bytes = static_cast<std::uint32_t>(image.size());
    const OsRomEntry* known = find_os_rom(image);
    os_rom.title = known ? std::string(known->title) : filepath.stem().string();
    return os_rom;
}

}  // namespace beebium
