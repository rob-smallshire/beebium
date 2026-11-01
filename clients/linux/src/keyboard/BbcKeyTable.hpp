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

#ifndef BEEBIUM_LINUX_KEYBOARD_BBC_KEY_TABLE_HPP
#define BEEBIUM_LINUX_KEYBOARD_BBC_KEY_TABLE_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {
class AllKeyMappingsResponse;
}

namespace beebium::linux_client {

// BBC SHIFT and CTRL are ordinary matrix cells (frontend-modifier-keys.md 3).
inline constexpr std::uint8_t BBC_SHIFT_IK = 0x00;
inline constexpr std::uint8_t BBC_CTRL_IK = 0x01;

// One row of the server's canonical character-to-key table
// (KeyboardService.GetAllKeyMappings).
struct BbcKeyEntry {
    std::string character;  // UTF-8; empty for a named-only key
    std::uint8_t ik_number = 0;
    bool needs_shift = false;
    std::string name;       // "A", "Return", "f0", "Up", "Shift Lock"
};

// The server's table, indexed. The server owns it (client specification
// 7.4); the client fetches it once per connection and never keeps its own.
class BbcKeyTable {
public:
    BbcKeyTable() = default;
    explicit BbcKeyTable(const std::vector<BbcKeyEntry>& entries);

    static BbcKeyTable from_proto(const beebium::AllKeyMappingsResponse& response);

    // By the character the key produces: 'a' is the A key unshifted, 'A'
    // the same key shifted.
    std::optional<BbcKeyEntry> by_character(std::string_view utf8_character) const;

    // By key name, as mapping files name BBC keys. Case matters only where
    // names differ by case alone ("a" and "A" are one key, so either works).
    std::optional<std::uint8_t> ik_by_name(std::string_view name) const;

    bool empty() const { return by_character_.empty() && by_name_.empty(); }

private:
    std::map<std::string, BbcKeyEntry, std::less<>> by_character_;
    std::map<std::string, std::uint8_t, std::less<>> by_name_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_KEYBOARD_BBC_KEY_TABLE_HPP
