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
