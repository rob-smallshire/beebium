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

#ifndef BEEBIUM_LINUX_PRESET_STRUCTURED_ITEM_HPP
#define BEEBIUM_LINUX_PRESET_STRUCTURED_ITEM_HPP

#include "preset/PresetSchema.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::linux_client {

// A structured list item's parts as text, by field key.
using ItemStrings = std::map<std::string, std::string>;

// A manifest's item_format (client specification 12.2): brace-delimited
// placeholders between literal separators, e.g. "{net}.{station}@{host}".
class ItemFormat {
public:
    // The format, or nothing when the template is not one.
    static std::optional<ItemFormat> parse(std::string_view format);

    std::vector<std::string> placeholders() const;
    // The one-line display of an item's parts.
    std::string line(const ItemStrings& strings) const;
    // A line split back into its parts, each separator taken in turn;
    // nothing when one is missing.
    std::optional<ItemStrings> split(std::string_view line) const;

private:
    struct Part {
        bool placeholder = false;
        std::string text;  // the key, or the literal
    };
    std::vector<Part> parts_;
};

// The object a preset holds for an item: each field typed by its kind, an
// integer that does not parse kept as written for the server to flag, and
// an empty field left out.
nlohmann::ordered_json item_object(const std::vector<SchemaField>& fields,
                                   const ItemStrings& strings);
// An item object's parts as text, every field present.
ItemStrings item_strings(const std::vector<SchemaField>& fields,
                         const nlohmann::ordered_json& object);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_STRUCTURED_ITEM_HPP
