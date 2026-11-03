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

#include "keyboard/KeyResolver.hpp"

namespace beebium::linux_client {

namespace {

bool applies_to(const MappingEntry& entry, const HostKey& key) {
    if (entry.code) {
        return !key.code.empty() && *entry.code == key.code;
    }
    return entry.platform == "linux" && entry.linux_scancode && key.evdev &&
           *entry.linux_scancode == *key.evdev;
}

// Switchable entries start as the file says; the user's switches belong to
// the keyboard view, which does not exist yet.
bool switched_on(const MappingEntry& entry) {
    return !entry.disabled.value_or(false);
}

std::optional<ResolvedKey> resolve_entry(const MappingEntry& entry, const BbcKeyTable& table) {
    ResolvedKey key;
    key.bbc_shift = entry.bbc_shift;
    key.bbc_ctrl = entry.bbc_ctrl;
    if (entry.bbc_key == "Break") {
        key.role = KeyRole::break_key;
        return key;
    }
    const auto ik = table.ik_by_name(entry.bbc_key);
    if (!ik) {
        return std::nullopt;
    }
    key.ik_number = *ik;
    if (*ik == BBC_SHIFT_IK) {
        key.role = KeyRole::shift;
    } else if (*ik == BBC_CTRL_IK) {
        key.role = KeyRole::ctrl;
    }
    return key;
}

}  // namespace

std::optional<ResolvedKey> resolve_key(const MappingFile& mapping, const BbcKeyTable& table,
                                       const HostKey& key) {
    const MappingEntry* exact = nullptr;
    const MappingEntry* subset = nullptr;
    const MappingEntry* unqualified = nullptr;
    for (const auto& entry : mapping.keys) {
        if (!entry.applies || !applies_to(entry, key)) {
            continue;
        }
        if ((entry.with & ~key.modifiers) != 0) {
            continue;  // needs a modifier that is not held
        }
        if (entry.with == key.modifiers) {
            if (!exact) {
                exact = &entry;
            }
        } else if (entry.with != 0) {
            if (!subset) {
                subset = &entry;
            }
        } else if (!unqualified) {
            unqualified = &entry;
        }
    }
    if (const MappingEntry* entry = exact ? exact : subset ? subset : unqualified) {
        // A switched-off entry still wins its key, and silences it: the user
        // switched it off to stop the key doing anything, so it does not fall
        // through to character mapping.
        if (!switched_on(*entry)) {
            return std::nullopt;
        }
        return resolve_entry(*entry, table);
    }

    if (!mapping.character_mapping || key.text.empty()) {
        return std::nullopt;
    }
    const auto entry = table.by_character(key.text);
    if (!entry) {
        return std::nullopt;
    }
    ResolvedKey resolved;
    resolved.ik_number = entry->ik_number;
    resolved.from_character_mapping = true;
    resolved.bbc_shift = entry->needs_shift;
    return resolved;
}

}  // namespace beebium::linux_client
