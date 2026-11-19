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

// Something other than the machine that answers on sideways banks: a
// cartridge in the Master's cartridge slot is the first. The machine's
// sideways memory (ConfigurableBankedMemory::attach_provider) hands a bank to
// a provider, and from then on every read and write of that bank, from the
// CPU, the debugger or SidewaysService, goes to the provider, which holds
// the bank's contents itself. The machine keeps no copy.
//
// A provider numbers its banks from 0 (a cartridge: 0 on the slot's even
// bank, ROMQA 0, and 1 on the odd one). A ROM provider ignores writes; a RAM
// one takes them. A provider that also decodes I/O (a FRED/JIM cartridge)
// is the same object with its bus handlers beside.

#pragma once

#include "ConfigurableSlot.hpp"

#include <cstdint>

namespace beebium {

class SidewaysBankProvider {
public:
    virtual ~SidewaysBankProvider() = default;

    // A CPU read of byte `offset` (0-&3FFF) of the provider's bank `bank`.
    virtual uint8_t read_bank(uint8_t bank, uint16_t offset) = 0;

    // The same byte without side effects, for the debugger and SidewaysService.
    virtual uint8_t peek_bank(uint8_t bank, uint16_t offset) const = 0;

    // A write to the bank: a ROM ignores it.
    virtual void write_bank(uint8_t bank, uint16_t offset, uint8_t value) = 0;

    // What the bank is, for SidewaysService and the Memory sidebar: its type
    // (Rom, Ram), whether it holds anything, and its image's name.
    virtual SlotInfo bank_info(uint8_t bank) const = 0;
};

}  // namespace beebium
