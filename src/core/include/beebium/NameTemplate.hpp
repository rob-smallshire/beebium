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

// Machine-name templates (#153; docs/discussion/machine-name-templates.md).
//
// A machine's name is a template: ordinary text in which `{key}` stands for
// the current value of a placeholder. Rendering is total -- every input
// renders to some text and never fails -- and reports what it could not
// substitute so a client can warn without parsing.
//
// Syntax, read left to right:
//
//   {{          a literal '{'
//   }}          a literal '}'
//   {inner}     a placeholder: '{', then text containing no '{' or '}', then
//               '}'. If `inner` is a well-formed key that the lookup knows,
//               the placeholder renders as its value (or as nothing when the
//               value is not applicable). Otherwise -- an unknown key, an
//               empty `{}`, a key that breaks the key grammar, or one
//               containing a reserved character -- it renders verbatim,
//               braces included, and `inner` is reported in unknown_keys.
//   ':' '|' '?' '!' inside braces are reserved for future formatting,
//               fallbacks and conditional text; today they make the
//               placeholder unknown, so templates using them later still
//               render (verbatim) on this server.
//
// Malformed input, each rendered literally and reported in `malformed`:
//
//   - A '{' (not part of '{{') with no '}' before the next '{' or the end of
//     the text. The '{' and the text up to (not including) that next '{', or
//     to the end, are literal; that fragment is reported. So in
//     "{a{key}}" the fragment "{a" is literal and "{key}" is a placeholder.
//   - A '}' not part of '}}' and not closing a placeholder: a literal '}',
//     reported as "}".
//
// Values are inserted as they are, never parsed again, so a value containing
// braces cannot inject a placeholder.
//
// The finished rendering is trimmed of leading and trailing ASCII whitespace
// (space, tab, CR, LF, FF, VT), so a placeholder that renders empty at either
// end -- "Station {econet-station} {machine-ordinal}" with no ordinal --
// leaves no stray space. Interior whitespace is kept, and a rendering that is
// all whitespace becomes empty. Text other than braces -- including
// non-ASCII UTF-8 -- passes through byte for byte.
//
// Rendering is a single left-to-right pass: time and space are linear in the
// length of the template plus the values inserted. There is no length limit
// here; a caller that stores templates may impose one.
//
// Keys are lowercase ASCII words joined by single hyphens; a word is a letter
// followed by letters or digits: `econet-station`, `scsi-id0-title`. Every
// key begins with its owner's domain, a single word (`econet`, `machine`);
// the placeholder registry enforces that.

#ifndef BEEBIUM_NAME_TEMPLATE_HPP
#define BEEBIUM_NAME_TEMPLATE_HPP

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// A placeholder's current value.
struct NamePlaceholderValue {
    std::string text;
    // False when this machine cannot have a value for the placeholder (an
    // Econet placeholder on a machine with no Econet fitted). An inapplicable
    // placeholder renders as the empty string.
    bool applicable = true;
};

// What a template rendered to, and what it could not substitute.
struct NameRendering {
    std::string text;
    // The inner text of each placeholder that is not a known key, once each,
    // in order of first appearance. "" for an empty `{}`.
    std::vector<std::string> unknown_keys;
    // Known keys whose value was not applicable, once each, in order.
    std::vector<std::string> inapplicable_keys;
    // Each malformed fragment, in order (repeats included): an unterminated
    // "{..." or a lone "}".
    std::vector<std::string> malformed;
};

// Looks up a key, returning its value, or nullopt when the key is not known.
using NamePlaceholderLookup =
    std::function<std::optional<NamePlaceholderValue>(std::string_view key)>;

// Render `name_template`, looking keys up with `lookup`. The lookup is only
// called for well-formed keys.
NameRendering render_name_template(std::string_view name_template,
                                   const NamePlaceholderLookup& lookup);

// Whether `key` is a well-formed placeholder key: lowercase ASCII words
// joined by single hyphens, each word a letter followed by letters or digits.
bool is_valid_placeholder_key(std::string_view key);

// Whether `domain` is a well-formed placeholder domain: a single word as
// above. A key belongs to a domain when it begins with the domain and '-'.
bool is_valid_placeholder_domain(std::string_view domain);

}  // namespace beebium

#endif  // BEEBIUM_NAME_TEMPLATE_HPP
