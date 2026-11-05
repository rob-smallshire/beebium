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
#include "keyboard/MappingFile.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace beebium::linux_client {

// One host key event, as resolution needs it.
struct HostKey {
    std::string code;                    // W3C code of the physical key; empty if none
    std::optional<std::uint32_t> evdev;  // the native key, for linux platform entries
    // Modifier keys held besides this one, from the client's own tracking
    // of held keys, not the event's flags (docs/keyboard-mapping-files.md 4).
    HostModifiers modifiers = 0;
    // The character the host layout gives this key with those modifiers,
    // ignoring Control: the host folds Ctrl+M into a carriage return, and
    // the BBC applies CTRL itself (frontend-modifier-keys.md R9).
    std::string text;
};

enum class KeyRole {
    matrix,     // an ordinary key at a matrix position
    shift,      // the BBC SHIFT key itself: feeds the derived SHIFT
    ctrl,       // the BBC CTRL key itself: likewise
    break_key,  // the reset line, not a matrix position
    switched_off,  // a key the user switched off: it does nothing
};

// The user's switches over a mapping's switchable keys, by BBC key name:
// true for off. A key not named here is as the file says.
using KeySwitches = std::map<std::string, bool>;

// The keys a mapping lets the user switch off (its entries with
// `disabled`), by BBC key name in name order, each with the state it
// starts in: true for off.
std::vector<std::pair<std::string, bool>> switchable_keys(const MappingFile& mapping);

struct ResolvedKey {
    KeyRole role = KeyRole::matrix;
    std::uint8_t ik_number = 0;
    bool from_character_mapping = false;
    bool bbc_shift = false;  // the entry holds SHIFT, or the character's face needs it
    bool bbc_ctrl = false;   // the entry holds CTRL; a character never needs it
};

// Resolve per docs/keyboard-mapping-files.md section 4 (client
// specification 7.4):
//  1. physical entries for this key (its code, or this platform's entry for
//     its native key) whose `with` is a subset of the held modifiers: one
//     equal to them; else the first, in file order, that is a proper subset;
//     else the first with no `with`. If that entry is switched off, by the
//     user's switches or else by the file, it resolves as switched off,
//     without falling through;
//  2. if the mapping allows, the character, through the server's catalogue;
//  3. nothing.
// An entry naming a key the catalogue does not know resolves to nothing.
std::optional<ResolvedKey> resolve_key(const MappingFile& mapping, const BbcKeyTable& table,
                                       const HostKey& key, const KeySwitches& switches = {});

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_KEYBOARD_KEY_RESOLVER_HPP
