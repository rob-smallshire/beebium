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
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <span>
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
    // Emulated CPU cycle at which the frame completed: the vsync rising edge
    // that ended its (last) field. 0 when unknown -- no stamp source wired, or
    // the edge's stamp was lost.
    uint64_t cycle_count = 0;
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
// - swap(meta): Called by core at VSYNC; publishes the pixels, the metadata
//   and the new version together under the lock
// - read_frame(meta, pixels), metadata(), width(), height(): Called by
//   clients; each takes the lock, so what it returns belongs to one frame
// - capture_frame_after(): Called by clients; answered as frames are published
// - version(): Lock-free read of atomic counter, for noticing a new frame
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

    // Publish the completed frame: swap front and back buffers, install the
    // frame's metadata and logical dimensions, and increment the version, all
    // under one lock, so no reader sees one frame's metadata with another's
    // pixels. Any capture waiting for this frame is answered here, before the
    // next frame can replace it.
    void swap(FrameMetadata meta) {
        assert(meta.width <= capacity_width_ && meta.height <= capacity_height_);
        std::lock_guard<std::mutex> lock(mutex_);
        std::swap(front_, back_);
        width_ = meta.width;
        height_ = meta.height;
        metadata_ = std::move(meta);
        version_.fetch_add(1, std::memory_order_release);

        bool answered = false;
        for (PendingCapture* capture : pending_captures_) {
            if (!capture->done && metadata_.cycle_count >= capture->after_cycle) {
                copy_published_locked(capture->meta, capture->pixels);
                capture->done = true;
                answered = true;
            }
        }
        if (answered) {
            captured_.notify_all();
        }
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

    // Read the latest published frame whole: its metadata and as many of its
    // pixels as fit, taken under one lock. Returns its version, which is 0
    // before any frame has been published.
    uint64_t read_frame(FrameMetadata& meta, std::span<uint32_t> pixels) const {
        std::lock_guard<std::mutex> lock(mutex_);
        copy_published_locked(meta, pixels);
        return version_.load(std::memory_order_relaxed);
    }

    // Get the frame version counter.
    // Incremented each time swap() is called.
    // Clients can poll this to detect new frames without locking.
    uint64_t version() const {
        return version_.load(std::memory_order_acquire);
    }

    enum class CaptureResult { Captured, TimedOut, Cancelled };

    // Wait for the first frame completed at or after `after_cycle` (by its
    // metadata's cycle_count) and copy it out. The latest published frame is
    // taken if it already qualifies; otherwise the request is answered by
    // swap() as frames are published, so the frame returned is the first to
    // qualify however late this thread runs. Gives up at `deadline`, or when
    // `cancelled` returns true (it is polled while waiting).
    CaptureResult capture_frame_after(uint64_t after_cycle,
                                      std::chrono::steady_clock::time_point deadline,
                                      const std::function<bool()>& cancelled,
                                      FrameMetadata& meta,
                                      std::span<uint32_t> pixels) {
        // How often a waiting capture looks at `cancelled`. Frames are not
        // missed between looks: swap() answers the capture itself.
        constexpr auto kCancelPoll = std::chrono::milliseconds(50);

        std::unique_lock<std::mutex> lock(mutex_);
        if (version_.load(std::memory_order_relaxed) != 0
            && metadata_.cycle_count >= after_cycle) {
            copy_published_locked(meta, pixels);
            return CaptureResult::Captured;
        }

        PendingCapture capture{after_cycle, meta, pixels};
        pending_captures_.push_back(&capture);
        CaptureResult result = CaptureResult::TimedOut;
        while (true) {
            if (capture.done) {
                result = CaptureResult::Captured;
                break;
            }
            if (cancelled()) {
                result = CaptureResult::Cancelled;
                break;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                break;
            }
            captured_.wait_until(lock, std::min(deadline, now + kCancelPoll));
        }
        pending_captures_.remove(&capture);
        return result;
    }

    // Captures waiting for a frame (for tests).
    size_t pending_captures() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_captures_.size();
    }

    // --- Query interface ---

    // Logical dimensions (the latest published frame's content size)
    size_t width() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return width_;
    }
    size_t height() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return height_;
    }

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
    size_t pixel_count() const { return width() * height(); }
    size_t byte_size() const { return pixel_count() * sizeof(uint32_t); }

    // --- Metadata interface ---

    // The latest published frame's metadata, copied under the lock.
    FrameMetadata metadata() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return metadata_;
    }

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

    // A capture_frame_after() call waiting for a frame, answered by swap().
    struct PendingCapture {
        uint64_t after_cycle;
        FrameMetadata& meta;
        std::span<uint32_t> pixels;
        bool done = false;
    };

    // Copy the published frame out; the caller holds mutex_.
    void copy_published_locked(FrameMetadata& meta, std::span<uint32_t> pixels) const {
        meta = metadata_;
        const size_t count = std::min(pixels.size(), back_.size());
        std::copy(back_.begin(), back_.begin() + count, pixels.begin());
    }

    // Guards back_, metadata_, width_ and height_, and the pending captures:
    // everything swap() publishes.
    mutable std::mutex mutex_;
    std::atomic<uint64_t> version_{0};  // Frame version counter

    FrameMetadata metadata_;  // The published frame's metadata (set by swap)

    std::list<PendingCapture*> pending_captures_;
    std::condition_variable captured_;  // Signalled when swap() answers a capture
};

} // namespace beebium

#endif // BEEBIUM_FRAME_BUFFER_HPP
