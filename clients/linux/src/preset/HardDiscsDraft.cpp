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

#include "preset/HardDiscsDraft.hpp"

#include <algorithm>
#include <set>

namespace beebium::linux_client {

using nlohmann::ordered_json;

namespace {

const char* EXTENSIONS = "/extensions";

bool has(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

std::optional<HardDiscCapability> HardDiscCapability::from_schema(const PresetSchema& schema) {
    const auto points = schema.attachment_points();
    for (const auto& point : points) {
        if (!point.provided_by) {
            continue;
        }
        // An adapter for the point, on one of the machine's own.
        std::string adapter;
        for (const auto& provider : *point.provided_by) {
            const auto extension = schema.extension(provider);
            if (!extension) {
                continue;
            }
            for (const auto& own : points) {
                if (!own.provided_by && has(extension->attaches_to, own.id)) {
                    adapter = provider;
                }
            }
            if (!adapter.empty()) {
                break;
            }
        }
        if (adapter.empty()) {
            continue;
        }
        for (const auto& name : point.extensions) {
            const auto extension = schema.extension(name);
            if (!extension) {
                continue;
            }
            HardDiscCapability capability;
            for (const auto& parameter : extension->parameters) {
                if (parameter.media == "hard-disc") {
                    capability.image_key = parameter.key;
                } else if (parameter.unique && parameter.type == "integer") {
                    capability.id_key = parameter.key;
                    capability.id_min = static_cast<int>(parameter.min.value_or(0));
                    capability.id_max = static_cast<int>(parameter.max.value_or(7));
                } else if (parameter.provider_of == point.id) {
                    capability.adapter_key = parameter.key;
                }
            }
            if (capability.image_key.empty() || capability.id_key.empty()) {
                continue;
            }
            capability.disc = name;
            capability.point = point.id;
            capability.adapter = adapter;
            capability.per_adapter = point.max_occupancy;
            return capability;
        }
    }
    return std::nullopt;
}

HardDiscsDraft::HardDiscsDraft(MachineDraft& draft, PresetSchema schema, HardDiscCapability capability)
    : draft_(draft), extensions_(draft, std::move(schema)), capability_(std::move(capability)) {}

std::vector<HardDisc> HardDiscsDraft::discs() const {
    std::vector<HardDisc> out;
    const auto all = draft_.get(EXTENSIONS).value_or(ordered_json::array());
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].value("name", std::string()) != capability_.disc) {
            continue;
        }
        const auto config = all[i].value("config", ordered_json::object());
        HardDisc disc;
        disc.index = i;
        if (config.contains(capability_.id_key) && config[capability_.id_key].is_number_integer()) {
            disc.id = config[capability_.id_key].get<int>();
        } else {
            disc.id = capability_.id_min;
        }
        if (config.contains(capability_.image_key) && config[capability_.image_key].is_string()) {
            disc.image = config[capability_.image_key].get<std::string>();
        }
        if (!capability_.adapter_key.empty() && config.contains(capability_.adapter_key) &&
            config[capability_.adapter_key].is_string()) {
            disc.adapter = config[capability_.adapter_key].get<std::string>();
        }
        out.push_back(std::move(disc));
    }
    return out;
}

std::vector<std::string> HardDiscsDraft::adapters() const {
    std::vector<std::string> out;
    for (const auto& instance : extensions_.instances()) {
        if (instance.name == capability_.adapter) {
            out.push_back(instance.id);
        }
    }
    return out;
}

std::string HardDiscsDraft::attached_to(const HardDisc& disc) const {
    if (!disc.adapter.empty()) {
        return disc.adapter;
    }
    const auto all = adapters();
    return all.empty() ? std::string() : all.back();
}

bool HardDiscsDraft::can_add(const std::string& adapter) const {
    const auto all = adapters();
    if (all.empty() || !capability_.per_adapter) {
        return true;
    }
    const std::string target = adapter.empty() ? all.back() : adapter;
    int count = 0;
    for (const auto& disc : discs()) {
        count += attached_to(disc) == target ? 1 : 0;
    }
    return count < *capability_.per_adapter;
}

bool HardDiscsDraft::duplicate_id(const HardDisc& disc) const {
    const std::string adapter = attached_to(disc);
    for (const auto& other : discs()) {
        if (other.index != disc.index && other.id == disc.id && attached_to(other) == adapter) {
            return true;
        }
    }
    return false;
}

void HardDiscsDraft::add(const std::string& adapter) {
    auto all = draft_.get(EXTENSIONS).value_or(ordered_json::array());
    if (adapters().empty()) {
        all.push_back(ordered_json{{"name", capability_.adapter}});
        draft_.set(EXTENSIONS, all);
    }
    const auto several = adapters().size() > 1;
    const std::string target = adapter.empty() ? adapters().back() : adapter;
    std::set<int> taken;
    for (const auto& disc : discs()) {
        if (attached_to(disc) == target) {
            taken.insert(disc.id);
        }
    }
    int id = capability_.id_min;
    while (taken.count(id) && id < capability_.id_max) {
        ++id;
    }
    ordered_json entry{{"name", capability_.disc}};
    entry["config"][capability_.id_key] = id;
    if (several && !adapter.empty() && !capability_.adapter_key.empty()) {
        entry["config"][capability_.adapter_key] = adapter;
    }
    all = draft_.get(EXTENSIONS).value_or(ordered_json::array());
    all.push_back(std::move(entry));
    draft_.set(EXTENSIONS, all);
}

void HardDiscsDraft::remove(const HardDisc& disc) {
    auto all = draft_.get(EXTENSIONS).value_or(ordered_json::array());
    if (disc.index >= all.size()) {
        return;
    }
    all.erase(all.begin() + static_cast<std::ptrdiff_t>(disc.index));
    draft_.set(EXTENSIONS, all);
    // The only adapter goes with the last disc.
    if (discs().empty() && adapters().size() == 1) {
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i].value("name", std::string()) == capability_.adapter) {
                all.erase(all.begin() + static_cast<std::ptrdiff_t>(i));
                draft_.set(EXTENSIONS, all);
                break;
            }
        }
    }
}

void HardDiscsDraft::set_config(std::size_t index, const std::string& key, ordered_json value) {
    auto all = draft_.get(EXTENSIONS).value_or(ordered_json::array());
    if (index >= all.size()) {
        return;
    }
    ordered_json& entry = all[index];
    if (value.is_null()) {
        if (entry.contains("config") && entry["config"].is_object()) {
            entry["config"].erase(key);
            if (entry["config"].empty()) {
                entry.erase("config");
            }
        }
    } else {
        entry["config"][key] = std::move(value);
    }
    draft_.set(EXTENSIONS, all);
}

void HardDiscsDraft::set_id(const HardDisc& disc, int id) {
    set_config(disc.index, capability_.id_key, id);
}

void HardDiscsDraft::set_image(const HardDisc& disc, const std::string& filepath) {
    set_config(disc.index, capability_.image_key, filepath.empty() ? ordered_json() : ordered_json(filepath));
}

void HardDiscsDraft::set_adapter(const HardDisc& disc, const std::string& adapter) {
    if (!capability_.adapter_key.empty()) {
        set_config(disc.index, capability_.adapter_key,
                   adapter.empty() ? ordered_json() : ordered_json(adapter));
    }
}

}  // namespace beebium::linux_client
