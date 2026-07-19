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

#ifndef SCREENTEXT_TOOLS_JSON_HPP
#define SCREENTEXT_TOOLS_JSON_HPP

#include <string>

#include "screentext/Read.hpp"

namespace screentext::tools {

// Render a result as JSON: runs, cells, positions and unmatched counts, for a
// caller that needs the uncertainty rather than just the text.
//
// Written by hand rather than with a JSON library, because the output shape
// is fixed and small, and it keeps the CLI's dependencies to the image
// decoder alone.
std::string to_json(const Result& result);

// Escape a string as a JSON string literal, including the surrounding quotes.
std::string json_string(const std::string& value);

} // namespace screentext::tools

#endif // SCREENTEXT_TOOLS_JSON_HPP
