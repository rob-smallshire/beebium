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

#include "connection/SidewaysTopology.hpp"

#include <algorithm>

namespace beebium::linux_client {

namespace {

SidewaysSlotType slot_type(beebium::SidewaysSlotType type) {
    switch (type) {
    case beebium::SIDEWAYS_SLOT_TYPE_ROM: return SidewaysSlotType::rom;
    case beebium::SIDEWAYS_SLOT_TYPE_RAM: return SidewaysSlotType::ram;
    default: return SidewaysSlotType::empty;
    }
}

std::uint32_t highest_slot(const SidewaysSocket& socket) {
    return socket.slots.empty() ? socket.index : socket.slots.back();
}

bool covers(const std::vector<std::uint32_t>& slots, std::uint32_t slot) {
    return std::find(slots.begin(), slots.end(), slot) != slots.end();
}

}  // namespace

SidewaysRom sideways_rom(const beebium::RomHeader& header) {
    SidewaysRom rom;
    rom.recognised = header.recognised();
    rom.title = header.title();
    rom.version = header.version();
    rom.copyright = header.copyright();
    rom.kinds.assign(header.kinds().begin(), header.kinds().end());
    return rom;
}

SidewaysTopology sideways_topology(const beebium::GetSlotStatusResponse& response) {
    SidewaysTopology topology;
    topology.has_aliasing = response.has_aliasing();
    for (const auto& status : response.sockets()) {
        SidewaysSocket socket;
        socket.index = status.socket_index();
        socket.label = status.socket_label();
        socket.slots.assign(status.aliased_slots().begin(), status.aliased_slots().end());
        std::sort(socket.slots.begin(), socket.slots.end());
        socket.type = slot_type(status.type());
        socket.populated = status.populated();
        socket.image_filepath = status.image_name();
        socket.runtime_configurable = status.capabilities().runtime_configurable();
        socket.rom = sideways_rom(status.rom_header());
        topology.sockets.push_back(std::move(socket));
    }
    std::stable_sort(topology.sockets.begin(), topology.sockets.end(),
                     [](const SidewaysSocket& a, const SidewaysSocket& b) {
                         return highest_slot(a) > highest_slot(b);
                     });
    for (const auto& wire : response.protection_groups()) {
        SidewaysProtectionGroup group;
        group.id = wire.id();
        group.label = wire.label();
        group.slots.assign(wire.slots().begin(), wire.slots().end());
        group.supports_write_protect = wire.supports_write_protect();
        group.supports_hide = wire.supports_hide();
        group.write_protected = wire.write_protected();
        group.hidden = wire.hidden();
        topology.groups.push_back(std::move(group));
    }
    return topology;
}

std::vector<const SidewaysProtectionGroup*> SidewaysTopology::groups_covering(
    const SidewaysSocket& socket) const {
    std::vector<const SidewaysProtectionGroup*> covering;
    for (const auto& group : groups) {
        const bool any = std::any_of(socket.slots.begin(), socket.slots.end(),
                                     [&](std::uint32_t slot) { return covers(group.slots, slot); });
        if (any) {
            covering.push_back(&group);
        }
    }
    return covering;
}

bool SidewaysTopology::apply_header_change(std::uint32_t slot, const beebium::RomHeader& header) {
    for (auto& socket : sockets) {
        if (covers(socket.slots, slot)) {
            SidewaysRom rom = sideways_rom(header);
            if (rom == socket.rom) {
                return false;
            }
            // A header means content, as on macOS.
            socket.populated = socket.populated || rom.recognised;
            socket.rom = std::move(rom);
            return true;
        }
    }
    return false;
}

}  // namespace beebium::linux_client
