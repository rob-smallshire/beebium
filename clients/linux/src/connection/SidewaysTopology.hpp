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

#ifndef BEEBIUM_LINUX_CONNECTION_SIDEWAYS_TOPOLOGY_HPP
#define BEEBIUM_LINUX_CONNECTION_SIDEWAYS_TOPOLOGY_HPP

#include "sideways.pb.h"

#include <cstdint>
#include <string>
#include <vector>

namespace beebium::linux_client {

// The machine's sideways ROM/RAM as the server describes it (client
// specification 11; docs/sideways-slots.md). All of it is the server's: the
// client maps and displays, never infers from the machine's model.

enum class SidewaysSlotType { empty, rom, ram };

// A ROM header, parsed by the server; recognised is false when there is
// none (an empty socket, blank RAM, or a non-standard image).
struct SidewaysRom {
    bool recognised = false;
    std::string title;
    std::string version;
    std::string copyright;
    std::vector<std::string> kinds;  // "language", "service", "romfs"

    friend bool operator==(const SidewaysRom&, const SidewaysRom&) = default;
};

// A physical socket and the slot numbers that answer to it.
struct SidewaysSocket {
    std::uint32_t index = 0;
    std::string label;                // e.g. "IC52"; empty where the board has none
    std::vector<std::uint32_t> slots;  // ascending
    SidewaysSlotType type = SidewaysSlotType::empty;
    bool populated = false;
    std::string image_filepath;  // on the server's host; empty when none
    bool runtime_configurable = false;
    SidewaysRom rom;
};

// A switch that protects a set of slots as a whole: a one-slot write
// switch, a whole-board write switch, or a hide (read-protect) switch.
struct SidewaysProtectionGroup {
    std::string id;
    std::string label;
    std::vector<std::uint32_t> slots;
    bool supports_write_protect = false;
    bool supports_hide = false;
    bool write_protected = false;
    bool hidden = false;
};

struct SidewaysTopology {
    bool has_aliasing = false;
    // Highest boot priority first: the MOS looks from slot 15 down, so a
    // socket ranks by the highest slot that answers to it.
    std::vector<SidewaysSocket> sockets;
    // In the server's order.
    std::vector<SidewaysProtectionGroup> groups;

    // The groups governing any of a socket's slots, in the server's order.
    std::vector<const SidewaysProtectionGroup*> groups_covering(const SidewaysSocket& socket) const;
    // A RAM slot's new header from the event stream, applied to the socket
    // that answers that slot. Returns whether anything changed.
    bool apply_header_change(std::uint32_t slot, const beebium::RomHeader& header);
};

SidewaysRom sideways_rom(const beebium::RomHeader& header);
SidewaysTopology sideways_topology(const beebium::GetSlotStatusResponse& response);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_SIDEWAYS_TOPOLOGY_HPP
