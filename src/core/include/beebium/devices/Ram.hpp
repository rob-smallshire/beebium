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

#include <array>
#include <cstdint>
#include <cstring>

namespace beebium {

// Simple RAM device with compile-time size
template<size_t Size>
class Ram {
    std::array<uint8_t, Size> data_{};

public:
    static constexpr size_t size = Size;

    Ram() = default;

    // Initialize with fill value
    explicit Ram(uint8_t fill_value) {
        data_.fill(fill_value);
    }

    uint8_t read(uint16_t offset) const {
        return data_[offset % Size];
    }

    void write(uint16_t offset, uint8_t value) {
        data_[offset % Size] = value;
    }

    // Direct access for initialization/testing
    uint8_t* data() noexcept { return data_.data(); }
    const uint8_t* data() const noexcept { return data_.data(); }

    // Load data into RAM (e.g., for testing)
    void load(const uint8_t* src, size_t len, size_t offset = 0) {
        if (offset < Size) {
            size_t copy_len = std::min(len, Size - offset);
            std::memcpy(data_.data() + offset, src, copy_len);
        }
    }

    // Clear RAM
    void clear() {
        data_.fill(0);
    }
};

} // namespace beebium
