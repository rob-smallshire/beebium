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

namespace beebium::linux_client {

VideoStream::VideoStream(std::shared_ptr<grpc::Channel> channel, FrameMailbox& mailbox,
                         Callbacks callbacks)
    : mailbox_(mailbox), frame_ready_(std::move(callbacks.frame_ready)),
      reader_("bbm-video",
              [stub = std::shared_ptr<beebium::VideoService::Stub>(
                   beebium::VideoService::NewStub(channel))](grpc::ClientContext& context) {
                  return stub->SubscribeFrames(&context, beebium::SubscribeFramesRequest{});
              },
              {[this](beebium::Frame& wire) { on_frame(wire); },
               std::move(callbacks.finished)}) {}

void VideoStream::on_frame(beebium::Frame& wire) {
    const auto received_at = std::chrono::steady_clock::now();
    if (previous_arrival_) {
        arrival_intervals_.record(
            std::chrono::duration<double, std::milli>(received_at - *previous_arrival_).count());
    }
    previous_arrival_ = received_at;
    auto decoded = decode_frame(wire, received_at);
    if (auto* frame = std::get_if<std::unique_ptr<DecodedFrame>>(&decoded)) {
        frames_received_.fetch_add(1);
        if (mailbox_.publish(std::move(*frame)) && frame_ready_) {
            frame_ready_();
        }
    } else {
        // A malformed frame is skipped, never drawn; the count makes it
        // visible rather than silent.
        frames_refused_.fetch_add(1);
    }
}

}  // namespace beebium::linux_client
