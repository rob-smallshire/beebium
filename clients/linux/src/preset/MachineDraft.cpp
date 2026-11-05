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

#include "preset/MachineDraft.hpp"

#include "preset/PresetDocument.hpp"

namespace beebium::linux_client {

using nlohmann::ordered_json;

namespace {

const ordered_json* at(const ordered_json& document, const std::string& pointer) {
    const ordered_json::json_pointer path(pointer);
    return document.contains(path) ? &document.at(path) : nullptr;
}

}  // namespace

MachineDraft::MachineDraft(ordered_json resolved)
    : json_(std::move(resolved)), baseline_(json_) {}

std::optional<ordered_json> MachineDraft::get(const std::string& pointer) const {
    const ordered_json* value = at(json_, pointer);
    if (!value || value->is_null()) {
        return std::nullopt;
    }
    return std::optional<ordered_json>(std::in_place, *value);
}

void MachineDraft::set(const std::string& pointer, ordered_json value) {
    if (value.is_null()) {
        unset(pointer);
        return;
    }
    json_[ordered_json::json_pointer(pointer)] = std::move(value);
}

void MachineDraft::unset(const std::string& pointer) {
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
}

bool MachineDraft::changed(const std::string& pointer) const {
    const ordered_json* now = at(json_, pointer);
    const ordered_json* then = at(baseline_, pointer);
    const ordered_json none;
    return !equivalent(now ? *now : none, then ? *then : none);
}

bool MachineDraft::changed_where(const std::string& pointer,
                                 const std::function<bool(const ordered_json& item)>& keep) const {
    const auto kept = [&](const ordered_json* array) {
        ordered_json out = ordered_json::array();
        if (array && array->is_array()) {
            for (const auto& item : *array) {
                if (keep(item)) {
                    out.push_back(item);
                }
            }
        }
        return out;
    };
    return !equivalent(kept(at(json_, pointer)), kept(at(baseline_, pointer)));
}

void MachineDraft::revert(const std::string& pointer) {
    if (pointer.empty()) {
        json_ = baseline_;
        return;
    }
    if (const ordered_json* then = at(baseline_, pointer)) {
        json_[ordered_json::json_pointer(pointer)] = *then;
    } else {
        unset(pointer);
    }
}

std::string MachineDraft::serialise() const {
    return json_.dump(2) + "\n";
}

}  // namespace beebium::linux_client
