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

#include "preset/StructuredItem.hpp"

#include <charconv>

namespace beebium::linux_client {

using nlohmann::ordered_json;

std::optional<ItemFormat> ItemFormat::parse(std::string_view format) {
    ItemFormat out;
    std::size_t at = 0;
    while (at < format.size()) {
        const std::size_t open = format.find('{', at);
        if (open == std::string_view::npos) {
            out.parts_.push_back({false, std::string(format.substr(at))});
            break;
        }
        if (open > at) {
            out.parts_.push_back({false, std::string(format.substr(at, open - at))});
        }
        const std::size_t close = format.find('}', open);
        if (close == std::string_view::npos || close == open + 1) {
            return std::nullopt;
        }
        out.parts_.push_back({true, std::string(format.substr(open + 1, close - open - 1))});
        at = close + 1;
    }
    if (out.placeholders().empty()) {
        return std::nullopt;
    }
    return out;
}

std::vector<std::string> ItemFormat::placeholders() const {
    std::vector<std::string> keys;
    for (const auto& part : parts_) {
        if (part.placeholder) {
            keys.push_back(part.text);
        }
    }
    return keys;
}

std::string ItemFormat::line(const ItemStrings& strings) const {
    std::string out;
    for (const auto& part : parts_) {
        if (part.placeholder) {
            const auto found = strings.find(part.text);
            if (found != strings.end()) {
                out += found->second;
            }
        } else {
            out += part.text;
        }
    }
    return out;
}

std::optional<ItemStrings> ItemFormat::split(std::string_view line) const {
    ItemStrings out;
    std::size_t at = 0;
    for (std::size_t i = 0; i < parts_.size(); ++i) {
        const Part& part = parts_[i];
        if (!part.placeholder) {
            if (line.substr(at, part.text.size()) != part.text) {
                return std::nullopt;
            }
            at += part.text.size();
            continue;
        }
        // Up to the next literal, or the rest.
        if (i + 1 < parts_.size() && !parts_[i + 1].placeholder) {
            const std::size_t next = line.find(parts_[i + 1].text, at);
            if (next == std::string_view::npos) {
                return std::nullopt;
            }
            out[part.text] = std::string(line.substr(at, next - at));
            at = next;
        } else {
            out[part.text] = std::string(line.substr(at));
            at = line.size();
        }
    }
    return out;
}

ordered_json item_object(const std::vector<SchemaField>& fields, const ItemStrings& strings) {
    ordered_json object = ordered_json::object();
    for (const auto& field : fields) {
        const auto found = strings.find(field.key);
        if (found == strings.end() || found->second.empty()) {
            continue;
        }
        const std::string& text = found->second;
        if (field.type == "integer") {
            long long value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error == std::errc() && end == text.data() + text.size()) {
                object[field.key] = value;
            } else {
                object[field.key] = text;
            }
        } else if (field.type == "boolean" && (text == "true" || text == "false")) {
            object[field.key] = text == "true";
        } else {
            object[field.key] = text;
        }
    }
    return object;
}

ItemStrings item_strings(const std::vector<SchemaField>& fields, const ordered_json& object) {
    ItemStrings strings;
    for (const auto& field : fields) {
        const auto found = object.find(field.key);
        if (found == object.end() || found->is_null()) {
            strings[field.key] = {};
        } else if (found->is_string()) {
            strings[field.key] = found->get<std::string>();
        } else {
            strings[field.key] = found->dump();
        }
    }
    return strings;
}

}  // namespace beebium::linux_client
