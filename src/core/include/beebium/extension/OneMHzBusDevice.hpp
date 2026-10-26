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

#ifndef BEEBIUM_EXTENSION_ONE_MHZ_BUS_DEVICE_HPP
#define BEEBIUM_EXTENSION_ONE_MHZ_BUS_DEVICE_HPP

#include "Export.hpp"
#include "../ResetCause.hpp"

#include <cstdint>

namespace beebium {

// Callback interface for devices attached to the 1 MHz bus (FRED/JIM region).
//
// FRED: read() and write() receive an offset relative to 0xFC00, matching the
// offset that MemoryMap's Region binding computes (addr - Base), for the FRED
// offsets the device claimed with OneMHzBusPort::claim_addresses().
//
// JIM: the port owns the JIM paging register at 0xFCFF and routes an access to
// page 0xFD to the device that claimed the current extended page with
// OneMHzBusPort::claim_jim_pages(). jim_read() and jim_write() receive that
// page number and the offset 0x00-0xFF within page 0xFD. The defaults model a
// device that does not decode JIM: reads float (0xFF) and writes are ignored.
struct BEEBIUM_EXT_API OneMHzBusDevice {
    virtual ~OneMHzBusDevice() = default;

    virtual uint8_t read(uint16_t offset) = 0;
    virtual void write(uint16_t offset, uint8_t value) = 0;
    virtual uint8_t jim_read(uint8_t /*page*/, uint8_t /*offset*/) { return 0xFF; }
    virtual void jim_write(uint8_t /*page*/, uint8_t /*offset*/, uint8_t /*value*/) {}
    virtual void tick() {}

    // The bus's NRST line, asserted on power-up and on BREAK (Advanced User
    // Guide, the one megahertz bus, NRST pin 14). Devices that need no reset
    // ignore it.
    virtual void reset(ResetCause /*cause*/) {}

    // IRQ support. Devices that generate interrupts override this to
    // return true when their IRQ line is asserted. The OneMHzBusPort
    // aggregates IRQ status from all attached devices.
    virtual bool irq_pending() const { return false; }
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ONE_MHZ_BUS_DEVICE_HPP
