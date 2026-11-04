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

#ifndef BEEBIUM_LINUX_PRESET_PRESET_SCHEMA_HPP
#define BEEBIUM_LINUX_PRESET_PRESET_SCHEMA_HPP

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::linux_client {

// What a preset for one model may hold (client specification 5.2, 12.2;
// docs/preset-schema.md, describe-preset-schema): the editor's capabilities,
// all the server's, read once per model. Nothing here knows an extension.

struct SchemaChoice {
    std::string value;
    std::string label;
    std::string description;
};

// One part of a structured list item (its manifest's item_format names it).
struct SchemaField {
    std::string key;
    std::string label;
    std::string description;
    std::string type;  // "string", "integer" or "boolean"
    bool required = false;
    std::string default_value;
    std::optional<std::int64_t> min;
    std::optional<std::int64_t> max;
    std::vector<SchemaChoice> choices;
};

// One manifest parameter of an extension or transport.
struct SchemaParameter {
    std::string key;
    std::string label;
    std::string description;
    std::string type;  // "string", "integer", "boolean", "filepath", "dirpath"
    bool required = false;
    bool is_list = false;
    std::string default_value;
    std::string effective_default;
    std::optional<std::int64_t> min;
    std::optional<std::int64_t> max;
    std::vector<SchemaChoice> choices;
    std::string choices_from;
    std::vector<std::string> excludes;
    std::string bundled;  // a bundled image kind this may name
    std::string media;
    std::string access;   // "read" or "write" for a filepath; empty otherwise
    bool unique = false;
    std::string provider_of;
    std::string item_format;
    std::vector<SchemaField> fields;
};

struct SchemaExtension {
    std::string name;
    std::string display_name;
    std::string description;
    std::string kind;
    std::vector<std::string> attaches_to;
    std::vector<std::string> provides;
    std::vector<SchemaParameter> parameters;
};

struct SchemaAttachmentPoint {
    std::string id;
    std::string display_name;
    std::string description;
    int min_occupancy = 0;
    std::optional<int> max_occupancy;  // none: unbounded
    // Null for one of the model's own connectors.
    std::optional<std::vector<std::string>> provided_by;
    std::vector<std::string> extensions;  // the names that attach here
};

struct SchemaFdcOption {
    std::string id;
    std::string label;
    std::string description;
};

struct SchemaStation {
    std::uint32_t min = 1;
    std::uint32_t max = 254;
    bool automatic = false;
    std::uint32_t auto_low = 1;
    std::uint32_t auto_high = 253;
};

class PresetSchema {
public:
    // The schema, or nothing when the text is not one.
    static std::optional<PresetSchema> parse(std::string_view text);

    const std::string& model_id() const { return model_id_; }
    const std::string& model_name() const { return model_name_; }
    // A section by its preset key, in the server's words; null when absent.
    const nlohmann::ordered_json* section(std::string_view type) const;
    // The section's label, what a user calls it.
    std::string section_label(std::string_view type) const;

    std::vector<SchemaFdcOption> fdc_options() const;
    std::optional<SchemaStation> station() const;
    std::vector<SchemaAttachmentPoint> attachment_points() const;
    // Every extension and transport the model can take.
    std::vector<SchemaExtension> catalogue() const;
    std::optional<SchemaExtension> extension(std::string_view name) const;

private:
    nlohmann::ordered_json json_;
    std::string model_id_;
    std::string model_name_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_PRESET_SCHEMA_HPP
