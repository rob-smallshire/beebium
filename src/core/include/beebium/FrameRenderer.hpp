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

#ifndef BEEBIUM_FRAME_RENDERER_HPP
#define BEEBIUM_FRAME_RENDERER_HPP

#include "PixelBatch.hpp"
#include "OutputQueue.hpp"
#include "FrameBuffer.hpp"
#include <cstdint>

namespace beebium {

// Converts PixelBatch stream to pixel framebuffer.
//
// The FrameRenderer consumes PixelBatches from an OutputQueue,
// converts them to BGRA32 pixels, and writes them to a FrameBuffer.
// It handles sync signals (HSYNC/VSYNC) to track raster position.
//
// This is an optional convenience component. Clients that want
// raw PixelBatch access (e.g., for CRT shaders) can consume
// the queue directly without using FrameRenderer.
//
class FrameRenderer {
public:
    explicit FrameRenderer(FrameBuffer* frame_buffer)
        : frame_buffer_(frame_buffer)
        , x_(0)
        , y_(0)
        , in_vsync_(false)
        , in_hsync_(false)
    {}

    // Process a batch of PixelBatches from the queue.
    // Returns number of batches consumed.
    // Should be called periodically (e.g., in render thread).
    size_t process(OutputQueue<PixelBatch>& queue, size_t max_units = 1000) {
        auto buffers = queue.get_consumer_buffer();
        if (buffers.empty()) {
            return 0;
        }

        size_t consumed = 0;
        size_t to_consume = std::min(max_units, buffers.total());

        // Process buffer A
        for (size_t i = 0; i < std::min(to_consume, buffers.a.size()); ++i) {
            process_unit(buffers.a[i]);
            ++consumed;
        }

        // Process buffer B (wrap-around portion)
        if (consumed < to_consume && !buffers.b.empty()) {
            for (size_t i = 0; i < std::min(to_consume - consumed, buffers.b.size()); ++i) {
                process_unit(buffers.b[i]);
                ++consumed;
            }
        }

        queue.consume(consumed);
        return consumed;
    }

    // Process a single PixelBatch
    void process_unit(const PixelBatch& batch) {
        uint8_t flags = batch.flags();
        bool vsync = (flags & VIDEO_FLAG_VSYNC) != 0;
        bool hsync = (flags & VIDEO_FLAG_HSYNC) != 0;
        bool display = (flags & VIDEO_FLAG_DISPLAY) != 0;

        // Handle VSYNC
        if (vsync && !in_vsync_) {
            // Rising edge of VSYNC - end of frame
            frame_buffer_->swap();
            y_ = 0;
        }
        in_vsync_ = vsync;

        // Handle HSYNC
        if (hsync && !in_hsync_) {
            // Rising edge of HSYNC - end of scanline
            x_ = 0;
            ++y_;
            if (y_ >= frame_buffer_->height()) {
                y_ = 0;  // Wrap around if we exceed buffer
            }
        }
        in_hsync_ = hsync;

        // Only write pixels during display enable
        if (!display) {
            return;
        }

        // Convert PixelBatch pixels to BGRA32 and write to framebuffer
        if (x_ + 8 <= frame_buffer_->width() && y_ < frame_buffer_->height()) {
            uint32_t* dest = frame_buffer_->write_ptr(x_, y_);
            for (int i = 0; i < 8; ++i) {
                dest[i] = pixel_to_bgra32(batch.pixels.pixels[i]);
            }
        }

        x_ += 8;  // Each batch is 8 pixels
    }

    // Get current raster position (for debugging)
    size_t x() const { return x_; }
    size_t y() const { return y_; }

    // Reset renderer state
    void reset() {
        x_ = 0;
        y_ = 0;
        in_vsync_ = false;
        in_hsync_ = false;
    }

private:
    // Convert a 4-bit-per-channel VideoDataPixel to BGRA32
    static uint32_t pixel_to_bgra32(VideoDataPixel pixel) {
        // VideoDataPixel: bits 0-3 blue, 4-7 green, 8-11 red
        // BGRA32: bits 0-7 blue, 8-15 green, 16-23 red, 24-31 alpha
        uint8_t b = (pixel.bits.b << 4) | pixel.bits.b;  // 4-bit to 8-bit
        uint8_t g = (pixel.bits.g << 4) | pixel.bits.g;
        uint8_t r = (pixel.bits.r << 4) | pixel.bits.r;
        return (0xFF << 24) | (r << 16) | (g << 8) | b;
    }

    FrameBuffer* frame_buffer_;
    size_t x_;  // Current horizontal pixel position
    size_t y_;  // Current scanline
    bool in_vsync_;
    bool in_hsync_;
};

} // namespace beebium

#endif // BEEBIUM_FRAME_RENDERER_HPP
