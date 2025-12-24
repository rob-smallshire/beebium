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

#ifndef BEEBIUM_FRAME_ALLOCATOR_HPP
#define BEEBIUM_FRAME_ALLOCATOR_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <memory>
#include <vector>

namespace beebium {

// Standard BBC Micro video dimensions
namespace video_constants {
    // Frame buffer dimensions (accommodates all modes including wide games)
    constexpr size_t FRAME_WIDTH = 736;
    constexpr size_t FRAME_HEIGHT = 576;  // 288 scanlines x 2 for interlace
    constexpr size_t FRAME_STRIDE = FRAME_WIDTH * sizeof(uint32_t);  // 2944 bytes
    constexpr size_t FRAME_SIZE = FRAME_WIDTH * FRAME_HEIGHT;  // pixels
    constexpr size_t FRAME_BYTES = FRAME_SIZE * sizeof(uint32_t);  // ~1.7MB
}

// Abstract interface for frame buffer allocation.
// Allows pluggable allocation strategies (heap, shared memory, GPU, etc.)
class FrameAllocator {
public:
    virtual ~FrameAllocator() = default;

    // Allocate a frame buffer of the given size (in pixels).
    // Returns a span to the allocated buffer, or empty span on failure.
    virtual std::span<uint32_t> allocate(size_t pixel_count) = 0;

    // Release a previously allocated buffer.
    // After this call, the span is invalid.
    virtual void release(std::span<uint32_t> buffer) = 0;
};

// Default allocator using heap memory (std::vector).
// Simple, portable, suitable for single-process use.
class HeapFrameAllocator : public FrameAllocator {
public:
    std::span<uint32_t> allocate(size_t pixel_count) override {
        auto buffer = std::make_unique<std::vector<uint32_t>>(pixel_count, 0);
        std::span<uint32_t> span(buffer->data(), buffer->size());
        buffers_.push_back(std::move(buffer));
        return span;
    }

    void release(std::span<uint32_t> buffer) override {
        // Find and remove the buffer
        auto it = std::find_if(buffers_.begin(), buffers_.end(),
            [&](const auto& b) { return b->data() == buffer.data(); });
        if (it != buffers_.end()) {
            buffers_.erase(it);
        }
    }

private:
    std::vector<std::unique_ptr<std::vector<uint32_t>>> buffers_;
};

} // namespace beebium

#endif // BEEBIUM_FRAME_ALLOCATOR_HPP
