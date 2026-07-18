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

// test_text_translation.cpp
//
// Tests for translating host clipboard text into text the BBC keyboard can
// type. Non-ASCII inputs are written as explicit UTF-8 byte escapes so this
// file stays 7-bit clean.

#include <catch2/catch_test_macros.hpp>

#include <beebium/KeyboardMapping.hpp>
#include <beebium/TextTranslation.hpp>

using namespace beebium;

// ============================================================================
// Line endings
// ============================================================================
//
// The BBC uses CR alone to end a line. Both CR and LF map to the Return key
// (KeyboardMapping.cpp), so untranslated CRLF text would press Return twice
// per line.

TEST_CASE("ASCII text passes through unchanged", "[text-translation]") {
    REQUIRE(translate_for_typing("PRINT \"HELLO\"") == "PRINT \"HELLO\"");
}

TEST_CASE("Empty text translates to empty text", "[text-translation]") {
    REQUIRE(translate_for_typing("") == "");
}

TEST_CASE("CRLF collapses to a single CR", "[text-translation]") {
    REQUIRE(translate_for_typing("10 PRINT\r\n20 END") == "10 PRINT\r20 END");
}

TEST_CASE("Lone LF becomes CR", "[text-translation]") {
    REQUIRE(translate_for_typing("10 PRINT\n20 END") == "10 PRINT\r20 END");
}

TEST_CASE("Lone CR is preserved", "[text-translation]") {
    REQUIRE(translate_for_typing("10 PRINT\r20 END") == "10 PRINT\r20 END");
}

TEST_CASE("LFCR collapses to a single CR", "[text-translation]") {
    REQUIRE(translate_for_typing("A\n\rB") == "A\rB");
}

TEST_CASE("Consecutive CRLF pairs each become one CR", "[text-translation]") {
    REQUIRE(translate_for_typing("A\r\n\r\nB") == "A\r\rB");
}

TEST_CASE("Trailing CRLF becomes a single trailing CR", "[text-translation]") {
    REQUIRE(translate_for_typing("10 END\r\n") == "10 END\r");
}

TEST_CASE("Unicode line and paragraph separators become CR", "[text-translation]") {
    // U+2028 LINE SEPARATOR and U+2029 PARAGRAPH SEPARATOR are rare, but they
    // mean "line break" unambiguously, so honour them rather than rejecting
    // the whole paste.
    REQUIRE(translate_for_typing("A\xE2\x80\xA8" "B") == "A\rB");
    REQUIRE(translate_for_typing("A\xE2\x80\xA9" "B") == "A\rB");
}

TEST_CASE("Next line control becomes CR", "[text-translation]") {
    // U+0085 NEXT LINE, which Python's str.splitlines() also treats as a break.
    REQUIRE(translate_for_typing("A\xC2\x85" "B") == "A\rB");
}

TEST_CASE("A Unicode separator adjacent to CRLF does not swallow a line",
          "[text-translation]") {
    // Only CRLF and LFCR are pairs. A separator beside a CR is its own break.
    REQUIRE(translate_for_typing("A\r\n\xE2\x80\xA8" "B") == "A\r\rB");
}

TEST_CASE("Vertical tab and form feed are not line endings", "[text-translation]") {
    // str.splitlines() would break on these, but they are not line endings in
    // any clipboard convention, and the BBC cannot type them. Leaving them
    // alone lets them be reported rather than silently changing the text.
    REQUIRE(translate_for_typing("A\x0B" "B") == "A\x0B" "B");
    REQUIRE(translate_for_typing("A\x0C" "B") == "A\x0C" "B");
}

// ============================================================================
// Typographic characters
// ============================================================================
//
// Text copied from web pages and word processors routinely contains smart
// quotes and dashes. Without translation these are unmappable and the whole
// paste is rejected.

TEST_CASE("Smart single quotes become apostrophes", "[text-translation]") {
    // U+2018 LEFT SINGLE QUOTATION MARK, U+2019 RIGHT SINGLE QUOTATION MARK
    REQUIRE(translate_for_typing("\xE2\x80\x98" "A" "\xE2\x80\x99") == "'A'");
}

TEST_CASE("Smart double quotes become ASCII quotes", "[text-translation]") {
    // U+201C LEFT DOUBLE QUOTATION MARK, U+201D RIGHT DOUBLE QUOTATION MARK
    REQUIRE(translate_for_typing("\xE2\x80\x9C" "A" "\xE2\x80\x9D") == "\"A\"");
}

TEST_CASE("En dash and em dash become hyphens", "[text-translation]") {
    // U+2013 EN DASH, U+2014 EM DASH
    REQUIRE(translate_for_typing("\xE2\x80\x93") == "-");
    REQUIRE(translate_for_typing("\xE2\x80\x94") == "-");
}

