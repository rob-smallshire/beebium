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
    std::string image_uri;       // a name or file:// URI; empty for none
    bool write_protected = false;

    friend bool operator==(const SocketContent&, const SocketContent&) = default;
};

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

private:
    SocketContent read(const nlohmann::ordered_json& slots, std::size_t socket) const;
    std::optional<int> entry_slot(std::size_t socket) const;

    MachineDraft& draft_;
    std::vector<SchemaSocket> sockets_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_SIDEWAYS_DRAFT_HPP
