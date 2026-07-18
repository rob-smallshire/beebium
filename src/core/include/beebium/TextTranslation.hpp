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

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace beebium {

// Translate host text into text the BBC keyboard can type.
//
// Clipboard text carries conventions the BBC does not share: host line
// endings, typographic punctuation from web pages and word processors, and
// the Unicode glyphs the SAA5050 renders for certain ASCII codes. Translation
// reconciles those so that pasting moves characters rather than bytes.
//
// Three groups of substitution are applied:
//
//   Line endings   Every host convention (CR, LF, CRLF, LFCR, and the Unicode
//                  separators U+0085, U+2028, U+2029) becomes a single CR.
//                  Both CR and LF map to Return, so untranslated CRLF would
//                  press Return twice per line.
//   Typography     Smart quotes, dashes, ellipsis and no-break space become
//                  their ASCII counterparts.
//   Teletext       Glyphs the SAA5050 displays for [ \ ] ^ { | } ~ map back to
//                  those codes, so MODE 7 text can be pasted back.
//
// Translation is platform-independent, and must stay that way. A client may
// be running on a different operating system from the server, so the server
// cannot know which line-ending convention the text arrived in or which one
// the client would prefer. Accepting every inbound convention and emitting the
// BBC's own is the only answer that does not require knowing. Nothing here may
// consult the host platform.
//
// The same rule applies in the opposite direction whenever text travels back
// out to a client: the wire carries one canonical form, and converting to a
// platform-native convention is the client's business, on the client's
// machine, at the point where text meets the local clipboard.
//
// Characters with no BBC equivalent are left in place rather than dropped, so
// the caller can reject the text and say which character was at fault. Use
// first_untypeable() on the translated text to find it.
//
// The transformation is idempotent: translating already-translated text
// changes nothing further.
std::string translate_for_typing(std::string_view text);

// Find the first character that cannot be typed on the BBC keyboard.
//
// Returns the Unicode codepoint of the offending character, or nullopt if
// every character is typeable. Apply translate_for_typing() first if the text
// is destined for translation, otherwise characters that translation would
// have handled are reported as failures.
//
// Malformed UTF-8 is reported as U+FFFD REPLACEMENT CHARACTER.
std::optional<char32_t> first_untypeable(std::string_view text);

} // namespace beebium
