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
#include <span>

namespace beebium {

// Simple ROM device with compile-time size
// Writes are silently ignored
template<size_t Size>
class Rom {
    std::array<uint8_t, Size> data_{};

public:
    static constexpr size_t size = Size;

    Rom() = default;

    // Initialize from data
    explicit Rom(std::span<const uint8_t> src) {
        load(src);
    }

    uint8_t read(uint16_t offset) const {
        return data_[offset % Size];
    }

    void write(uint16_t /*offset*/, uint8_t /*value*/) {
        // ROM: writes are ignored
    }

    // Direct access for initialization
    uint8_t* data() noexcept { return data_.data(); }
    const uint8_t* data() const noexcept { return data_.data(); }

    // Load ROM image
    void load(std::span<const uint8_t> src) {
        size_t copy_len = std::min(src.size(), Size);
        std::memcpy(data_.data(), src.data(), copy_len);
    }

    void load(const uint8_t* src, size_t len) {
        load(std::span<const uint8_t>(src, len));
    }
};

} // namespace beebium
