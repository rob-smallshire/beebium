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
#include "connection/StreamReader.hpp"
#include "metrics/IntervalStats.hpp"

#include "video.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace beebium::linux_client {

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

    VideoStream(const VideoStream&) = delete;
    VideoStream& operator=(const VideoStream&) = delete;

    void start() { reader_.start(); }

    // Cancel the call and join the reader. Idempotent; safe before start().
    void stop() { reader_.stop(); }

    std::uint64_t frames_received() const { return frames_received_.load(); }
    std::uint64_t frames_refused() const { return frames_refused_.load(); }

    // Time between consecutive frames' arrival, for pacing measurements.
    IntervalRecorder& arrival_intervals() { return arrival_intervals_; }

private:
    void on_frame(beebium::Frame& wire);

    FrameMailbox& mailbox_;
    std::function<void()> frame_ready_;
    std::atomic<std::uint64_t> frames_received_{0};
    std::atomic<std::uint64_t> frames_refused_{0};
    IntervalRecorder arrival_intervals_;
    std::optional<std::chrono::steady_clock::time_point> previous_arrival_;  // reader thread only
    StreamReader<beebium::Frame> reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_VIDEO_STREAM_HPP
