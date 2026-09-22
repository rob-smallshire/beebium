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

#include <string>
#include <vector>

namespace beebium {

// The two controls a protection group can carry. Write-protect inhibits writes
// to the group's slots; Hide makes them "vanish" - reads return a constant, so
// the ROM/RAM effectively disappears from the machine (the Watford S1 link).
enum class ProtectionKind { WriteProtect, Hide };

// A named set of sideways slots whose write-protect and/or hide controls are
// engaged as a whole by one link/switch on a board. This is the single,
// board-agnostic way Beebium models sideways-memory protection:
//
//  - the ATPL Sidewise slot-15 write-protect is a one-slot write group,
//  - the Watford ROM/RAM board's S2 is a write group covering every slot, and
//    its S1 is a hide group covering just slot 14,
//  - the (fantasy) ROM/RAM board exposes a one-slot write group per RAM slot.
//
// A machine variant exposes its groups through Memory::protection_groups(), and
// they are enumerated on SidewaysService.GetSlotStatus so a front-end can build
// the matching controls (a labelled switch per group, over the listed slots) and
// toggle them with SidewaysService.SetSlotProtection. The list is empty on
// machines that have no protection controls.
struct SlotProtectionGroup {
    std::string id;                     // Stable identifier, e.g. "board", "slot-14", "slot-15"
    std::string label;                  // Human-readable, e.g. "Write-protect (S2)"
    std::vector<int> slots;             // The slots this switch covers as a whole
    bool supports_write_protect = false;  // The group has a write-protect switch
    bool supports_hide = false;         // The group has a hide switch
    bool write_protected = false;       // Current state (meaningful iff supports_write_protect)
    bool hidden = false;                // Current state (meaningful iff supports_hide)
};

}  // namespace beebium
