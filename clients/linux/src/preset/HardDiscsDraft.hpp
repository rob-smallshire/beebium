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

#ifndef BEEBIUM_LINUX_PRESET_HARD_DISCS_DRAFT_HPP
#define BEEBIUM_LINUX_PRESET_HARD_DISCS_DRAFT_HPP

#include "preset/ExtensionsDraft.hpp"

#include <optional>
#include <string>
#include <vector>

namespace beebium::linux_client {

// The extensions a model's hard discs are made of, as its schema describes
// them: a disc extension on a point an adapter provides, the adapter on a
// point of the machine's own (SCSI: scsi-hard-disc on scsi, provided by
// acorn-scsi on the 1 MHz bus), and the disc's parameters by their roles.
struct HardDiscCapability {
    std::string disc;
    std::string point;
    std::string adapter;
    std::string id_key;       // the unique integer: the SCSI ID
    int id_min = 0;
    int id_max = 7;
    std::string image_key;    // the hard-disc medium
    std::string adapter_key;  // provider_of the point
    std::optional<int> per_adapter;

    static std::optional<HardDiscCapability> from_schema(const PresetSchema& schema);
};

struct HardDisc {
    std::size_t index = 0;  // in /extensions
    int id = 0;
    std::string image;
    std::string adapter;  // the adapter it names; empty for the last
};

// The draft's hard discs as one list (client specification 5.2), as the
// macOS client's Storage section edits them: a disc added with the lowest
// free ID, and an adapter fitted with the first disc and taken out with
// the last when it is the only one.
class HardDiscsDraft {
public:
    HardDiscsDraft(MachineDraft& draft, PresetSchema schema, HardDiscCapability capability);

    const HardDiscCapability& capability() const { return capability_; }
    std::vector<HardDisc> discs() const;
    // The adapters' ids, in the draft's order.
    std::vector<std::string> adapters() const;
    // Whether an adapter (empty: the last, or a new one) has room.
    bool can_add(const std::string& adapter = {}) const;
    // Another disc on the same adapter has the same ID.
    bool duplicate_id(const HardDisc& disc) const;

    // On the adapter named, when it is one of several.
    void add(const std::string& adapter = {});
    void remove(const HardDisc& disc);
    void set_id(const HardDisc& disc, int id);
    void set_image(const HardDisc& disc, const std::string& filepath);
    void set_adapter(const HardDisc& disc, const std::string& adapter);

private:
    std::string attached_to(const HardDisc& disc) const;
    void set_config(std::size_t index, const std::string& key, nlohmann::ordered_json value);

    MachineDraft& draft_;
    ExtensionsDraft extensions_;
    HardDiscCapability capability_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_HARD_DISCS_DRAFT_HPP
