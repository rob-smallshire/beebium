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

#include <beebium/TextTranslation.hpp>

#include <beebium/KeyboardMapping.hpp>

#include <array>

namespace beebium {

namespace {

constexpr char32_t kReplacementCharacter = 0xFFFD;

// A decoded UTF-8 character: its codepoint and the number of bytes it spanned.
struct DecodedChar {
    char32_t codepoint;
    size_t length;
};

// Decode the UTF-8 character starting at text[pos].
//
// Malformed or truncated sequences yield U+FFFD and advance a single byte, so
// decoding always makes progress and never runs past the end.
DecodedChar decode_utf8(std::string_view text, size_t pos) {
    const auto byte = static_cast<unsigned char>(text[pos]);
    const size_t available = text.size() - pos;

    size_t length = 0;
    char32_t codepoint = 0;

    if ((byte & 0x80) == 0) {
        return {byte, 1};
    } else if ((byte & 0xE0) == 0xC0) {
        length = 2;
        codepoint = byte & 0x1F;
    } else if ((byte & 0xF0) == 0xE0) {
        length = 3;
        codepoint = byte & 0x0F;
    } else if ((byte & 0xF8) == 0xF0) {
        length = 4;
        codepoint = byte & 0x07;
    } else {
        return {kReplacementCharacter, 1};
    }

    if (available < length) {
        return {kReplacementCharacter, 1};
    }

    for (size_t i = 1; i < length; ++i) {
        const auto continuation = static_cast<unsigned char>(text[pos + i]);
        if ((continuation & 0xC0) != 0x80) {
            return {kReplacementCharacter, 1};
        }
        codepoint = (codepoint << 6) | (continuation & 0x3F);
    }

    return {codepoint, length};
}

// Is this codepoint a line break in some host text convention?
//
// Covers the three ordinary conventions (CR, LF, CRLF) plus the Unicode
// separators. Those separators are rare in practice; they are handled because
// they unambiguously mean "line break", the predicate exists anyway, and the
// alternative is rejecting an entire paste over one character.
// Deliberately narrower than Python's str.splitlines(), which also breaks on
// vertical tab, form feed and the file/group/record separators: those are not
// line endings in any clipboard convention, and treating them as such would
// silently restructure the text.
bool is_line_break(char32_t codepoint) {
    return codepoint == '\r'      // CARRIAGE RETURN
        || codepoint == '\n'      // LINE FEED
        || codepoint == 0x0085    // NEXT LINE
        || codepoint == 0x2028    // LINE SEPARATOR
        || codepoint == 0x2029;   // PARAGRAPH SEPARATOR
}

// A substitution from a Unicode codepoint to the BBC characters that stand in
// for it. Replacements are plain ASCII, so they are typeable by construction.
struct Substitution {
    char32_t codepoint;
    const char* replacement;
};

// Typographic characters that host applications produce but the BBC keyboard
// cannot, mapped to their nearest ASCII equivalents.
constexpr std::array kTypography = {
    Substitution{0x2018, "'"},    // LEFT SINGLE QUOTATION MARK
    Substitution{0x2019, "'"},    // RIGHT SINGLE QUOTATION MARK
    Substitution{0x201A, "'"},    // SINGLE LOW-9 QUOTATION MARK
    Substitution{0x201C, "\""},   // LEFT DOUBLE QUOTATION MARK
    Substitution{0x201D, "\""},   // RIGHT DOUBLE QUOTATION MARK
    Substitution{0x201E, "\""},   // DOUBLE LOW-9 QUOTATION MARK
    Substitution{0x2013, "-"},    // EN DASH
    Substitution{0x2014, "-"},    // EM DASH
    Substitution{0x2015, "-"},    // HORIZONTAL BAR
    Substitution{0x2212, "-"},    // MINUS SIGN
    Substitution{0x2026, "..."},  // HORIZONTAL ELLIPSIS
    Substitution{0x00A0, " "},    // NO-BREAK SPACE
    Substitution{0x2022, "*"},    // BULLET
    Substitution{0x00D7, "*"},    // MULTIPLICATION SIGN
};

// Glyphs the SAA5050 teletext generator displays for particular ASCII codes,
// mapped back to the code that produces them. This lets text copied from a
// MODE 7 screen be pasted back. Outside MODE 7 these codes display as the
// ordinary ASCII characters, which is the best available answer: the
// alternative is rejecting the paste outright.
constexpr std::array kTeletext = {
    Substitution{0x2190, "["},    // LEFTWARDS ARROW
    Substitution{0x00BD, "\\"},   // VULGAR FRACTION ONE HALF
    Substitution{0x2192, "]"},    // RIGHTWARDS ARROW
    Substitution{0x2191, "^"},    // UPWARDS ARROW
    Substitution{0x00BC, "{"},    // VULGAR FRACTION ONE QUARTER
    Substitution{0x2016, "|"},    // DOUBLE VERTICAL LINE
    Substitution{0x00BE, "}"},    // VULGAR FRACTION THREE QUARTERS
    Substitution{0x00F7, "~"},    // DIVISION SIGN
};

// Find the replacement for a codepoint, or nullptr if there is none.
const char* find_substitution(char32_t codepoint) {
    for (const auto& substitution : kTypography) {
        if (substitution.codepoint == codepoint) return substitution.replacement;
    }
    for (const auto& substitution : kTeletext) {
        if (substitution.codepoint == codepoint) return substitution.replacement;
    }
    return nullptr;
}

} // namespace

std::string translate_for_typing(std::string_view text) {
    std::string result;
    result.reserve(text.size());

    size_t pos = 0;
    while (pos < text.size()) {
        const auto [codepoint, length] = decode_utf8(text, pos);

        // Line endings: every host convention becomes a single CR. Both CR and
        // LF map to Return, so an untranslated CRLF presses Return twice.
        if (is_line_break(codepoint)) {
            result.push_back('\r');
            pos += length;

            // CRLF and LFCR are single breaks. Two of the same character, or
            // any other pair, are two breaks.
            if (pos < text.size()) {
                const auto next = decode_utf8(text, pos);
                const bool completes_pair =
                    (codepoint == '\r' && next.codepoint == '\n') ||
                    (codepoint == '\n' && next.codepoint == '\r');
                if (completes_pair) {
                    pos += next.length;
                }
            }
            continue;
        }

        if (const char* replacement = find_substitution(codepoint)) {
            result.append(replacement);
        } else {
            // Untranslatable characters are left in place rather than dropped,
            // so the caller can name what it could not type.
            result.append(text.substr(pos, length));
        }
        pos += length;
    }

    return result;
}

std::optional<char32_t> first_untypeable(std::string_view text) {
    size_t pos = 0;
    while (pos < text.size()) {
        const auto [codepoint, length] = decode_utf8(text, pos);
        if (!is_typeable(codepoint)) {
            return codepoint;
        }
        pos += length;
    }
    return std::nullopt;
}

} // namespace beebium
