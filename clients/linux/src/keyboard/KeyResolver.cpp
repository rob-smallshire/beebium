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

KeyMapping logical_key_mapping() {
    KeyMapping mapping;
    mapping.name = "Logical";
    mapping.character_mapping = true;
    mapping.entries = {
        {"ArrowUp", false, "Up"},       {"ArrowDown", false, "Down"},
        {"ArrowLeft", false, "Left"},   {"ArrowRight", false, "Right"},
        {"F1", false, "f1"},            {"F2", false, "f2"},
        {"F3", false, "f3"},            {"F4", false, "f4"},
        {"F5", false, "f5"},            {"F6", false, "f6"},
        {"F7", false, "f7"},            {"F8", false, "f8"},
        {"F9", false, "f9"},            {"F10", false, "f0"},
        {"F11", false, "Copy"},         {"Escape", false, "Escape"},
        {"F12", true, "Break"},         {"F13", false, "Shift Lock"},
        {"Backspace", false, "Delete"}, {"ShiftLeft", false, "Shift"},
        {"ShiftRight", false, "Shift"}, {"ControlLeft", false, "Ctrl"},
        {"ControlRight", false, "Ctrl"},
    };
    return mapping;
}

namespace {

std::optional<ResolvedKey> resolve_entry(const KeyMappingEntry& entry, const BbcKeyTable& table) {
    ResolvedKey key;
    key.bbc_shift = entry.bbc_shift;
    key.bbc_ctrl = entry.bbc_ctrl;
    if (entry.bbc_key_name == "Break") {
        key.role = KeyRole::break_key;
        return key;
    }
    const auto ik = table.ik_by_name(entry.bbc_key_name);
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

std::optional<ResolvedKey> resolve_key(const KeyMapping& mapping, const BbcKeyTable& table,
                                       const HostKey& key) {
    const KeyMappingEntry* with_alt = nullptr;
    const KeyMappingEntry* plain = nullptr;
    for (const auto& entry : mapping.entries) {
        if (entry.code != key.code) {
            continue;
        }
        if (entry.requires_alt) {
            if (key.alt_held && !with_alt) {
                with_alt = &entry;
            }
        } else if (!plain) {
            plain = &entry;
        }
    }
    if (const KeyMappingEntry* entry = with_alt ? with_alt : plain) {
        return resolve_entry(*entry, table);
    }

    if (!mapping.character_mapping) {
        return std::nullopt;
    }
    const std::string& character = key.ctrl_held ? key.text_ignoring_ctrl : key.text;
    if (character.empty()) {
        return std::nullopt;
    }
    const auto entry = table.by_character(character);
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
