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

#ifndef BEEBIUM_EXTENSION_ONE_MHZ_BUS_PORT_HPP
#define BEEBIUM_EXTENSION_ONE_MHZ_BUS_PORT_HPP

#include "Export.hpp"
#include "OneMHzBusDevice.hpp"

#include <array>
#include <concepts>
#include <cstdint>
#include <vector>

namespace beebium {

// Port handle for the 1 MHz bus (FRED/JIM region, 0xFC00-0xFDFF).
//
// Satisfies the MemoryMappedDevice concept (read/write with uint16_t offset).
//
// FRED (offsets 0x000-0x0FE): devices claim address ranges and the port
// dispatches each access to the claimant.
//
// JIM: the port owns the JIM paging register at FRED offset 0x0FF, as the bus
// does (every JIM-paged board latches the same register). A write stores the
// extended page number; a read returns 0xFF, since no board drives the bus on
// a read of it. Devices claim ranges of extended page numbers, and an access
// to page 0xFD (offsets 0x100-0x1FF) goes to the device that claimed the
// current page. The register is not cleared by reset and starts at 0xFF.
//
// Unclaimed addresses and pages return 0xFF on read (74LS245 transceiver
// behaviour) and silently ignore writes.
class BEEBIUM_EXT_API OneMHzBusPort {
public:
    static constexpr uint16_t kSize = 512;  // 0xFC00-0xFDFF
    static constexpr uint16_t kJimPagingRegisterOffset = 0x0FF;  // 0xFCFF
    static constexpr uint16_t kJimBaseOffset = 0x100;            // 0xFD00

    OneMHzBusPort() {
        fred_map_.fill(nullptr);
        jim_page_map_.fill(nullptr);
    }

    // MemoryMappedDevice interface (called by MemoryMap region binding).
    // Offset is relative to 0xFC00.

    uint8_t read(uint16_t offset) {
        if (offset < kJimPagingRegisterOffset) {
            if (auto* dev = fred_map_[offset]) {
                return dev->read(offset);
            }
            return 0xFF;
        }
        if (offset >= kJimBaseOffset) {
            if (auto* dev = jim_page_map_[jim_page_]) {
                return dev->jim_read(jim_page_, static_cast<uint8_t>(offset));
            }
        }
        return 0xFF;
    }

    void write(uint16_t offset, uint8_t value) {
        if (offset < kJimPagingRegisterOffset) {
            if (auto* dev = fred_map_[offset]) {
                dev->write(offset, value);
            }
        } else if (offset == kJimPagingRegisterOffset) {
            jim_page_ = value;
        } else if (auto* dev = jim_page_map_[jim_page_]) {
            dev->jim_write(jim_page_, static_cast<uint8_t>(offset), value);
        }
    }

    // Extension framework interface.

    // Register a device for a contiguous range of FRED offsets [base, end]
    // inclusive, relative to 0xFC00. Throws std::runtime_error if the range is
    // invalid, reaches the JIM paging register (0x0FF) or JIM (0x100 onwards),
    // or overlaps an existing claim.
    void claim_addresses(uint16_t base, uint16_t end, OneMHzBusDevice& device);

    // Register a device for a contiguous range of extended JIM page numbers
    // [first_page, last_page] inclusive. Throws std::runtime_error if the range
    // is reversed or overlaps an existing page claim.
    void claim_jim_pages(uint8_t first_page, uint8_t last_page, OneMHzBusDevice& device);

    // Tick all registered devices (called at 1 MHz from poll_nmi).
    // Invalidates the IRQ cache since device state may have changed.
    void tick();

    // Query whether any device has claimed the given FRED offset.
    bool is_claimed(uint16_t offset) const {
        return offset < kJimPagingRegisterOffset && fred_map_[offset] != nullptr;
    }

    // Query whether any device has claimed the given extended JIM page.
    bool is_jim_page_claimed(uint8_t page) const {
        return jim_page_map_[page] != nullptr;
    }

    // The current extended page number in the JIM paging register.
    uint8_t jim_page() const { return jim_page_; }

    // Poll IRQ status from all attached devices.
    // Returns true if any device on the 1 MHz bus is asserting IRQ.
    // Used by the IrqAggregator to include 1 MHz bus IRQs.
    // Caches the result between tick() calls to avoid redundant virtual
    // dispatch at 2 MHz when tick() only runs at 1 MHz.
    bool irq_pending() const;

private:
    void add_tickable(OneMHzBusDevice& device);

    std::array<OneMHzBusDevice*, kJimPagingRegisterOffset> fred_map_;
    std::array<OneMHzBusDevice*, 256> jim_page_map_;
    uint8_t jim_page_ = 0xFF;
    std::vector<OneMHzBusDevice*> tickable_devices_;
    mutable bool cached_irq_ = false;
    mutable bool irq_dirty_ = true;
};

// Concept to detect hardware that has a 1 MHz bus port.
template<typename T>
concept HasOneMHzBus = requires(T& hw) {
    { hw.one_mhz_bus() } -> std::convertible_to<OneMHzBusPort&>;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ONE_MHZ_BUS_PORT_HPP
