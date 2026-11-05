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

#ifndef BEEBIUM_LINUX_PRESET_MACHINE_DRAFT_HPP
#define BEEBIUM_LINUX_PRESET_MACHINE_DRAFT_HPP

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>

namespace beebium::linux_client {

// A machine before launch (client specification 5.2), as the macOS client
// holds it: the preset's resolve-preset document, defaults filled, paths
// absolute and default ROMs explicit, edited in place by JSON pointer. A
// saved or launched draft is the whole document, so it means what the user
// saw even if a server default changes later. Each section knows whether
// it differs from the document as resolved.
class MachineDraft {
public:
    MachineDraft() = default;
    explicit MachineDraft(nlohmann::ordered_json resolved);

    const nlohmann::ordered_json& json() const { return json_; }
    const nlohmann::ordered_json& baseline() const { return baseline_; }

    // The value at a JSON pointer; nothing when absent or null.
    std::optional<nlohmann::ordered_json> get(const std::string& pointer) const;
    // Set a value, making the path's objects as needed; null unsets.
    void set(const std::string& pointer, nlohmann::ordered_json value);
    void unset(const std::string& pointer);

    // Whether the part at a pointer (a section, "/econet", or the whole
    // document, "") differs from the document as resolved, nulls counting
    // as absent.
    bool changed(const std::string& pointer = {}) const;
    // Whether the items of the array at a pointer that are kept differ,
    // in order, from those kept as resolved.
    bool changed_where(const std::string& pointer,
                       const std::function<bool(const nlohmann::ordered_json& item)>& keep) const;
    // Put the part at a pointer back as it was resolved.
    void revert(const std::string& pointer);

    // The whole document, as the server writes a preset: for a launch or a
    // new preset.
    std::string serialise() const;

private:
    nlohmann::ordered_json json_ = nlohmann::ordered_json::object();
    nlohmann::ordered_json baseline_ = nlohmann::ordered_json::object();
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_MACHINE_DRAFT_HPP
