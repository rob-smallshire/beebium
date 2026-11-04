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

#include "preset/PresetSchema.hpp"

namespace beebium::linux_client {

using nlohmann::ordered_json;

namespace {

std::string text(const ordered_json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        return {};
    }
    return found->is_string() ? found->get<std::string>() : found->dump();
}

bool flag(const ordered_json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_boolean() && found->get<bool>();
}

std::optional<std::int64_t> bound(const ordered_json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_integer()) {
        return std::nullopt;
    }
    return found->get<std::int64_t>();
}

std::vector<std::string> strings(const ordered_json& object, const char* key) {
    std::vector<std::string> out;
    const auto found = object.find(key);
    if (found != object.end() && found->is_array()) {
        for (const auto& item : *found) {
            if (item.is_string()) {
                out.push_back(item.get<std::string>());
            }
        }
    }
    return out;
}

std::vector<SchemaChoice> choices(const ordered_json& object) {
    std::vector<SchemaChoice> out;
    const auto found = object.find("choices");
    if (found != object.end() && found->is_array()) {
        for (const auto& item : *found) {
            out.push_back({text(item, "value"), text(item, "label"), text(item, "description")});
        }
    }
    return out;
}

SchemaParameter parameter(const ordered_json& p) {
    SchemaParameter out;
    out.key = text(p, "key");
    out.label = text(p, "label");
    out.description = text(p, "description");
    out.type = text(p, "type");
    out.required = flag(p, "required");
    out.is_list = flag(p, "is_list");
    out.default_value = text(p, "default");
    out.effective_default = text(p, "effective_default");
    out.min = bound(p, "min");
    out.max = bound(p, "max");
    out.choices = choices(p);
    out.choices_from = text(p, "choices_from");
    out.excludes = strings(p, "excludes");
    out.bundled = text(p, "bundled");
    out.media = text(p, "media");
    out.access = text(p, "access");
    out.unique = flag(p, "unique");
    out.provider_of = text(p, "provider_of");
    out.item_format = text(p, "item_format");
    if (const auto fields = p.find("fields"); fields != p.end() && fields->is_array()) {
        for (const auto& f : *fields) {
            out.fields.push_back({text(f, "key"), text(f, "label"), text(f, "description"),
                                  text(f, "type"), flag(f, "required"), text(f, "default"),
                                  bound(f, "min"), bound(f, "max"), choices(f)});
        }
    }
    return out;
}

SchemaExtension extension_of(const ordered_json& e) {
    SchemaExtension out;
    out.name = text(e, "name");
    out.display_name = text(e, "display_name");
    out.description = text(e, "description");
    out.kind = text(e, "kind");
    out.attaches_to = strings(e, "attaches_to");
    out.provides = strings(e, "provides");
    if (const auto ps = e.find("parameters"); ps != e.end() && ps->is_array()) {
        for (const auto& p : *ps) {
            out.parameters.push_back(parameter(p));
        }
    }
    return out;
}

}  // namespace

std::optional<PresetSchema> PresetSchema::parse(std::string_view input) {
    PresetSchema schema;
    try {
        schema.json_ = ordered_json::parse(input);
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
    if (!schema.json_.is_object() || !schema.json_.contains("sections")) {
        return std::nullopt;
    }
    if (const auto model = schema.json_.find("model"); model != schema.json_.end()) {
        schema.model_id_ = text(*model, "id");
        schema.model_name_ = text(*model, "name");
    }
    return schema;
}

const ordered_json* PresetSchema::section(std::string_view type) const {
    for (const auto& s : json_["sections"]) {
        if (s.is_object() && s.value("type", std::string()) == type) {
            return &s;
        }
    }
    return nullptr;
}

std::string PresetSchema::section_label(std::string_view type) const {
    const ordered_json* s = section(type);
    return s ? text(*s, "label") : std::string();
}

std::vector<SchemaFdcOption> PresetSchema::fdc_options() const {
    std::vector<SchemaFdcOption> out;
    const ordered_json* s = section("storage");
    if (!s || !s->contains("fdc_socket") || !(*s)["fdc_socket"].is_object()) {
        return out;
    }
    for (const auto& option : (*s)["fdc_socket"].value("options", ordered_json::array())) {
        out.push_back({text(option, "id"), text(option, "label"), text(option, "description")});
    }
    return out;
}

std::optional<SchemaStation> PresetSchema::station() const {
    const ordered_json* s = section("econet");
    if (!s || !s->contains("station") || !(*s)["station"].is_object()) {
        return std::nullopt;
    }
    const ordered_json& st = (*s)["station"];
    SchemaStation out;
    out.min = static_cast<std::uint32_t>(bound(st, "min").value_or(1));
    out.max = static_cast<std::uint32_t>(bound(st, "max").value_or(254));
    out.automatic = flag(st, "auto");
    if (const auto range = st.find("auto_default_range");
        range != st.end() && range->is_array() && range->size() == 2) {
        out.auto_low = (*range)[0].get<std::uint32_t>();
        out.auto_high = (*range)[1].get<std::uint32_t>();
    }
    return out;
}

std::vector<SchemaAttachmentPoint> PresetSchema::attachment_points() const {
    std::vector<SchemaAttachmentPoint> out;
    const ordered_json* s = section("extensions");
    if (!s) {
        return out;
    }
    for (const auto& p : s->value("attachment_points", ordered_json::array())) {
        SchemaAttachmentPoint point;
        point.id = text(p, "id");
        point.display_name = text(p, "display_name");
        point.description = text(p, "description");
        point.min_occupancy = static_cast<int>(bound(p, "min_occupancy").value_or(0));
        if (const auto max = bound(p, "max_occupancy")) {
            point.max_occupancy = static_cast<int>(*max);
        }
        if (const auto by = p.find("provided_by"); by != p.end() && by->is_array()) {
            point.provided_by = strings(p, "provided_by");
        }
        point.extensions = strings(p, "extensions");
        out.push_back(std::move(point));
    }
    return out;
}

std::vector<SchemaExtension> PresetSchema::catalogue() const {
    std::vector<SchemaExtension> out;
    const ordered_json* s = section("extensions");
    if (!s) {
        return out;
    }
    for (const auto& e : s->value("catalogue", ordered_json::array())) {
        out.push_back(extension_of(e));
    }
    return out;
}

std::optional<SchemaExtension> PresetSchema::extension(std::string_view name) const {
    for (auto& e : catalogue()) {
        if (e.name == name) {
            return e;
        }
    }
    return std::nullopt;
}

}  // namespace beebium::linux_client