TEST_CASE("Horizontal ellipsis becomes three full stops", "[text-translation]") {
    // U+2026 HORIZONTAL ELLIPSIS
    REQUIRE(translate_for_typing("WAIT\xE2\x80\xA6") == "WAIT...");
}

TEST_CASE("Non-breaking space becomes an ordinary space", "[text-translation]") {
    // U+00A0 NO-BREAK SPACE
    REQUIRE(translate_for_typing("A\xC2\xA0" "B") == "A B");
}

TEST_CASE("Pound sign is preserved", "[text-translation]") {
    // U+00A3 is directly typeable on the BBC keyboard (shifted, ik 0x28),
    // so translation must leave it alone rather than substituting it.
    REQUIRE(translate_for_typing("\xC2\xA3") == "\xC2\xA3");
}

// ============================================================================
// Teletext (SAA5050) glyphs
// ============================================================================
//
// In MODE 7 the SAA5050 renders several ASCII codes as different glyphs.
// Mapping those glyphs back to the codes that produce them lets text copied
// from a MODE 7 screen be pasted back. Without this they are unmappable and
// reject the whole paste.

TEST_CASE("Teletext arrows map to the codes that produce them", "[text-translation]") {
    REQUIRE(translate_for_typing("\xE2\x86\x90") == "[");   // U+2190 LEFTWARDS ARROW
    REQUIRE(translate_for_typing("\xE2\x86\x92") == "]");   // U+2192 RIGHTWARDS ARROW
    REQUIRE(translate_for_typing("\xE2\x86\x91") == "^");   // U+2191 UPWARDS ARROW
}

TEST_CASE("Teletext fractions map to the codes that produce them", "[text-translation]") {
    REQUIRE(translate_for_typing("\xC2\xBC") == "{");       // U+00BC VULGAR FRACTION ONE QUARTER
    REQUIRE(translate_for_typing("\xC2\xBD") == "\\");      // U+00BD VULGAR FRACTION ONE HALF
    REQUIRE(translate_for_typing("\xC2\xBE") == "}");       // U+00BE VULGAR FRACTION THREE QUARTERS
}

TEST_CASE("Teletext division sign maps to tilde", "[text-translation]") {
    REQUIRE(translate_for_typing("\xC3\xB7") == "~");       // U+00F7 DIVISION SIGN
}

// ============================================================================
// Characters with no BBC equivalent
// ============================================================================

TEST_CASE("Untranslatable characters are left in place", "[text-translation]") {
    // Translation must not silently drop what it cannot map. Leaving the
    // character in place lets the caller reject the text and name the
    // offending character.
    const std::string snowman = "\xE2\x98\x83";  // U+2603 SNOWMAN
    REQUIRE(translate_for_typing(snowman) == snowman);
}

TEST_CASE("Untranslatable characters do not disturb their neighbours",
          "[text-translation]") {
    REQUIRE(translate_for_typing("A\xE2\x98\x83\r\nB") == "A\xE2\x98\x83\rB");
}

// ============================================================================
// Properties
// ============================================================================

TEST_CASE("Translation is idempotent", "[text-translation]") {
    const std::string input =
        "10 PRINT \xE2\x80\x9C" "HI" "\xE2\x80\x9D\r\n20 END\r\n";
    const std::string once = translate_for_typing(input);
    REQUIRE(translate_for_typing(once) == once);
}

TEST_CASE("Translated text is typeable when the input is translatable",
          "[text-translation]") {
    // The point of the exercise: text that would be rejected untranslated
    // becomes acceptable to the type-ahead queue.
    const std::string input =
        "10 PRINT \xE2\x80\x9C" "HELLO" "\xE2\x80\x9D\r\n20 END\r\n";
    REQUIRE_FALSE(is_typeable(input));
    REQUIRE(is_typeable(translate_for_typing(input)));
}

// ============================================================================
// Naming the offending character
// ============================================================================

TEST_CASE("First untypeable character is reported with its codepoint",
          "[text-translation]") {
    const auto offender = first_untypeable("AB\xE2\x98\x83" "C");
    REQUIRE(offender.has_value());
    REQUIRE(*offender == 0x2603);
}

TEST_CASE("No offending character is reported for typeable text",
          "[text-translation]") {
    REQUIRE_FALSE(first_untypeable("10 PRINT\r").has_value());
}

TEST_CASE("Offending character is reported after translation is applied",
          "[text-translation]") {
    // A smart quote is untypeable raw but fine once translated, so it must
    // not be reported as the offender.
    const std::string smart_quoted = "\xE2\x80\x9C" "HI" "\xE2\x80\x9D";
    REQUIRE(first_untypeable(smart_quoted).has_value());
    REQUIRE_FALSE(first_untypeable(translate_for_typing(smart_quoted)).has_value());
}
