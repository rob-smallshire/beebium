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

#ifndef BEEBIUM_LINUX_CONNECTION_VIDEO_STREAM_HPP
#define BEEBIUM_LINUX_CONNECTION_VIDEO_STREAM_HPP

#include "connection/FrameMailbox.hpp"
#include "metrics/IntervalStats.hpp"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace beebium::linux_client {

// How a stream ended. cancelled_by_client separates our own stop() from a
// loss: the status code alone cannot, since a server may also end a stream
// as CANCELLED (lifecycle-management.md section 2).
struct StreamEnd {
    grpc::StatusCode code = grpc::StatusCode::OK;
    std::string message;
    bool cancelled_by_client = false;
};

// VideoService.SubscribeFrames on a thread of its own (client specification
// 6.1). Each frame is decoded and left in the mailbox; frame_ready runs on
// the reader thread when the mailbox goes from empty to full, so a consumer
// is told once per batch and takes the latest. finished runs once, on the
// reader thread, when the stream ends for any reason.
//
// The stream is never a liveness signal: frames stop while a machine is
// paused. Liveness comes from SystemService.WatchServerStatus alone.
class VideoStream {
public:
    struct Callbacks {
        std::function<void()> frame_ready;
        std::function<void(const StreamEnd&)> finished;
    };

    VideoStream(std::shared_ptr<grpc::Channel> channel, FrameMailbox& mailbox,
                Callbacks callbacks);
    ~VideoStream();

    VideoStream(const VideoStream&) = delete;
    VideoStream& operator=(const VideoStream&) = delete;

    void start();

    // Cancel the call and join the reader. Idempotent; safe before start().
    void stop();

    std::uint64_t frames_received() const { return frames_received_.load(); }
    std::uint64_t frames_refused() const { return frames_refused_.load(); }

    // Time between consecutive frames' arrival, for pacing measurements.
    IntervalRecorder& arrival_intervals() { return arrival_intervals_; }

private:
    void run();

    std::shared_ptr<grpc::Channel> channel_;
    FrameMailbox& mailbox_;
    Callbacks callbacks_;
    grpc::ClientContext context_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<std::uint64_t> frames_received_{0};
    std::atomic<std::uint64_t> frames_refused_{0};
    IntervalRecorder arrival_intervals_;
    std::thread reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_VIDEO_STREAM_HPP
