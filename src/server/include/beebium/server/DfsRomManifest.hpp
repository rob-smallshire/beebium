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

#include "beebium/Sha1.hpp"
#include "beebium/disc/DiscConcepts.hpp"
#include "beebium/disc/DiscControllerRegistry.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace beebium {

// A disc filing system ROM whose disc controller is known: Acorn DFS drives
// one controller and does nothing useful with the other. The 0.9x and 1.xx
// releases are 8271-only and the 2.xx releases 1770-only (J.G. Harston's DFS
// release table, mdfs.net/System/ROMs/Filing/Disk/Acorn/versions; see
// docs/discussion/intel-8271-study.md, section 6.1). A ROM is known by its
// contents, so a renamed image is still recognised and an unknown one is not
// judged.
struct DfsRom {
    std::string_view sha1;       // 40 lower-case hex digits
    std::string_view name;
    std::string_view fdc_chip;   // as DiscControllerInterface::fdc_chip() names it
};

inline constexpr std::array<DfsRom, 6> DFS_ROMS = {{
    {"5cc0f14b8f46855c70eaa653cca4ad079b458732", "Acorn DFS 0.90", "8271"},
    {"6df3552d5426f3a4625b9a0c7829bdba03f05e84", "Acorn DFS 0.98", "8271"},
    {"161f585dc45665ea77433c84afd2f95049f7f5a0", "Acorn DFS 1.00 (NFS 3.40)", "8271"},
    {"7e3c536baeae84d6498a14e8405319e01ee78232", "Acorn DFS 1.20 (NFS 3.60)", "8271"},
    {"822c333526d08ad4582ac0360344210ce4e3f0fb", "Acorn DFS 1.22 (NFS 3.62)", "8271"},
    {"cf2ebc422a8d24ec6f1a0320520c38a0e704109a", "Acorn DFS 2.26", "WD1770"},
}};

// The known DFS this image is, or nullptr
inline const DfsRom* find_dfs_rom(std::span<const uint8_t> image) {
    const std::string digest = sha1_hex(image);
    for (const DfsRom& rom : DFS_ROMS) {
        if (rom.sha1 == digest) return &rom;
    }
    return nullptr;
}

// What is wrong with fitting this DFS where `fitted_chip` is the machine's
// disc controller chip (empty for none), or nothing when they go together.
inline std::optional<std::string> dfs_controller_mismatch(const DfsRom& rom,
                                                          std::string_view fitted_chip) {
    if (rom.fdc_chip == fitted_chip) return std::nullopt;
    std::string fitted = fitted_chip.empty() ? "none" : "the " + std::string(fitted_chip);
    return std::string(rom.name) + " needs the " + std::string(rom.fdc_chip) +
           " disc controller; this machine has " + fitted;
}

// The disc controller chip a machine of this hardware has, given the socket's
// controller id (empty or "none" for an empty socket): empty for none.
template<typename Memory>
std::string fitted_fdc_chip(const std::string& fdc_type) {
    if constexpr (HasDiscControllerSocket<Memory>) {
        if (fdc_type.empty() || fdc_type == "none") return {};
        const auto* info = DiscControllerRegistry::find(fdc_type);
        return info ? std::string(info->fdc_chip) : std::string();
    } else if constexpr (HasDiscDrives<Memory>) {
        return "WD1770";   // the B+ family's built-in controller
    } else {
        return {};
    }
}

} // namespace beebium
