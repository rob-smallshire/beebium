// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include "BusStretching.hpp"

#include <cstdint>
#include <concepts>
#include <tuple>
#include <type_traits>

namespace beebium {

// Concept for any device that can be memory-mapped
template<typename T>
concept MemoryMappedDevice = requires(T& device, uint16_t offset, uint8_t value) {
    { device.read(offset) } -> std::convertible_to<uint8_t>;
    { device.write(offset, value) } -> std::same_as<void>;
};

// A device that can also be read without side effects, for inspection: a peek
// leaves interrupt flags, FIFOs and data-ready latches as they were.
template<typename T>
concept PeekableDevice = requires(const T& device, uint16_t offset) {
    { device.peek(offset) } -> std::convertible_to<uint8_t>;
};

// Mirror policies for address decoding

struct NoMirror {
    static constexpr uint16_t apply(uint16_t offset) noexcept { return offset; }
};

template<uint16_t Mask>
struct Mirror {
    static constexpr uint16_t apply(uint16_t offset) noexcept { return offset & Mask; }
};

// Region template: binds an address range to a device reference
template<uint16_t Base, uint16_t End, typename MirrorPolicy = NoMirror>
struct Region {
    static_assert(Base <= End, "Region base must be <= end");

    static constexpr uint16_t base = Base;
    static constexpr uint16_t end = End;
    static constexpr uint16_t size = End - Base + 1;

    template<MemoryMappedDevice Device>
    struct Binding {
        Device& device;

        constexpr bool contains(uint16_t addr) const noexcept {
            return addr >= Base && addr <= End;
        }

        uint8_t read(uint16_t addr) const {
            return device.read(MirrorPolicy::apply(addr - Base));
        }

        void write(uint16_t addr, uint8_t value) {
            device.write(MirrorPolicy::apply(addr - Base), value);
        }

        // A read for inspection: the device's peek where it has one, else its
        // read, which then must have no side effects.
        uint8_t peek(uint16_t addr) const {
            if constexpr (PeekableDevice<Device>) {
                return device.peek(MirrorPolicy::apply(addr - Base));
            } else {
                return device.read(MirrorPolicy::apply(addr - Base));
            }
        }
    };

    template<MemoryMappedDevice Device>
    static constexpr Binding<Device> bind(Device& device) {
        return Binding<Device>{device};
    }
};

// Helper to create a region binding with deduced device type
template<uint16_t Base, uint16_t End, typename MirrorPolicy = NoMirror, MemoryMappedDevice Device>
constexpr auto make_region(Device& device) {
    return Region<Base, End, MirrorPolicy>::bind(device);
}

namespace detail {

// Recursive dispatch for read with open bus behavior
template<size_t I, typename Tuple>
uint8_t dispatch_read(const Tuple& regions, uint16_t addr,
                      uint8_t last_bus_value, OpenBusMode mode) {
    if constexpr (I < std::tuple_size_v<Tuple>) {
        const auto& region = std::get<I>(regions);
        if (region.contains(addr)) {
            return region.read(addr);
        }
        return dispatch_read<I + 1>(regions, addr, last_bus_value, mode);
    } else {
        // Unmapped address: return value based on bus speed and mode
        return unmapped_read_value(addr, last_bus_value, mode);
    }
}

// Recursive dispatch for peek: as read, but through each device's peek
template<size_t I, typename Tuple>
uint8_t dispatch_peek(const Tuple& regions, uint16_t addr,
                      uint8_t last_bus_value, OpenBusMode mode) {
    if constexpr (I < std::tuple_size_v<Tuple>) {
        const auto& region = std::get<I>(regions);
        if (region.contains(addr)) {
            return region.peek(addr);
        }
        return dispatch_peek<I + 1>(regions, addr, last_bus_value, mode);
    } else {
        return unmapped_read_value(addr, last_bus_value, mode);
    }
}

// Recursive dispatch for write
template<size_t I, typename Tuple>
void dispatch_write(Tuple& regions, uint16_t addr, uint8_t value) {
    if constexpr (I < std::tuple_size_v<Tuple>) {
        auto& region = std::get<I>(regions);
        if (region.contains(addr)) {
            region.write(addr, value);
            return;
        }
        dispatch_write<I + 1>(regions, addr, value);
    }
    // Unmapped write: silently ignored
}

} // namespace detail

// MemoryMap: variadic template composing multiple region bindings
// First matching region wins (order in constructor determines priority)
//
// The memory map tracks the last value on the data bus for accurate open bus
// emulation. When reading from unmapped addresses, the returned value depends
// on the bus speed characteristics (1MHz vs 2MHz) and the configured mode.
template<typename... RegionBindings>
class MemoryMap {
    std::tuple<RegionBindings...> regions_;

    // Last value driven on the data bus (for open bus emulation).
    // Marked mutable because tracking is internal bookkeeping, not observable state.
    mutable uint8_t last_bus_value_ = 0xFF;

    // Open bus behavior mode (configurable for compatibility testing)
    OpenBusMode open_bus_mode_ = OpenBusMode::Accurate;

public:
    explicit MemoryMap(RegionBindings... regions)
        : regions_{std::move(regions)...} {}

    // Read from memory, tracking bus value for open bus emulation.
    // For unmapped addresses, the returned value depends on bus speed and mode.
    uint8_t read(uint16_t addr) const {
        uint8_t value = detail::dispatch_read<0>(regions_, addr, last_bus_value_, open_bus_mode_);
        last_bus_value_ = value;
        return value;
    }

    // Read for inspection: no device side effects, and the bus value is left
    // as the CPU last drove it.
    uint8_t peek(uint16_t addr) const {
        return detail::dispatch_peek<0>(regions_, addr, last_bus_value_, open_bus_mode_);
    }

    // Write to memory, updating the bus value.
    // The CPU drives the data bus during writes, which affects subsequent open bus reads.
    void write(uint16_t addr, uint8_t value) {
        last_bus_value_ = value;
        detail::dispatch_write<0>(regions_, addr, value);
    }

    // Allow read-only access for debugging/inspection
    uint8_t operator[](uint16_t addr) const {
        return read(addr);
    }

    // Configure open bus behavior mode
    void set_open_bus_mode(OpenBusMode mode) {
        open_bus_mode_ = mode;
    }

    // Get current open bus behavior mode
    OpenBusMode open_bus_mode() const {
        return open_bus_mode_;
    }

    // Get last bus value (for debugging/testing)
    uint8_t last_bus_value() const {
        return last_bus_value_;
    }

    // Get pointer to last bus value for open bus emulation by memory-mapped devices.
    // The pointer remains valid for the lifetime of this MemoryMap.
    const uint8_t* last_bus_value_ptr() const {
        return &last_bus_value_;
    }
};


// Deduction guide for MemoryMap
template<typename... RegionBindings>
MemoryMap(RegionBindings...) -> MemoryMap<RegionBindings...>;

} // namespace beebium
