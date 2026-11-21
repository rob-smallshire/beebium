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

// A cartridge slot of the Master 128 (the attachment points cartridge-rear
// and cartridge-front): the port a cartridge extension's init() is given
// for the slot its instance is placed at (ExtensionContext::get<CartridgeSlot>()).
//
// A cartridge extension provides the slot's sideways banks. The slot answers
// on two banks, the rear slot on 0 and 1, the front on 2 and 3, which
// ROMSEL bit 0 (ROMQA) chooses between. The cartridge fits itself as a
// SidewaysBankProvider: its bank 0 is the slot's even bank and its bank 1
// the odd one. From then on the machine's sideways memory, the debugger and
// SidewaysService all ask the cartridge for those banks' contents, which it
// holds itself:
//
// - a ROM cartridge (AcornRomCartridgeExtension) answers reads from its images
//   and ignores writes;
// - a RAM cartridge would be the same provider taking writes;
// - a cartridge that also decodes FRED and JIM on the cartridge bus would add
//   its bus handlers to the same extension.

#pragma once

#include "beebium/devices/SidewaysBankProvider.hpp"

#include <cstdint>

namespace beebium {

class CartridgeSlot {
public:
    virtual ~CartridgeSlot() = default;

    // The slot's even bank (0 for the rear slot, 2 for the front).
    virtual uint8_t first_bank() const = 0;

    // Fit a cartridge: `cartridge` answers for the slot's two banks, its
    // bank 0 on the even one and its bank 1 on the odd one, until remove().
    // The cartridge must outlive its fitting.
    virtual void fit(SidewaysBankProvider& cartridge) = 0;

    // Take the cartridge out: both banks empty.
    virtual void remove() = 0;
};

}  // namespace beebium
