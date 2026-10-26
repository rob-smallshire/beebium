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

#include <beebium/extension/OneMHzBusPort.hpp>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace beebium {

namespace {
std::string to_hex(uint16_t value) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", value);
    return buf;
}
}  // anonymous namespace

void OneMHzBusPort::claim_addresses(uint16_t base, uint16_t end, OneMHzBusDevice& device) {
    if (base > end || end >= kJimPagingRegisterOffset) {
        throw std::runtime_error(
            "OneMHzBusPort: invalid FRED address range 0x"
            + to_hex(base) + "-0x" + to_hex(end)
            + " (FRED offsets 0x0000-0x00FE; the port owns the JIM paging"
              " register at 0x00FF and JIM is claimed by page)");
    }

    for (uint16_t offset = base; offset <= end; ++offset) {
        if (fred_map_[offset] != nullptr) {
            throw std::runtime_error(
                "OneMHzBusPort: address 0x" + to_hex(offset)
                + " already claimed");
        }
    }

    for (uint16_t offset = base; offset <= end; ++offset) {
        fred_map_[offset] = &device;
    }

    add_tickable(device);
}

void OneMHzBusPort::claim_jim_pages(uint8_t first_page, uint8_t last_page,
                                    OneMHzBusDevice& device) {
    if (first_page > last_page) {
        throw std::runtime_error(
            "OneMHzBusPort: invalid JIM page range 0x"
            + to_hex(first_page) + "-0x" + to_hex(last_page));
    }

    for (unsigned page = first_page; page <= last_page; ++page) {
        if (jim_page_map_[page] != nullptr) {
            throw std::runtime_error(
                "OneMHzBusPort: JIM page 0x" + to_hex(static_cast<uint16_t>(page))
                + " already claimed");
        }
    }

    for (unsigned page = first_page; page <= last_page; ++page) {
        jim_page_map_[page] = &device;
    }

    add_tickable(device);
}

void OneMHzBusPort::add_tickable(OneMHzBusDevice& device) {
    if (std::find(tickable_devices_.begin(), tickable_devices_.end(), &device)
            == tickable_devices_.end()) {
        tickable_devices_.push_back(&device);
    }
}

void OneMHzBusPort::tick() {
    for (auto* dev : tickable_devices_) {
        dev->tick();
    }
    irq_dirty_ = true;
}

void OneMHzBusPort::reset(ResetCause cause) {
    for (auto* dev : tickable_devices_) {
        dev->reset(cause);
    }
    irq_dirty_ = true;
}

bool OneMHzBusPort::irq_pending() const {
    if (irq_dirty_) {
        cached_irq_ = false;
        for (auto* dev : tickable_devices_) {
            if (dev->irq_pending()) {
                cached_irq_ = true;
                break;
            }
        }
        irq_dirty_ = false;
    }
    return cached_irq_;
}

}  // namespace beebium
