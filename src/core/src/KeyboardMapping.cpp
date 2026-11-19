// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include <beebium/KeyboardMapping.hpp>

#include <algorithm>
#include <array>
#include <cctype>

namespace beebium {

// BBC Micro keyboard mapping table
// Maps Unicode codepoints and key names to internal key numbers and shift state.
//
// Internal key number format: (row << 4) | column
// Example: 'V' at row 6, column 3 = 0x63
//
// BBC Keyboard Matrix Layout (10 columns x 8 rows of regular keys; the
// Master 128's keypad adds columns 10-12):
//   Row 0: SHIFT(0), CTRL(1), [links 2-9]
//   Row 1: Q(0), 3(1), 4(2), 5(3), f4(4), 8(5), f7(6), -(7), ^/~(8), LEFT(9)
//   Row 2: f0(0), W(1), E(2), T(3), 7/'(4), I(5), 9(6), 0(7), £/_(8), DOWN(9)
//   Row 3: 1/!(0), 2/"(1), D(2), R(3), 6/&(4), U(5), O(6), P(7), [(8), UP(9)
//   Row 4: CAPS(0), A(1), X(2), F(3), Y(4), J(5), K(6), @(7), :/* (8), RETURN(9)
//   Row 5: SHIFTLOCK(0), S(1), C(2), G(3), H(4), N(5), L(6), ;/+(7), ](8), DELETE(9)
//   Row 6: TAB(0), Z(1), SPACE(2), V(3), B(4), M(5), ,/<(6), ./>(/7), //?(/8), COPY(9)
//   Row 7: ESC(0), f1(1), f2(2), f3(3), f5(4), f6(5), f8(6), f9(7), \|(/8), RIGHT(9)
//
// Table includes:
// - Character keys (with codepoint and name)
// - Named-only keys (codepoint = 0): modifiers, function keys, cursor keys, etc.
//
static constexpr std::array kMappingTable = {
    // =========================================================================
    // Named-only keys (no character output, codepoint = 0)
    // =========================================================================

    // Modifier keys
    KeyMapping{0, 0x00, false, "Shift"},
    KeyMapping{0, 0x01, false, "Ctrl"},
    KeyMapping{0, 0x40, false, "Caps Lock"},
    KeyMapping{0, 0x50, false, "Shift Lock"},

    // Control keys
    KeyMapping{0, 0x70, false, "Escape"},
    KeyMapping{0, 0x60, false, "Tab"},
    KeyMapping{0, 0x49, false, "Return"},
    KeyMapping{0, 0x59, false, "Delete"},
    KeyMapping{0, 0x69, false, "Copy"},

    // Cursor keys
    KeyMapping{0, 0x39, false, "Up"},
    KeyMapping{0, 0x29, false, "Down"},
    KeyMapping{0, 0x19, false, "Left"},
    KeyMapping{0, 0x79, false, "Right"},

    // Function keys
    KeyMapping{0, 0x20, false, "f0"},
    KeyMapping{0, 0x71, false, "f1"},
    KeyMapping{0, 0x72, false, "f2"},
    KeyMapping{0, 0x73, false, "f3"},
    KeyMapping{0, 0x14, false, "f4"},
    KeyMapping{0, 0x74, false, "f5"},
    KeyMapping{0, 0x75, false, "f6"},
    KeyMapping{0, 0x16, false, "f7"},
    KeyMapping{0, 0x76, false, "f8"},
    KeyMapping{0, 0x77, false, "f9"},

    // The Master 128's keypad, in columns 10-12 (Master Reference Manual,
    // the internal key numbers; research section 1.5). Named only: the
    // characters they type come from the main keyboard's keys, so typing a
    // digit never presses the keypad. A machine whose keyboard has fewer
    // columns does not have them (KeyboardMatrix::column_count).
    KeyMapping{0, 0x6A, false, "Keypad0"},
    KeyMapping{0, 0x6B, false, "Keypad1"},
    KeyMapping{0, 0x7C, false, "Keypad2"},
    KeyMapping{0, 0x6C, false, "Keypad3"},
    KeyMapping{0, 0x7A, false, "Keypad4"},
    KeyMapping{0, 0x7B, false, "Keypad5"},
    KeyMapping{0, 0x1A, false, "Keypad6"},
    KeyMapping{0, 0x1B, false, "Keypad7"},
    KeyMapping{0, 0x2A, false, "Keypad8"},
    KeyMapping{0, 0x2B, false, "Keypad9"},
    KeyMapping{0, 0x3A, false, "KeypadPlus"},
    KeyMapping{0, 0x3B, false, "KeypadMinus"},
    KeyMapping{0, 0x4A, false, "KeypadDivide"},
    KeyMapping{0, 0x5A, false, "KeypadHash"},
    KeyMapping{0, 0x5B, false, "KeypadMultiply"},
    KeyMapping{0, 0x5C, false, "KeypadComma"},
    KeyMapping{0, 0x4C, false, "KeypadPeriod"},
    KeyMapping{0, 0x3C, false, "KeypadReturn"},
    KeyMapping{0, 0x4B, false, "KeypadDelete"},

    // =========================================================================
    // Character keys (codepoint maps to character, name is the character)
    // =========================================================================

    // Letters (lowercase - no shift)
    KeyMapping{'a', 0x41, false, "a"},
    KeyMapping{'b', 0x64, false, "b"},
    KeyMapping{'c', 0x52, false, "c"},
    KeyMapping{'d', 0x32, false, "d"},
    KeyMapping{'e', 0x22, false, "e"},
    KeyMapping{'f', 0x43, false, "f"},
    KeyMapping{'g', 0x53, false, "g"},
    KeyMapping{'h', 0x54, false, "h"},
    KeyMapping{'i', 0x25, false, "i"},
    KeyMapping{'j', 0x45, false, "j"},
    KeyMapping{'k', 0x46, false, "k"},
    KeyMapping{'l', 0x56, false, "l"},
    KeyMapping{'m', 0x65, false, "m"},
    KeyMapping{'n', 0x55, false, "n"},
    KeyMapping{'o', 0x36, false, "o"},
    KeyMapping{'p', 0x37, false, "p"},
    KeyMapping{'q', 0x10, false, "q"},
    KeyMapping{'r', 0x33, false, "r"},
    KeyMapping{'s', 0x51, false, "s"},
    KeyMapping{'t', 0x23, false, "t"},
    KeyMapping{'u', 0x35, false, "u"},
    KeyMapping{'v', 0x63, false, "v"},
    KeyMapping{'w', 0x21, false, "w"},
    KeyMapping{'x', 0x42, false, "x"},
    KeyMapping{'y', 0x44, false, "y"},
    KeyMapping{'z', 0x61, false, "z"},

    // Letters (uppercase - need shift)
    KeyMapping{'A', 0x41, true, "A"},
    KeyMapping{'B', 0x64, true, "B"},
    KeyMapping{'C', 0x52, true, "C"},
    KeyMapping{'D', 0x32, true, "D"},
    KeyMapping{'E', 0x22, true, "E"},
    KeyMapping{'F', 0x43, true, "F"},
    KeyMapping{'G', 0x53, true, "G"},
    KeyMapping{'H', 0x54, true, "H"},
    KeyMapping{'I', 0x25, true, "I"},
    KeyMapping{'J', 0x45, true, "J"},
    KeyMapping{'K', 0x46, true, "K"},
    KeyMapping{'L', 0x56, true, "L"},
    KeyMapping{'M', 0x65, true, "M"},
    KeyMapping{'N', 0x55, true, "N"},
    KeyMapping{'O', 0x36, true, "O"},
    KeyMapping{'P', 0x37, true, "P"},
    KeyMapping{'Q', 0x10, true, "Q"},
    KeyMapping{'R', 0x33, true, "R"},
    KeyMapping{'S', 0x51, true, "S"},
    KeyMapping{'T', 0x23, true, "T"},
    KeyMapping{'U', 0x35, true, "U"},
    KeyMapping{'V', 0x63, true, "V"},
    KeyMapping{'W', 0x21, true, "W"},
    KeyMapping{'X', 0x42, true, "X"},
    KeyMapping{'Y', 0x44, true, "Y"},
    KeyMapping{'Z', 0x61, true, "Z"},

    // Digits (unshifted)
    KeyMapping{'0', 0x27, false, "0"},
    KeyMapping{'1', 0x30, false, "1"},
    KeyMapping{'2', 0x31, false, "2"},
    KeyMapping{'3', 0x11, false, "3"},
    KeyMapping{'4', 0x12, false, "4"},
    KeyMapping{'5', 0x13, false, "5"},
    KeyMapping{'6', 0x34, false, "6"},
    KeyMapping{'7', 0x24, false, "7"},
    KeyMapping{'8', 0x15, false, "8"},
    KeyMapping{'9', 0x26, false, "9"},

    // Shifted digits produce symbols
    KeyMapping{'!', 0x30, true, "!"},
    KeyMapping{'"', 0x31, true, "\""},
    KeyMapping{'#', 0x11, true, "#"},
    KeyMapping{'$', 0x12, true, "$"},
    KeyMapping{'%', 0x13, true, "%"},
    KeyMapping{'&', 0x34, true, "&"},
    KeyMapping{'\'', 0x24, true, "'"},
    KeyMapping{'(', 0x15, true, "("},
    KeyMapping{')', 0x26, true, ")"},

    // Punctuation (unshifted)
    KeyMapping{' ', 0x62, false, "Space"},
    KeyMapping{',', 0x66, false, ","},
    KeyMapping{'.', 0x67, false, "."},
    KeyMapping{'/', 0x68, false, "/"},
    KeyMapping{';', 0x57, false, ";"},
    KeyMapping{':', 0x48, false, ":"},
    KeyMapping{'[', 0x38, false, "["},
    KeyMapping{']', 0x58, false, "]"},
    KeyMapping{'-', 0x17, false, "-"},
    KeyMapping{'^', 0x18, false, "^"},
    KeyMapping{'@', 0x47, false, "@"},
    KeyMapping{'\\', 0x78, false, "\\"},
    KeyMapping{'_', 0x28, false, "_"},
    KeyMapping{0x00A3, 0x28, true, "£"},  // Unicode pound sterling (U+00A3)

    // Shifted punctuation
    KeyMapping{'<', 0x66, true, "<"},
    KeyMapping{'>', 0x67, true, ">"},
    KeyMapping{'?', 0x68, true, "?"},
    KeyMapping{'+', 0x57, true, "+"},
    KeyMapping{'*', 0x48, true, "*"},
    KeyMapping{'{', 0x38, true, "{"},
    KeyMapping{'}', 0x58, true, "}"},
    KeyMapping{'=', 0x17, true, "="},
    KeyMapping{'~', 0x18, true, "~"},
    KeyMapping{'|', 0x78, true, "|"},

    // Control characters that also have character codes
    KeyMapping{'\r', 0x49, false, "\\r"},
    KeyMapping{'\n', 0x49, false, "\\n"},
    KeyMapping{'\t', 0x60, false, "\\t"},
    KeyMapping{'\x1b', 0x70, false, "\\e"},
    KeyMapping{'\x7f', 0x59, false, "DEL"},
};

// Case-insensitive string comparison
static bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::optional<KeyMapping> char_to_key(char32_t codepoint) {
    for (const auto& mapping : kMappingTable) {
        if (mapping.codepoint != 0 && mapping.codepoint == codepoint) {
            return mapping;
        }
    }
    return std::nullopt;
}

std::optional<KeyMapping> key_by_name(std::string_view name) {
    for (const auto& mapping : kMappingTable) {
        if (iequals(name, mapping.name)) {
            return mapping;
        }
    }
    return std::nullopt;
}

std::optional<char32_t> key_to_char(uint8_t ik_number, bool shifted) {
    for (const auto& mapping : kMappingTable) {
        if (mapping.codepoint != 0 &&
            mapping.ik_number == ik_number &&
            mapping.needs_shift == shifted) {
            return mapping.codepoint;
        }
    }
    return std::nullopt;
}

const char* key_name(uint8_t ik_number) {
    for (const auto& mapping : kMappingTable) {
        if (mapping.ik_number == ik_number && !mapping.needs_shift) {
            return mapping.name;
        }
    }
    return nullptr;
}

bool is_typeable(char32_t codepoint) {
    return char_to_key(codepoint).has_value();
}

bool is_typeable(std::string_view text) {
    // Decode UTF-8 and check each codepoint
    size_t i = 0;
    while (i < text.size()) {
        char32_t codepoint;
        uint8_t byte = static_cast<uint8_t>(text[i]);

        if ((byte & 0x80) == 0) {
            // Single byte ASCII
            codepoint = byte;
            i += 1;
        } else if ((byte & 0xE0) == 0xC0) {
            // Two-byte UTF-8
            if (i + 1 >= text.size()) return false;
            codepoint = ((byte & 0x1F) << 6) |
                        (static_cast<uint8_t>(text[i + 1]) & 0x3F);
            i += 2;
        } else if ((byte & 0xF0) == 0xE0) {
            // Three-byte UTF-8
            if (i + 2 >= text.size()) return false;
            codepoint = ((byte & 0x0F) << 12) |
                        ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 6) |
                        (static_cast<uint8_t>(text[i + 2]) & 0x3F);
            i += 3;
        } else if ((byte & 0xF8) == 0xF0) {
            // Four-byte UTF-8
            if (i + 3 >= text.size()) return false;
            codepoint = ((byte & 0x07) << 18) |
                        ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 12) |
                        ((static_cast<uint8_t>(text[i + 2]) & 0x3F) << 6) |
                        (static_cast<uint8_t>(text[i + 3]) & 0x3F);
            i += 4;
        } else {
            // Invalid UTF-8
            return false;
        }

        if (!is_typeable(codepoint)) {
            return false;
        }
    }
    return true;
}

std::span<const KeyMapping> get_all_mappings() {
    return kMappingTable;
}

size_t mapping_count() {
    return kMappingTable.size();
}

} // namespace beebium
