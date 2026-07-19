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
#include <cassert>
#include <vector>

namespace beebium {

// A horizontal region of scanlines sharing the same logical pixel width.
// Used for split-screen modes where the CRTC is reprogrammed mid-frame
// (e.g., Elite uses MODE 4 upper / MODE 5 lower).
// Also carries the character geometry those scanlines were drawn with, which
// the video path has no use for but reading text off the screen does: cell size
// and grid pitch cannot be inferred from the pixels afterwards. A region breaks
// on a change to any of these, so a band is a run of scanlines sharing one
// character geometry as well as one pixel width.
struct FrameDisplayRegion {
    uint32_t start_line = 0;    // First scanline (inclusive, 0-based)
    uint32_t end_line = 0;      // Last scanline (exclusive)
    uint32_t pixel_width = 0;   // Logical pixel width for scanlines in this region

    // Scanlines per character row, from CRTC R9 + 1. The grid pitch, which is
    // not the cell height: MODE 3 and MODE 6 put an eight-scanline glyph on a
    // ten-scanline pitch and blank the two spare lines.
    uint32_t char_scanlines = 0;

    // True when the SAA5050 was driving these scanlines rather than the Video
    // ULA, so their characters are recoverable exactly rather than by
    // recognising glyphs in pixels.
    bool is_teletext = false;
};

// Per-frame metadata describing frame dimensions and scaling.
// The physical buffer may be larger (fixed allocation), but only
// width × height pixels contain valid content for this frame.
struct FrameMetadata {
    uint32_t width = 640;          // Frame width in logical pixels
    uint32_t height = 512;         // Frame height in scanlines
    uint64_t frame_number = 0;     // Incrementing frame counter
    bool interlaced = false;       // True for MODE 7 and custom interlace modes

    // Target display resolution after scaling.
    // BBC Micro displays all modes at the same physical size:
    // - MODE 0: 640×256 logical = 640×256 display (1:1)
    // - MODE 1: 320×256 logical = 640×256 display (2:1 horizontal)
    // - MODE 2: 160×256 logical = 640×256 display (4:1 horizontal)
    // Clients should scale width→display_width, height→display_height
    // Note: FrameRenderer sets display_height = frame_height at swap time
    uint32_t display_width = 640;  // Target display width in pixels
    uint32_t display_height = 256; // Target display height (set by FrameRenderer)

    // Border dimensions (blanking area around active content)
    // These come from CRTC timing and allow clients to render
    // with authentic CRT-style borders if desired.
    uint32_t left_border = 0;      // Pixels from left edge to active area
    uint32_t right_border = 0;     // Pixels from active area to right edge
    uint32_t top_border = 0;       // Scanlines from top to active area
    uint32_t bottom_border = 0;    // Scanlines from active area to bottom

    // Display regions for split-screen modes.
    // Always populated with at least one region.
    // Each region describes a band of scanlines with its own logical pixel width.
    std::vector<FrameDisplayRegion> regions;
};

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
                         size_t max_width = video_constants::FRAME_WIDTH,
                         size_t max_height = video_constants::FRAME_HEIGHT)
        : capacity_width_(max_width)
        , capacity_height_(max_height)
        , width_(max_width)
        , height_(max_height)
        , allocator_(allocator)
        , owns_allocator_(allocator == nullptr)
    {
        if (owns_allocator_) {
            default_allocator_ = std::make_unique<HeapFrameAllocator>();
            allocator_ = default_allocator_.get();
        }

        // Allocate once at maximum size - never reallocate
        size_t pixel_count = max_width * max_height;
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

    // Get write pointer at specific (x, y) position.
    // Uses capacity_width_ for stride (row spacing).
    uint32_t* write_ptr(size_t x, size_t y) {
        return front_.data() + (y * capacity_width_ + x);
    }

    // Write a single pixel
    void write_pixel(size_t x, size_t y, uint32_t color) {
        if (x < capacity_width_ && y < capacity_height_) {
            front_[y * capacity_width_ + x] = color;
        }
    }

    // Write a row of pixels
    void write_row(size_t y, const uint32_t* pixels, size_t count) {
        if (y < capacity_height_ && count <= capacity_width_) {
            std::copy(pixels, pixels + count, front_.data() + y * capacity_width_);
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

    // Logical dimensions (current frame content size)
    size_t width() const { return width_; }
    size_t height() const { return height_; }

    // Physical capacity (maximum allocated size, never changes)
    size_t capacity_width() const { return capacity_width_; }
    size_t capacity_height() const { return capacity_height_; }

    // Stride is based on physical capacity (row spacing in pixels)
    size_t stride() const { return capacity_width_ * sizeof(uint32_t); }
    size_t stride_pixels() const { return capacity_width_; }

    // Total capacity (physical allocation)
    size_t capacity_pixels() const { return capacity_width_ * capacity_height_; }
    size_t capacity_bytes() const { return capacity_pixels() * sizeof(uint32_t); }

    // Logical frame size (content only)
    size_t pixel_count() const { return width_ * height_; }
    size_t byte_size() const { return pixel_count() * sizeof(uint32_t); }

    // --- Dimension management ---

    // Set logical dimensions for current frame (must fit in capacity).
    // Called at frame swap to record the actual frame size.
    void set_dimensions(size_t width, size_t height) {
        assert(width <= capacity_width_ && height <= capacity_height_);
        width_ = width;
        height_ = height;
    }

    // --- Metadata interface ---

    void set_metadata(const FrameMetadata& meta) { metadata_ = meta; }
    const FrameMetadata& metadata() const { return metadata_; }

private:
    // Physical allocation (fixed at construction, never changes)
    size_t capacity_width_;
    size_t capacity_height_;

    // Logical dimensions (can change each frame)
    size_t width_;
    size_t height_;
    FrameAllocator* allocator_;
    bool owns_allocator_;
    std::unique_ptr<HeapFrameAllocator> default_allocator_;

    std::span<uint32_t> front_;  // Core writes here during rendering
    std::span<uint32_t> back_;   // Clients read here (immutable between swaps)

    mutable std::mutex mutex_;   // Protects swap operations
    std::atomic<uint64_t> version_{0};  // Frame version counter

    FrameMetadata metadata_;  // Per-frame metadata (updated at swap)
};

} // namespace beebium

#endif // BEEBIUM_FRAME_BUFFER_HPP
