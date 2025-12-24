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

#ifndef BEEBIUM_FRAME_BUFFER_HPP
#define BEEBIUM_FRAME_BUFFER_HPP

#include "FrameAllocator.hpp"
#include <mutex>
#include <atomic>

namespace beebium {

// Double-buffered frame buffer for video output.
//
// The core writes to the front buffer during rendering.
// At VSYNC, swap() exchanges front and back buffers.
// Clients read from the back buffer (immutable between swaps).
//
// Thread safety:
// - write_ptr(): Called only by core (single thread), no lock needed
// - swap(): Called by core at VSYNC, acquires lock briefly
// - read_frame(): Called by clients, acquires lock briefly
// - version(): Lock-free read of atomic counter
//
class FrameBuffer {
public:
    explicit FrameBuffer(FrameAllocator* allocator = nullptr,
                         size_t width = video_constants::FRAME_WIDTH,
                         size_t height = video_constants::FRAME_HEIGHT)
        : width_(width)
        , height_(height)
        , allocator_(allocator)
        , owns_allocator_(allocator == nullptr)
    {
        if (owns_allocator_) {
            default_allocator_ = std::make_unique<HeapFrameAllocator>();
            allocator_ = default_allocator_.get();
        }

        size_t pixel_count = width * height;
        front_ = allocator_->allocate(pixel_count);
        back_ = allocator_->allocate(pixel_count);
    }

    ~FrameBuffer() {
        if (allocator_) {
            allocator_->release(front_);
            allocator_->release(back_);
        }
    }

    // Non-copyable, non-movable
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;
    FrameBuffer(FrameBuffer&&) = delete;
    FrameBuffer& operator=(FrameBuffer&&) = delete;

    // --- Core interface (called during rendering) ---

    // Get write pointer for the front buffer.
    // Core writes pixels here during rendering.
    // No lock needed - only core thread accesses front buffer.
    uint32_t* write_ptr() { return front_.data(); }

    // Get write pointer at specific (x, y) position
    uint32_t* write_ptr(size_t x, size_t y) {
        return front_.data() + (y * width_ + x);
    }

    // Write a single pixel
    void write_pixel(size_t x, size_t y, uint32_t color) {
        if (x < width_ && y < height_) {
            front_[y * width_ + x] = color;
        }
    }

    // Write a row of pixels
    void write_row(size_t y, const uint32_t* pixels, size_t count) {
        if (y < height_ && count <= width_) {
            std::copy(pixels, pixels + count, front_.data() + y * width_);
        }
    }

    // Clear front buffer to a color
    void clear(uint32_t color = 0) {
        std::fill(front_.begin(), front_.end(), color);
    }

    // --- Core interface (called at VSYNC) ---

    // Swap front and back buffers.
    // Called by core at VSYNC to publish the completed frame.
    // Increments version counter so clients can detect new frames.
    void swap() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::swap(front_, back_);
        version_.fetch_add(1, std::memory_order_release);
    }

    // --- Client interface (called by frontends) ---

    // Get read-only access to the back buffer (last complete frame).
    // Safe to call from any thread.
    std::span<const uint32_t> read_frame() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return back_;
    }

    // Copy the back buffer to a destination.
    // Useful when client needs to process the frame without holding lock.
    void copy_frame(uint32_t* dest, size_t max_pixels) const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t count = std::min(max_pixels, back_.size());
        std::copy(back_.begin(), back_.begin() + count, dest);
    }

    // Get the frame version counter.
    // Incremented each time swap() is called.
    // Clients can poll this to detect new frames without locking.
    uint64_t version() const {
        return version_.load(std::memory_order_acquire);
    }

    // --- Query interface ---

    size_t width() const { return width_; }
    size_t height() const { return height_; }
    size_t stride() const { return width_ * sizeof(uint32_t); }
    size_t pixel_count() const { return width_ * height_; }
    size_t byte_size() const { return pixel_count() * sizeof(uint32_t); }

private:
    size_t width_;
    size_t height_;
    FrameAllocator* allocator_;
    bool owns_allocator_;
    std::unique_ptr<HeapFrameAllocator> default_allocator_;

    std::span<uint32_t> front_;  // Core writes here during rendering
    std::span<uint32_t> back_;   // Clients read here (immutable between swaps)

    mutable std::mutex mutex_;   // Protects swap operations
    std::atomic<uint64_t> version_{0};  // Frame version counter
};

} // namespace beebium

#endif // BEEBIUM_FRAME_BUFFER_HPP
