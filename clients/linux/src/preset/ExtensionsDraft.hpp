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

#ifndef BEEBIUM_LINUX_PRESET_EXTENSIONS_DRAFT_HPP
#define BEEBIUM_LINUX_PRESET_EXTENSIONS_DRAFT_HPP

#include "preset/MachineDraft.hpp"
#include "preset/PresetSchema.hpp"

#include <optional>
#include <string>
#include <vector>

namespace beebium::linux_client {

// One entry of the draft's extensions.
struct ExtensionInstance {
    std::size_t index = 0;  // in /extensions
    std::string name;       // the extension's
    std::string id;         // the framework id, given or as the server assigns it

    friend bool operator==(const ExtensionInstance&, const ExtensionInstance&) = default;
};

// The draft's extensions as a tree of attachment points (docs/preset-schema.md,
// Extensions and Attachment), as the macOS client's Peripherals section
// edits them: the model's own points, the instances attached to each, and
// beneath an instance the points it provides. An addition is the
// extension's name alone, its parameters left for the server to fill in.
class ExtensionsDraft {
public:
    ExtensionsDraft(MachineDraft& draft, PresetSchema schema);

    std::vector<ExtensionInstance> instances() const;

    // The model's own points that take extensions, but for those excluded.
    std::vector<SchemaAttachmentPoint> model_points(const std::vector<std::string>& excluded = {}) const;
    // The points an instance provides.
    std::vector<SchemaAttachmentPoint> provided_points(const ExtensionInstance& provider) const;
    // What is attached to a point: the model's own, or one a provider's.
    std::vector<ExtensionInstance> attached(const SchemaAttachmentPoint& point,
                                            const ExtensionInstance* provider = nullptr) const;
    // What can be added to a point now, by display name: none when it is full.
    std::vector<SchemaExtension> addable(const SchemaAttachmentPoint& point,
                                         const ExtensionInstance* provider = nullptr) const;
    // What an instance on a single-occupancy point could be changed to.
    std::vector<SchemaExtension> alternatives(const SchemaAttachmentPoint& point,
                                              const ExtensionInstance& instance) const;

    // The extension's name as a user knows it; its identifier when unknown.
    std::string display_name(const std::string& name) const;

    void add(const std::string& name, const SchemaAttachmentPoint& point,
             const ExtensionInstance* provider = nullptr);
    void remove(const ExtensionInstance& instance);
    // The instance gone and the chosen extension added afresh.
    void change(const ExtensionInstance& instance, const std::string& name);

private:
    nlohmann::ordered_json entries() const;
    std::vector<ExtensionInstance> providers_of(const std::string& point) const;
    std::optional<std::string> provider_parameter(const std::string& name,
                                                  const std::string& point) const;
    std::string next_id(const std::string& name) const;

    MachineDraft& draft_;
    PresetSchema schema_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_EXTENSIONS_DRAFT_HPP
