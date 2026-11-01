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

#include "audio/FrameRing.hpp"

#include <algorithm>
#include <cstring>

namespace beebium::linux_client {

FrameRing::FrameRing(std::size_t capacity_frames, std::size_t fields_per_frame)
    : capacity_(std::max<std::size_t>(capacity_frames, 1)),
      fields_(std::max<std::size_t>(fields_per_frame, 1)),
      words_(capacity_ * fields_, 0) {}

std::size_t FrameRing::available() const {
    return static_cast<std::size_t>(write_index_.load(std::memory_order_acquire) -
                                    read_index_.load(std::memory_order_acquire));
}

std::size_t FrameRing::write(const std::uint32_t* frames, std::size_t count) {
    const std::uint64_t write = write_index_.load(std::memory_order_relaxed);
    const std::uint64_t read = read_index_.load(std::memory_order_acquire);
    const std::size_t space = capacity_ - static_cast<std::size_t>(write - read);
    const std::size_t taken = std::min(count, space);
    if (taken < count) {
        dropped_.fetch_add(count - taken, std::memory_order_relaxed);
    }
    const std::size_t start = static_cast<std::size_t>(write % capacity_);
    const std::size_t first = std::min(taken, capacity_ - start);
    std::memcpy(&words_[start * fields_], frames, first * fields_ * sizeof(std::uint32_t));
    std::memcpy(words_.data(), frames + first * fields_,
                (taken - first) * fields_ * sizeof(std::uint32_t));
    write_index_.store(write + taken, std::memory_order_release);
    return taken;
}

std::size_t FrameRing::read(std::uint32_t* destination, std::size_t count) {
    const std::uint64_t read = read_index_.load(std::memory_order_relaxed);
    const std::uint64_t write = write_index_.load(std::memory_order_acquire);
    const std::size_t held = static_cast<std::size_t>(write - read);
    const std::size_t taken = std::min(count, held);
    if (taken < count) {
        underrun_frames_.fetch_add(count - taken, std::memory_order_relaxed);
        underrun_events_.fetch_add(1, std::memory_order_relaxed);
    }
    const std::size_t start = static_cast<std::size_t>(read % capacity_);
    const std::size_t first = std::min(taken, capacity_ - start);
    std::memcpy(destination, &words_[start * fields_], first * fields_ * sizeof(std::uint32_t));
    std::memcpy(destination + first * fields_, words_.data(),
                (taken - first) * fields_ * sizeof(std::uint32_t));
    read_index_.store(read + taken, std::memory_order_release);
    return taken;
}

}  // namespace beebium::linux_client
