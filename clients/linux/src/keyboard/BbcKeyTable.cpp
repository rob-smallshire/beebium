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

#include "keyboard/BbcKeyTable.hpp"

#include "keyboard.pb.h"

namespace beebium::linux_client {

BbcKeyTable::BbcKeyTable(const std::vector<BbcKeyEntry>& entries) {
    for (const auto& entry : entries) {
        if (!entry.character.empty()) {
            by_character_.emplace(entry.character, entry);
        }
        if (!entry.name.empty()) {
            // First wins: the named-only and lowercase rows come first in the
            // server's table, and every row for a name shares its key.
            by_name_.emplace(entry.name, entry.ik_number);
        }
    }
}

BbcKeyTable BbcKeyTable::from_proto(const beebium::AllKeyMappingsResponse& response) {
    std::vector<BbcKeyEntry> entries;
    entries.reserve(static_cast<std::size_t>(response.mappings_size()));
    for (const auto& mapping : response.mappings()) {
        entries.push_back({mapping.character(), static_cast<std::uint8_t>(mapping.ik_number()),
                           mapping.needs_shift(), mapping.name()});
    }
    return BbcKeyTable(entries);
}

std::optional<BbcKeyEntry> BbcKeyTable::by_character(std::string_view utf8_character) const {
    const auto found = by_character_.find(utf8_character);
    if (found == by_character_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<std::uint8_t> BbcKeyTable::ik_by_name(std::string_view name) const {
    const auto found = by_name_.find(name);
    if (found == by_name_.end()) {
        return std::nullopt;
    }
    return found->second;
}

}  // namespace beebium::linux_client
