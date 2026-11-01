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

#include "connection/VideoStream.hpp"

#include "video.grpc.pb.h"

#include <chrono>

namespace beebium::linux_client {

VideoStream::VideoStream(std::shared_ptr<grpc::Channel> channel, FrameMailbox& mailbox,
                         Callbacks callbacks)
    : channel_(std::move(channel)), mailbox_(mailbox), callbacks_(std::move(callbacks)) {}

VideoStream::~VideoStream() {
    stop();
}

void VideoStream::start() {
    if (reader_.joinable() || stop_requested_.load()) {
        return;
    }
    reader_ = std::thread([this] { run(); });
}

void VideoStream::stop() {
    stop_requested_.store(true);
    context_.TryCancel();
    if (reader_.joinable()) {
        reader_.join();
    }
}

void VideoStream::run() {
    auto stub = beebium::VideoService::NewStub(channel_);
    beebium::SubscribeFramesRequest request;
    auto reader = stub->SubscribeFrames(&context_, request);

    beebium::Frame wire;
    while (reader->Read(&wire)) {
        const auto received_at = std::chrono::steady_clock::now();
        auto decoded = decode_frame(wire, received_at);
        if (auto* frame = std::get_if<std::unique_ptr<DecodedFrame>>(&decoded)) {
            frames_received_.fetch_add(1);
            if (mailbox_.publish(std::move(*frame)) && callbacks_.frame_ready) {
                callbacks_.frame_ready();
            }
        } else {
            // A malformed frame is skipped, never drawn; the count makes it
            // visible rather than silent.
            frames_refused_.fetch_add(1);
        }
    }

    const grpc::Status status = reader->Finish();
    StreamEnd end;
    end.code = status.error_code();
    end.message = status.error_message();
    end.cancelled_by_client = stop_requested_.load();
    if (callbacks_.finished) {
        callbacks_.finished(end);
    }
}

}  // namespace beebium::linux_client
