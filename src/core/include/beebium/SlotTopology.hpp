// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace beebium {

// SocketSpec describes one physical sideways memory socket on a particular
// machine variant: its hardware label (e.g. "IC52"), the logical sideways
// slot numbers wired to it through ROMSEL decoding, and what the socket can
// hold.
//
// On the BBC Model B, partial address decoding causes each socket to respond
// to four slot numbers (e.g. IC52 -> {0, 4, 8, 12}). On machines with full
// 4-bit decoding (Model B with ROM/RAM board) each socket responds to a
// single slot. On the Model B+, each socket is wired to a fixed pair of slots.
struct SocketSpec {
    int socket_index = 0;            // 0..N-1, identifies the physical socket
    std::string label;               // Display label, e.g. "IC52" or "Slot 7"
    std::vector<int> slots;          // Logical slot numbers wired to this socket
    // Every present socket can hold a ROM, be sideways RAM (third-party RAM in
    // a ROM socket with a flying R/W lead was commonplace), or be left vacant.
    // We favour this flexibility over strict per-machine historical accuracy; a
    // machine only sets one of these false if a socket genuinely cannot be it.
    bool supports_rom = true;        // Socket may hold a ROM image
    bool supports_ram = true;        // Socket may be configured as sideways RAM
    bool supports_empty = true;      // Socket may be left empty
    bool runtime_configurable = false; // Type may change at runtime via the API
    // The socket has a RAM write-protect switch (e.g. the ATPL Sidewise S6
    // link). Only then may its RAM be write-protected, at launch
    // (--write-protect) or at runtime (SidewaysService.SetSlotWriteProtect).
    // False for sideways RAM with no such switch (e.g. the B+ 128K SRAM banks),
    // so a front-end offers the control only where it exists.
    bool supports_write_protect = false;
};

// SlotTopology describes the sideways memory layout of a machine variant.
// It is the single source of truth for both startup configuration validation
// and the gRPC SidewaysService.GetSlotStatus topology fields.
struct SlotTopology {
    std::vector<SocketSpec> sockets;
    bool has_aliasing = false;       // True if any socket responds to >1 slot

    // Find the socket spec that owns the given logical slot, or nullptr if the
    // slot does not exist on this machine.
    const SocketSpec* find_socket_for_slot(int slot) const {
        for (const auto& s : sockets) {
            if (std::find(s.slots.begin(), s.slots.end(), slot) != s.slots.end()) {
                return &s;
            }
        }
        return nullptr;
    }

    // True if the slot exists on this machine variant.
    bool slot_exists(int slot) const {
        return find_socket_for_slot(slot) != nullptr;
    }

    // Return the set of all slots that exist on this machine, sorted.
    std::vector<int> all_slots() const {
        std::vector<int> all;
        for (const auto& s : sockets) {
            all.insert(all.end(), s.slots.begin(), s.slots.end());
        }
        std::sort(all.begin(), all.end());
        return all;
    }
};

}  // namespace beebium
