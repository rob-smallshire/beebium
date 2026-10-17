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

// Where a saved machine keeps what its battery-backed units (BatteryBackedUnit)
// hold while it is switched off: one file per unit, <dirpath>/<unit name>.bin,
// in a machine's nvram/ (or an extension instance's own directory).
//
// A file is a header then the unit's contents, all integers little-endian:
//
//   8 bytes   "BEEBNVRM"
//   u32       the unit's layout version
//   u32, ...  the length of the hardware descriptor, then the descriptor
//   u64, ...  the length of the contents, then the contents
//
// A file is restored only into the same layout for the same hardware, and
// only if the unit takes it; anything else is moved to stale/ beside it, with
// one line saying why, and the unit keeps its seed. Nothing is discarded
// silently.

#pragma once

#include <beebium/BatteryBackedUnit.hpp>
#include <beebium/server/AtomicFile.hpp>
#include <beebium/server/MachineDocument.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

namespace nvram_detail {

inline constexpr std::string_view MAGIC = "BEEBNVRM";

inline void put(std::string& out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<char>(value >> (8 * i)));
}

// Reads `bytes` little-endian bytes at `at`, advancing it; nullopt past the end.
inline std::optional<std::uint64_t> get(const std::vector<std::uint8_t>& in, std::size_t& at,
                                        int bytes) {
    if (in.size() < at + bytes) return std::nullopt;
    std::uint64_t value = 0;
    for (int i = 0; i < bytes; ++i) value |= static_cast<std::uint64_t>(in[at + i]) << (8 * i);
    at += bytes;
    return value;
}

// The file a unit's contents are kept in.
inline std::filesystem::path filepath_of(const std::filesystem::path& dirpath,
                                         const BatteryBackedUnit& unit) {
    return dirpath / (std::string(unit.name()) + ".bin");
}

// Why the file `data` cannot be restored into `unit`, or "" when it can.
inline std::string misfit(const std::vector<std::uint8_t>& data, const BatteryBackedUnit& unit,
                          std::vector<std::uint8_t>& contents) {
    std::size_t at = 0;
    if (data.size() < MAGIC.size() ||
        std::string_view(reinterpret_cast<const char*>(data.data()), MAGIC.size()) != MAGIC) {
        return "it is not a saved unit";
    }
    at = MAGIC.size();
    const auto version = get(data, at, 4);
    const auto descriptor_length = get(data, at, 4);
    if (!version || !descriptor_length || data.size() < at + *descriptor_length) {
        return "it is cut short";
    }
    const std::string descriptor(data.begin() + at, data.begin() + at + *descriptor_length);
    at += *descriptor_length;
    const auto contents_length = get(data, at, 8);
    if (!contents_length || data.size() != at + *contents_length) return "it is cut short";
    if (*version != unit.layout_version()) {
        return "it was saved in layout " + std::to_string(*version) + ", not " +
               std::to_string(unit.layout_version());
    }
    if (descriptor != unit.hardware_descriptor()) {
        return "it was saved for other hardware (" + descriptor + ")";
    }
    contents.assign(data.begin() + at, data.end());
    return "";
}

}  // namespace nvram_detail

// Saves every unit's contents to <dirpath>/<name>.bin, each atomically.
// Throws when one cannot be written.
inline void save_battery_units(const std::vector<BatteryBackedUnit*>& units,
                               const std::filesystem::path& dirpath) {
    if (units.empty()) return;
    std::filesystem::create_directories(dirpath);
    for (const auto* unit : units) {
        const auto contents = unit->serialise();
        const auto descriptor = unit->hardware_descriptor();
        std::string file(nvram_detail::MAGIC);
        nvram_detail::put(file, unit->layout_version(), 4);
        nvram_detail::put(file, descriptor.size(), 4);
        file += descriptor;
        nvram_detail::put(file, contents.size(), 8);
        file.append(contents.begin(), contents.end());
        write_text_atomically(nvram_detail::filepath_of(dirpath, *unit), file);
    }
}

// Restores every unit that has a file in `dirpath` from it; one without keeps
// its seed. A file that does not fit is moved to <dirpath>/stale/, renamed
// with the time, with one line on `warnings` saying why, and its unit keeps
// its seed. Returns the names of the units restored.
inline std::vector<std::string> restore_battery_units(const std::vector<BatteryBackedUnit*>& units,
                                                      const std::filesystem::path& dirpath,
                                                      std::ostream& warnings = std::cerr) {
    std::vector<std::string> restored;
    for (auto* unit : units) {
        const auto filepath = nvram_detail::filepath_of(dirpath, *unit);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(filepath, ec)) continue;
        std::vector<std::uint8_t> data;
        {
            std::ifstream in(filepath, std::ios::binary);
            data.assign(std::istreambuf_iterator<char>(in), {});
        }
        std::vector<std::uint8_t> contents;
        auto why = nvram_detail::misfit(data, *unit, contents);
        if (why.empty() && !unit->restore(contents)) why = "the unit cannot hold it";
        if (why.empty()) {
            restored.emplace_back(unit->name());
            continue;
        }
        auto stamp = iso8601_utc_now();
        for (auto& c : stamp) {
            if (c == ':') c = '-';
        }
        // Never over another file kept aside, even one from the same second.
        const auto stale_dirpath = dirpath / "stale";
        const auto stem = std::string(unit->name()) + "." + stamp;
        auto stale_filepath = stale_dirpath / (stem + ".bin");
        for (int n = 2; std::filesystem::exists(stale_filepath, ec); ++n) {
            stale_filepath = stale_dirpath / (stem + "-" + std::to_string(n) + ".bin");
        }
        std::filesystem::create_directories(stale_dirpath, ec);
        std::filesystem::rename(filepath, stale_filepath, ec);
        warnings << "Warning: " << unit->name() << " starts afresh: its saved contents "
                 << filepath.string() << " were not restored because " << why << "; kept as "
                 << (ec ? filepath : stale_filepath).string() << "\n";
    }
    return restored;
}

// Forgets everything the units kept in `dirpath`, as removing the battery
// does: the directory is removed, so every unit starts from its seed at the
// next power-on. Throws when it cannot be removed.
inline void forget_battery_units(const std::filesystem::path& dirpath) {
    std::filesystem::remove_all(dirpath);
}

}  // namespace beebium::server
