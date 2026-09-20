// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_CLI_ARG_SPLIT_HPP
#define BEEBIUM_CLI_ARG_SPLIT_HPP

#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// Split one CLI option's value into colon-separated tokens, the shared
// convention for beebium's key=value options: `--<ext> key=value:key=value`
// and `--sideways slot=N:type=rom:image=...`. Not extension-specific - the
// extension-args parser and the core --sideways parser both build on it.
//
// A ':' inside a double-quoted run does not split, so a value may contain a
// colon (a URL like ip232://host:port, a Windows path like C:\rom). The quote
// characters are retained in the token here and stripped from the value by the
// caller.
inline std::vector<std::string> split_colon_args(std::string_view input) {
    std::vector<std::string> tokens;
    if (input.empty()) return tokens;

    std::string current;
    bool in_quotes = false;
    for (char c : input) {
        if (c == '"') {
            in_quotes = !in_quotes;
            current += c;
        } else if (c == ':' && !in_quotes) {
            tokens.push_back(std::move(current));
            current.clear();
        } else {
            current += c;
        }
    }
    tokens.push_back(std::move(current));
    return tokens;
}

}  // namespace beebium

#endif  // BEEBIUM_CLI_ARG_SPLIT_HPP
