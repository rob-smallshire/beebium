// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// A completed frame is published whole (#162): its pixels, its metadata and the
// version counter change together, so a reader on another thread can never pair
// one frame's metadata with another's pixels. And a capture waiting for the
// first frame completed at or after a cycle is answered as frames are
// published, so a reader that runs late does not miss it.

#include <catch2/catch_test_macros.hpp>

#include "beebium/FrameBuffer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace beebium;

namespace {

// Small, so that publishing thousands of frames is quick.
constexpr size_t kWidth = 16;
constexpr size_t kHeight = 8;

// Draw frame n -- every pixel holds n -- and publish it, stamped with cycle
// n * 100 and numbered n, as the FrameRenderer numbers frames.
void publish_frame(FrameBuffer& frame_buffer, uint64_t n) {
    std::fill(frame_buffer.write_ptr(),
              frame_buffer.write_ptr() + frame_buffer.capacity_pixels(),
              static_cast<uint32_t>(n));
    FrameMetadata meta;
    meta.width = kWidth;
    meta.height = kHeight;
    meta.frame_number = n;
    meta.cycle_count = n * 100;
    frame_buffer.swap(meta);
}

// Wait for a capture to be registered with the frame buffer. Only thread
// start-up stands between the call and its registration, so this is bounded by
// a generous wall-clock backstop rather than by any emulated progress.
bool wait_for_pending_capture(const FrameBuffer& frame_buffer) {
    const auto backstop = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (frame_buffer.pending_captures() == 0) {
        if (std::chrono::steady_clock::now() >= backstop) return false;
        std::this_thread::yield();
    }
    return true;
}

}  // namespace

TEST_CASE("FrameBuffer publishes a frame's metadata with its pixels and version",
          "[video][frame][publication]") {
    FrameBuffer frame_buffer(nullptr, kWidth, kHeight);
    constexpr uint64_t kFrames = 20000;

    std::atomic<bool> done{false};
    std::thread renderer([&]() {
        for (uint64_t n = 1; n <= kFrames; ++n) {
            publish_frame(frame_buffer, n);
        }
        done = true;
    });

    // Read flat out while frames are published, and count every reading whose
    // parts disagree about which frame they belong to.
    uint64_t readings = 0;
    uint64_t incoherent = 0;
    FrameMetadata meta;
    std::vector<uint32_t> pixels(frame_buffer.capacity_pixels());
    while (!done) {
        const uint64_t version = frame_buffer.read_frame(meta, pixels);
        if (version == 0) continue;
        ++readings;
        const bool coherent = meta.frame_number == version
                              && pixels.front() == version
                              && pixels.back() == version
                              && meta.cycle_count == version * 100;
        if (!coherent) ++incoherent;
    }
    renderer.join();

    INFO(readings << " readings while " << kFrames << " frames were published");
    REQUIRE(readings > 0);
    CHECK(incoherent == 0);
}

TEST_CASE("FrameBuffer metadata is the published frame's",
          "[video][frame][publication]") {
    FrameBuffer frame_buffer(nullptr, kWidth, kHeight);
    publish_frame(frame_buffer, 1);
    publish_frame(frame_buffer, 2);

    CHECK(frame_buffer.version() == 2);
    CHECK(frame_buffer.metadata().frame_number == 2);
    CHECK(frame_buffer.metadata().cycle_count == 200);
    CHECK(frame_buffer.width() == kWidth);
    CHECK(frame_buffer.height() == kHeight);
}

TEST_CASE("FrameBuffer capture returns the first frame completed at or after a cycle, "
          "however late the reader looks",
          "[video][frame][capture]") {
    FrameBuffer frame_buffer(nullptr, kWidth, kHeight);
    publish_frame(frame_buffer, 1);  // cycle 100: too early to qualify

    FrameMetadata meta;
    std::vector<uint32_t> pixels(frame_buffer.capacity_pixels());
    FrameBuffer::CaptureResult result = FrameBuffer::CaptureResult::TimedOut;
    std::thread reader([&]() {
        result = frame_buffer.capture_frame_after(
            450, std::chrono::steady_clock::now() + std::chrono::seconds(60),
            [] { return false; }, meta, pixels);
    });
    REQUIRE(wait_for_pending_capture(frame_buffer));

    // Frames 2..10 complete at cycles 200..1000 before the reader is likely to
    // have woken: the first at or after 450 is frame 5, at cycle 500, and it is
    // that one the reader must get, not whichever is current when it looks.
    for (uint64_t n = 2; n <= 10; ++n) {
        publish_frame(frame_buffer, n);
    }
    reader.join();

    REQUIRE(result == FrameBuffer::CaptureResult::Captured);
    CHECK(meta.cycle_count == 500);
    CHECK(meta.frame_number == 5);
    CHECK(pixels.front() == 5);
    CHECK(pixels.back() == 5);
    CHECK(frame_buffer.pending_captures() == 0);
}

TEST_CASE("FrameBuffer capture takes the current frame when it already qualifies",
          "[video][frame][capture]") {
    FrameBuffer frame_buffer(nullptr, kWidth, kHeight);
    publish_frame(frame_buffer, 7);  // cycle 700

    FrameMetadata meta;
    std::vector<uint32_t> pixels(frame_buffer.capacity_pixels());
    const auto result = frame_buffer.capture_frame_after(
        450, std::chrono::steady_clock::now(), [] { return false; }, meta, pixels);

    REQUIRE(result == FrameBuffer::CaptureResult::Captured);
    CHECK(meta.cycle_count == 700);
    CHECK(pixels.front() == 7);
}

TEST_CASE("FrameBuffer capture with cycle 0 takes any published frame",
          "[video][frame][capture]") {
    FrameBuffer frame_buffer(nullptr, kWidth, kHeight);
    publish_frame(frame_buffer, 3);

    FrameMetadata meta;
    std::vector<uint32_t> pixels(frame_buffer.capacity_pixels());
    const auto result = frame_buffer.capture_frame_after(
        0, std::chrono::steady_clock::now(), [] { return false; }, meta, pixels);

    REQUIRE(result == FrameBuffer::CaptureResult::Captured);
    CHECK(meta.frame_number == 3);
}

TEST_CASE("FrameBuffer capture times out when no frame qualifies",
          "[video][frame][capture]") {
    FrameBuffer frame_buffer(nullptr, kWidth, kHeight);
    publish_frame(frame_buffer, 1);

    FrameMetadata meta;
    std::vector<uint32_t> pixels(frame_buffer.capacity_pixels());
    const auto result = frame_buffer.capture_frame_after(
        1'000'000, std::chrono::steady_clock::now() + std::chrono::milliseconds(50),
        [] { return false; }, meta, pixels);

    CHECK(result == FrameBuffer::CaptureResult::TimedOut);
    CHECK(frame_buffer.pending_captures() == 0);
}

TEST_CASE("FrameBuffer capture stops when cancelled",
          "[video][frame][capture]") {
    FrameBuffer frame_buffer(nullptr, kWidth, kHeight);

    FrameMetadata meta;
    std::vector<uint32_t> pixels(frame_buffer.capacity_pixels());
    const auto result = frame_buffer.capture_frame_after(
        1'000'000, std::chrono::steady_clock::now() + std::chrono::seconds(60),
        [] { return true; }, meta, pixels);

    CHECK(result == FrameBuffer::CaptureResult::Cancelled);
    CHECK(frame_buffer.pending_captures() == 0);
}
