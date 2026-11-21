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

#ifndef BEEBIUM_LINUX_PRESET_SIDEWAYS_DRAFT_HPP
#define BEEBIUM_LINUX_PRESET_SIDEWAYS_DRAFT_HPP

#include "preset/MachineDraft.hpp"
#include "preset/PresetSchema.hpp"

#include <optional>
#include <string>
#include <vector>

namespace beebium::linux_client {

// What a sideways socket holds in a draft (docs/preset-schema.md, Sideways
// ROM/RAM bank): its slots' entry, or empty where the draft names none.
struct SocketContent {
    std::string type = "empty";  // "rom", "ram" or "empty"
    std::string image_uri;       // a name or path; empty for none
    bool write_protected = false;
    // A Master's bank 9-15 the server derives from the MOS in force
    // ("source": "mos"), written back marked so it follows the MOS; the
    // user's own image is never marked.
    bool from_mos = false;

    friend bool operator==(const SocketContent&, const SocketContent&) = default;
};

// A sideways socket as the draft's cartridges make it (client
// specification 5.9): one a cartridge provides has the capabilities the
// fitted cartridge's manifest gives its banks, and none but "empty"
// without one.
struct DraftSocket {
    SchemaSocket socket;
    std::string point_name;  // where its cartridge is fitted; empty for the model's own
    std::string provider;    // the fitted cartridge, by display name; empty for none
    bool unfitted() const { return !socket.provided_by_point.empty() && provider.empty(); }
};

// The schema's sockets in its order, as the draft fits them.
std::vector<DraftSocket> draft_sockets(const PresetSchema& schema, MachineDraft& draft);

// The draft's sideways slots seen by socket (client specification 5.2, 11),
// as the macOS client's Memory section edits them. A socket is addressed
// at any of its slots; an edited socket is written explicitly, empty
// included, so it means what the user saw.
class SidewaysDraft {
public:
    SidewaysDraft(MachineDraft& draft, std::vector<SchemaSocket> sockets);

    // Highest boot priority first.
    const std::vector<SchemaSocket>& sockets() const { return sockets_; }

    SocketContent content(std::size_t socket) const;
    SocketContent resolved(std::size_t socket) const;  // as the preset resolved
    bool changed(std::size_t socket) const { return content(socket) != resolved(socket); }

    void set(std::size_t socket, const SocketContent& content);
    // The contents of two sockets change places; each keeps its slots.
    void swap(std::size_t a, std::size_t b);
    void revert(std::size_t socket) { set(socket, resolved(socket)); }

    // Where the socket's entry is in the draft's slots, if it has one.
    std::optional<std::size_t> entry_index(std::size_t socket) const;

private:
    SocketContent read(const nlohmann::ordered_json& slots, std::size_t socket) const;
    std::optional<int> entry_slot(std::size_t socket) const;

    MachineDraft& draft_;
    std::vector<SchemaSocket> sockets_;
};

// Take the banks the server derived from the MOS in force (resolve-preset's
// answer for the draft) into the draft, in place of its marked ones: after
// a MOS change they show the new set before launch. The user's own entries
// stay. Whether anything changed.
bool follow_mos_banks(MachineDraft& draft, const nlohmann::ordered_json& resolved);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_SIDEWAYS_DRAFT_HPP
