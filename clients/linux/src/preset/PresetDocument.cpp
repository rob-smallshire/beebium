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

#include "preset/PresetDocument.hpp"

namespace beebium::linux_client {

namespace {

using nlohmann::ordered_json;

// The value with every null object member dropped, recursively.
ordered_json without_nulls(const ordered_json& value) {
    if (value.is_object()) {
        ordered_json out = ordered_json::object();
        for (const auto& [key, member] : value.items()) {
            if (!member.is_null()) {
                out[key] = without_nulls(member);
            }
        }
        return out;
    }
    if (value.is_array()) {
        ordered_json out = ordered_json::array();
        for (const auto& member : value) {
            out.push_back(without_nulls(member));
        }
        return out;
    }
    return value;
}

}  // namespace

std::variant<PresetDocument, std::string> PresetDocument::parse(std::string_view text) {
    PresetDocument document;
    try {
        document.json_ = ordered_json::parse(text);
    } catch (const nlohmann::json::parse_error&) {
        return std::string("The preset is not valid JSON.");
    }
    if (!document.json_.is_object()) {
        return std::string("A preset must be a JSON object.");
    }
    document.original_ = std::string(text);
    return document;
}

std::optional<ordered_json> PresetDocument::get(const std::string& pointer) const {
    const ordered_json::json_pointer path(pointer);
    if (!json_.contains(path)) {
        return std::nullopt;
    }
    const ordered_json& value = json_.at(path);
    if (value.is_null()) {
        return std::nullopt;
    }
    return std::optional<ordered_json>(std::in_place, value);
}

void PresetDocument::set(const std::string& pointer, ordered_json value) {
    if (value.is_null()) {
        unset(pointer);
        return;
    }
    json_[ordered_json::json_pointer(pointer)] = std::move(value);
    modified_ = true;
}

void PresetDocument::unset(const std::string& pointer) {
    const ordered_json::json_pointer path(pointer);
    if (!json_.contains(path)) {
        return;
    }
    ordered_json& parent = json_.at(path.parent_pointer());
    if (parent.is_object()) {
        parent.erase(path.back());
    } else if (parent.is_array()) {
        parent.erase(static_cast<std::size_t>(std::stoul(path.back())));
    }
    modified_ = true;
}

std::string PresetDocument::dump() const {
    return modified_ ? json_.dump(2) + "\n" : original_;
}

bool equivalent(const ordered_json& a, const ordered_json& b) {
    return without_nulls(a) == without_nulls(b);
}

}  // namespace beebium::linux_client
