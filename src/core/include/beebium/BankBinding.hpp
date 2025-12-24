// Copyright ©️ 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include "MemoryMap.hpp"  // for MemoryMappedDevice concept
#include <cstddef>
#include <cstdint>

namespace beebium {

// Bank binding: associates a bank index (0-15) with a device reference.
// Analogous to Region<Base, End> for MemoryMap, but indexes into banks
// rather than address ranges.
//
// Usage:
//   Rom<16384> basic_rom;
//   auto binding = make_bank<0>(basic_rom);
//   // Or: auto binding = Bank<0>::bind(basic_rom);
//
template<size_t Index>
struct Bank {
    static_assert(Index < 16, "Bank index must be 0-15");
    static constexpr size_t index = Index;

    template<MemoryMappedDevice Device>
    struct Binding {
        static constexpr size_t bank_index = Index;
        using device_type = Device;

        Device& device;

        uint8_t read(uint16_t offset) const {
            return device.read(offset);
        }

        void write(uint16_t offset, uint8_t value) {
            device.write(offset, value);
        }
    };

    template<MemoryMappedDevice Device>
    static constexpr Binding<Device> bind(Device& device) {
        return Binding<Device>{device};
    }
};

// Helper function for ergonomic API (mirrors make_region)
template<size_t Index, MemoryMappedDevice Device>
constexpr auto make_bank(Device& device) {
    return Bank<Index>::bind(device);
}

} // namespace beebium
