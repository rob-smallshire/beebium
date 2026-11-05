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

#include "preset/ExtensionsDraft.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace beebium::linux_client {

using nlohmann::ordered_json;

namespace {

const char* EXTENSIONS = "/extensions";

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string given_id(const ordered_json& entry) {
    const auto config = entry.value("config", ordered_json::object());
    if (config.is_object() && config.contains("id") && config["id"].is_string()) {
        return config["id"].get<std::string>();
    }
    return {};
}

}  // namespace

ExtensionsDraft::ExtensionsDraft(MachineDraft& draft, PresetSchema schema)
    : draft_(draft), schema_(std::move(schema)) {}

ordered_json ExtensionsDraft::entries() const {
    const auto found = draft_.get(EXTENSIONS);
    return found && found->is_array() ? *found : ordered_json::array();
}

std::vector<ExtensionInstance> ExtensionsDraft::instances() const {
    const ordered_json all = entries();
    // Given ids first: an assigned one never takes a given one.
    std::set<std::string> taken;
    for (const auto& entry : all) {
        const std::string id = given_id(entry);
        if (!id.empty()) {
            taken.insert(id);
        }
    }
    std::vector<ExtensionInstance> out;
    for (std::size_t i = 0; i < all.size(); ++i) {
        ExtensionInstance instance;
        instance.index = i;
        instance.name = all[i].value("name", std::string());
        instance.id = given_id(all[i]);
        if (instance.id.empty()) {
            std::string id = instance.name;
            for (int k = 1; taken.count(id); ++k) {
                id = instance.name + "-" + std::to_string(k);
            }
            instance.id = id;
            taken.insert(id);
        }
        out.push_back(std::move(instance));
    }
    return out;
}

std::vector<SchemaAttachmentPoint> ExtensionsDraft::model_points(
    const std::vector<std::string>& excluded) const {
    std::vector<SchemaAttachmentPoint> out;
    for (auto& point : schema_.attachment_points()) {
        if (!point.provided_by && !point.extensions.empty() && !contains(excluded, point.id)) {
            out.push_back(std::move(point));
        }
    }
    return out;
}

std::vector<SchemaAttachmentPoint> ExtensionsDraft::provided_points(
    const ExtensionInstance& provider) const {
    std::vector<SchemaAttachmentPoint> out;
    const auto extension = schema_.extension(provider.name);
    if (!extension) {
        return out;
    }
    for (auto& point : schema_.attachment_points()) {
        if (contains(extension->provides, point.id)) {
            out.push_back(std::move(point));
        }
    }
    return out;
}

std::vector<ExtensionInstance> ExtensionsDraft::providers_of(const std::string& point) const {
    std::vector<ExtensionInstance> out;
    for (auto& instance : instances()) {
        const auto extension = schema_.extension(instance.name);
        if (extension && contains(extension->provides, point)) {
            out.push_back(std::move(instance));
        }
    }
    return out;
}

std::optional<std::string> ExtensionsDraft::provider_parameter(const std::string& name,
                                                               const std::string& point) const {
    const auto extension = schema_.extension(name);
    if (extension) {
        for (const auto& parameter : extension->parameters) {
            if (parameter.provider_of == point) {
                return parameter.key;
            }
        }
    }
    return std::nullopt;
}

std::vector<ExtensionInstance> ExtensionsDraft::attached(const SchemaAttachmentPoint& point,
                                                         const ExtensionInstance* provider) const {
    const ordered_json all = entries();
    const auto providers = provider ? providers_of(point.id) : std::vector<ExtensionInstance>{};
    std::vector<ExtensionInstance> out;
    for (auto& instance : instances()) {
        const auto extension = schema_.extension(instance.name);
        if (!extension || !contains(extension->attaches_to, point.id)) {
            continue;
        }
        if (provider) {
            // Its provider by id, else the last that provides the point,
            // as the server attaches it.
            std::string named;
            if (const auto key = provider_parameter(instance.name, point.id)) {
                const auto config = all[instance.index].value("config", ordered_json::object());
                if (config.is_object() && config.contains(*key) && config[*key].is_string()) {
                    named = config[*key].get<std::string>();
                }
            }
            const std::string chosen =
                !named.empty() ? named : (providers.empty() ? std::string() : providers.back().id);
            if (chosen != provider->id) {
                continue;
            }
        }
        out.push_back(std::move(instance));
    }
    return out;
}

std::vector<SchemaExtension> ExtensionsDraft::addable(const SchemaAttachmentPoint& point,
                                                      const ExtensionInstance* provider) const {
    std::vector<SchemaExtension> out;
    if (point.max_occupancy &&
        static_cast<int>(attached(point, provider).size()) >= *point.max_occupancy) {
        return out;
    }
    for (const auto& name : point.extensions) {
        if (auto extension = schema_.extension(name)) {
            out.push_back(std::move(*extension));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const SchemaExtension& a, const SchemaExtension& b) {
        return lower(a.display_name) < lower(b.display_name);
    });
    return out;
}

std::vector<SchemaExtension> ExtensionsDraft::alternatives(const SchemaAttachmentPoint& point,
                                                           const ExtensionInstance& instance) const {
    std::vector<SchemaExtension> out;
    for (const auto& name : point.extensions) {
        if (name == instance.name) {
            continue;
        }
        if (auto extension = schema_.extension(name)) {
            out.push_back(std::move(*extension));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const SchemaExtension& a, const SchemaExtension& b) {
        return lower(a.display_name) < lower(b.display_name);
    });
    return out;
}

std::string ExtensionsDraft::display_name(const std::string& name) const {
    const auto extension = schema_.extension(name);
    return extension && !extension->display_name.empty() ? extension->display_name : name;
}

std::string ExtensionsDraft::next_id(const std::string& name) const {
    const auto existing = instances();
    const bool present = std::any_of(existing.begin(), existing.end(),
                                     [&](const ExtensionInstance& i) { return i.name == name; });
    if (!present) {
        return {};  // the first takes its name
    }
    std::set<std::string> taken;
    for (const auto& instance : existing) {
        taken.insert(instance.id);
    }
    std::string id;
    for (int k = 1; id.empty() || taken.count(id); ++k) {
        id = name + "-" + std::to_string(k);
    }
    return id;
}

void ExtensionsDraft::add(const std::string& name, const SchemaAttachmentPoint& point,
                          const ExtensionInstance* provider) {
    ordered_json entry{{"name", name}};
    const std::string id = next_id(name);
    if (!id.empty()) {
        entry["config"]["id"] = id;
    }
    // Beneath one of several providers, it names the one.
    if (provider && providers_of(point.id).size() > 1) {
        if (const auto key = provider_parameter(name, point.id)) {
            entry["config"][*key] = provider->id;
        }
    }
    ordered_json all = entries();
    all.push_back(std::move(entry));
    draft_.set(EXTENSIONS, all);
}

void ExtensionsDraft::remove(const ExtensionInstance& instance) {
    ordered_json all = entries();
    if (instance.index < all.size()) {
        all.erase(all.begin() + static_cast<std::ptrdiff_t>(instance.index));
        draft_.set(EXTENSIONS, all);
    }
}

void ExtensionsDraft::change(const ExtensionInstance& instance, const std::string& name) {
    ordered_json all = entries();
    if (instance.index < all.size()) {
        all.erase(all.begin() + static_cast<std::ptrdiff_t>(instance.index));
        all.push_back(ordered_json{{"name", name}});
        draft_.set(EXTENSIONS, all);
    }
}

}  // namespace beebium::linux_client
