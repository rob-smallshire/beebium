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

#ifndef BEEBIUM_LINUX_KEYBOARD_KEY_RESOLVER_HPP
#define BEEBIUM_LINUX_KEYBOARD_KEY_RESOLVER_HPP

#include "keyboard/BbcKeyTable.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace beebium::linux_client {

// A direct entry of a keyboard mapping: a physical host key, named by its
// neutral W3C code (client specification 7.4), bound to a BBC key with
// explicit SHIFT and CTRL overrides.
struct KeyMappingEntry {
    std::string code;           // "ArrowUp", "F12"
    bool requires_alt = false;  // matches only while Alt is held
    std::string bbc_key_name;   // a server table name, or "Break"
    bool bbc_shift = false;     // hold BBC SHIFT with it
    bool bbc_ctrl = false;      // hold BBC CTRL with it
};

struct KeyMapping {
    std::string name;
    // Resolve keys with no direct entry by the character the host layout
    // gives them, through the server's table.
    bool character_mapping = true;
    std::vector<KeyMappingEntry> entries;
};

// The built-in default, "Logical": the macOS client's Logical mapping in
// neutral codes. Keys that produce characters (letters, digits, punctuation,
// Return, Tab, Space) come through character mapping; the rest are direct.
KeyMapping logical_key_mapping();

// One host key event, as the resolver needs it.
struct HostKey {
    std::string code;                // W3C code of the physical key
    std::string text;                // what the host layout produced (UTF-8)
    std::string text_ignoring_ctrl;  // the same key's character without CTRL
    bool ctrl_held = false;
    bool alt_held = false;
};

enum class KeyRole {
    matrix,     // an ordinary key at a matrix position
    shift,      // the user's own SHIFT: feeds the physical state only
    ctrl,       // the user's own CTRL: likewise
    break_key,  // the reset line, not a matrix position
};

struct ResolvedKey {
    KeyRole role = KeyRole::matrix;
    std::uint8_t ik_number = 0;
    bool from_character_mapping = false;
    bool bbc_shift = false;  // the BBC key needs SHIFT (character) or asks for it (direct)
    bool bbc_ctrl = false;   // direct entries only: a character never needs CTRL
};

// Resolve a host key: a direct entry first (one requiring Alt is preferred
// while Alt is held), else, if the mapping allows, the character. Under CTRL
// the character is the unmodified one, since the host folds Ctrl+M into a
// carriage return and the BBC applies CTRL itself (frontend-modifier-keys.md
// R9). Nullopt for a key that maps to nothing.
std::optional<ResolvedKey> resolve_key(const KeyMapping& mapping, const BbcKeyTable& table,
                                       const HostKey& key);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_KEYBOARD_KEY_RESOLVER_HPP
