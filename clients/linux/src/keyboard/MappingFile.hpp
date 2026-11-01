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

#ifndef BEEBIUM_LINUX_KEYBOARD_MAPPING_FILE_HPP
#define BEEBIUM_LINUX_KEYBOARD_MAPPING_FILE_HPP

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace beebium::linux_client {

// A keyboard mapping file, version 2 (docs/keyboard-mapping-files.md; client
// specification 7.4). The parsed fields are what resolution needs; the file
// as read is kept beside them, so writing it back preserves every field this
// client does not understand and every other platform's entries (sections
// 2, 3.4 and 6).

// Host modifiers an entry may require (section 3.2), as a bit set. Left and
// right are not distinguished.
using HostModifiers = std::uint8_t;
inline constexpr HostModifiers MODIFIER_SHIFT = 1;
inline constexpr HostModifiers MODIFIER_CONTROL = 2;
inline constexpr HostModifiers MODIFIER_ALT = 4;
inline constexpr HostModifiers MODIFIER_META = 8;

// The modifier a W3C code names ("ShiftLeft" is MODIFIER_SHIFT), or 0.
HostModifiers modifier_for_code(std::string_view code);

using Json = nlohmann::ordered_json;

struct MappingEntry {
    // Exactly one of code (portable) and platform (section 3.4).
    std::optional<std::string> code;
    std::optional<std::string> platform;
    // For a "linux" platform entry: its evdev code (section 3.4).
    std::optional<std::uint32_t> linux_scancode;

    HostModifiers with = 0;

    std::string bbc_key;      // a catalogue name, or "Break"
    bool bbc_shift = false;
    bool bbc_ctrl = false;
    // Absent: cannot be switched off. Otherwise switchable, and this is
    // whether it starts off.
    std::optional<bool> disabled;
    std::optional<std::string> action;

    Json source;  // the entry as read
};

struct MappingFile {
    std::string id;
    std::string name;
    bool character_mapping = true;
    bool synchronize_caps_lock = true;
    std::vector<MappingEntry> keys;

    Json source;  // the file as read
};

struct MappingFileError {
    std::string reason;
};

using MappingFileResult = std::variant<MappingFile, MappingFileError>;

// Read a mapping file. Refuses a version other than 2 (a version 1 file is
// reported as such, for conversion), a malformed entry, an unknown host
// modifier, and an entry carrying both a code and a platform, or a "linux"
// platform entry for a key that has a code (section 3.4).
MappingFileResult parse_mapping_file(std::string_view json_text);

// Write a mapping file as version 2: the known fields from `file`, every
// other field as it was read.
std::string write_mapping_file(const MappingFile& file);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_KEYBOARD_MAPPING_FILE_HPP
