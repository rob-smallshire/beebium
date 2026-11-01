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

#ifndef BEEBIUM_LINUX_CONNECTION_FRAME_MAILBOX_HPP
#define BEEBIUM_LINUX_CONNECTION_FRAME_MAILBOX_HPP

#include "connection/DecodedFrame.hpp"

#include <cstdint>
#include <memory>
#include <mutex>

namespace beebium::linux_client {

// A single slot between the video reader thread and the render thread.
//
// Video is latest-wins: a frame superseded before the renderer took it is
// worth nothing, so publishing replaces whatever is waiting rather than
// queueing behind it, and a slow renderer drops frames instead of falling
// behind. Audio, which must not drop, does not use this.
class FrameMailbox {
public:
    // Leave a frame. Returns true if the slot was empty, which is when the
    // consumer needs telling; a frame replacing an untaken one rides on the
    // notification already sent, so notifications coalesce.
    bool publish(std::unique_ptr<DecodedFrame> frame) {
        std::lock_guard lock(mutex_);
        const bool was_empty = !waiting_;
        if (!was_empty) {
            ++superseded_count_;
        }
        waiting_ = std::move(frame);
        ++published_count_;
        return was_empty;
    }

    // Take the waiting frame, leaving the slot empty; null if none.
    std::unique_ptr<DecodedFrame> take() {
        std::lock_guard lock(mutex_);
        return std::move(waiting_);
    }

    std::uint64_t published_count() const {
        std::lock_guard lock(mutex_);
        return published_count_;
    }

    // Frames replaced before anyone took them: frames the renderer dropped.
    std::uint64_t superseded_count() const {
        std::lock_guard lock(mutex_);
        return superseded_count_;
    }

private:
    mutable std::mutex mutex_;
    std::unique_ptr<DecodedFrame> waiting_;
    std::uint64_t published_count_ = 0;
    std::uint64_t superseded_count_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_FRAME_MAILBOX_HPP
