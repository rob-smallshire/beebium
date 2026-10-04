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

#include "beebium/NameTemplate.hpp"

#include <algorithm>

namespace beebium {

namespace {

bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

// A word: a lowercase letter followed by lowercase letters or digits.
bool is_word(std::string_view word) {
    if (word.empty() || !is_lower(word.front())) return false;
    return std::all_of(word.begin(), word.end(),
                       [](char c) { return is_lower(c) || is_digit(c); });
}

void append_once(std::vector<std::string>& list, std::string_view item) {
    if (std::find(list.begin(), list.end(), item) == list.end()) {
        list.emplace_back(item);
    }
}

}  // namespace

bool is_valid_placeholder_domain(std::string_view domain) {
    return is_word(domain);
}

bool is_valid_placeholder_key(std::string_view key) {
    if (key.empty()) return false;
    std::size_t start = 0;
    while (true) {
        const std::size_t hyphen = key.find('-', start);
        const std::string_view word = key.substr(start, hyphen - start);
        if (!is_word(word)) return false;
        if (hyphen == std::string_view::npos) return true;
        start = hyphen + 1;
    }
}

NameRendering render_name_template(std::string_view text,
                                   const NamePlaceholderLookup& lookup) {
    NameRendering out;
    out.text.reserve(text.size());
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (c == '{') {
            if (i + 1 < n && text[i + 1] == '{') {
                out.text += '{';
                i += 2;
                continue;
            }
            const std::size_t next = text.find_first_of("{}", i + 1);
            if (next == std::string_view::npos || text[next] == '{') {
                // Unterminated: literal up to the next '{' or the end.
                const std::size_t end = (next == std::string_view::npos) ? n : next;
                const std::string_view fragment = text.substr(i, end - i);
                out.text += fragment;
                out.malformed.emplace_back(fragment);
                i = end;
                continue;
            }
            const std::string_view inner = text.substr(i + 1, next - i - 1);
            std::optional<NamePlaceholderValue> value;
            if (is_valid_placeholder_key(inner)) {
                value = lookup(inner);
            }
            if (value) {
                if (value->applicable) {
                    out.text += value->text;
                } else {
                    append_once(out.inapplicable_keys, inner);
                }
            } else {
                out.text += text.substr(i, next - i + 1);
                append_once(out.unknown_keys, inner);
            }
            i = next + 1;
            continue;
        }
        if (c == '}') {
            if (i + 1 < n && text[i + 1] == '}') {
                i += 2;
            } else {
                out.malformed.emplace_back("}");
                i += 1;
            }
            out.text += '}';
            continue;
        }
        // A run of ordinary text up to the next brace.
        const std::size_t next = text.find_first_of("{}", i);
        const std::size_t end = (next == std::string_view::npos) ? n : next;
        out.text += text.substr(i, end - i);
        i = end;
    }
    // No stray space where a placeholder at either end rendered empty.
    constexpr const char* kWhitespace = " \t\r\n\f\v";
    const std::size_t first = out.text.find_first_not_of(kWhitespace);
    if (first == std::string::npos) {
        out.text.clear();
    } else {
        out.text.erase(out.text.find_last_not_of(kWhitespace) + 1);
        out.text.erase(0, first);
    }
    return out;
}

}  // namespace beebium
