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

#ifndef BEEBIUM_LINUX_PRESET_PRESET_DOCUMENT_HPP
#define BEEBIUM_LINUX_PRESET_PRESET_DOCUMENT_HPP

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace beebium::linux_client {

// A preset's JSON as a document (client specification 5.2, 13.3; docs/
// preset-schema.md): lossless, keeping every key it does not know, at every
// level and in its order, and integers distinct from other numbers. An
// explicit null and an absent key both mean unset. Until it is changed, it
// writes back the bytes it was read from.
class PresetDocument {
public:
    // The document, or a sentence saying why the text is not a preset.
    static std::variant<PresetDocument, std::string> parse(std::string_view text);

    const nlohmann::ordered_json& json() const { return json_; }
    bool modified() const { return modified_; }

    // The value at a JSON pointer; nothing when absent or null.
    std::optional<nlohmann::ordered_json> get(const std::string& pointer) const;
    // Set a value, making the path's objects as needed; null unsets.
    void set(const std::string& pointer, nlohmann::ordered_json value);
    void unset(const std::string& pointer);

    // The text: the original bytes if unchanged, else as the server writes
    // a preset.
    std::string dump() const;

private:
    nlohmann::ordered_json json_;
    std::string original_;
    bool modified_ = false;
};

// Equal but for nulls, which mean the same as absent keys.
bool equivalent(const nlohmann::ordered_json& a, const nlohmann::ordered_json& b);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_PRESET_DOCUMENT_HPP
