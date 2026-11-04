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

#ifndef BEEBIUM_LINUX_CONNECTION_SIDEWAYS_EVENT_STREAM_HPP
#define BEEBIUM_LINUX_CONNECTION_SIDEWAYS_EVENT_STREAM_HPP

#include "connection/StreamReader.hpp"

#include "sideways.grpc.pb.h"

#include <functional>
#include <memory>

namespace beebium::linux_client {

// SidewaysService.SubscribeEvents on a thread of its own, opted into header
// monitoring, so a *SRLOAD into sideways RAM arrives as a new header (client
// specification 11). The monitoring costs the server a rescan, so open this
// only while the header is on show. Callbacks run on the reader thread.
class SidewaysEventStream {
public:
    static constexpr std::uint32_t MIN_INTERVAL_MS = 100;

    struct Callbacks {
        std::function<void(const beebium::SidewaysEvent&)> event;
        std::function<void(const StreamEnd&)> finished;
    };

    SidewaysEventStream(const std::shared_ptr<grpc::Channel>& channel, Callbacks callbacks)
        : reader_("bbm-sideways",
                  [stub = std::shared_ptr<beebium::SidewaysService::Stub>(
                       beebium::SidewaysService::NewStub(channel))](grpc::ClientContext& context) {
                      beebium::SubscribeEventsRequest request;
                      request.set_min_interval_ms(MIN_INTERVAL_MS);
                      request.set_monitor_header_changes(true);
                      return stub->SubscribeEvents(&context, request);
                  },
                  {[event = std::move(callbacks.event)](beebium::SidewaysEvent& message) {
                       if (event) {
                           event(message);
                       }
                   },
                   std::move(callbacks.finished)}) {}

    void start() { reader_.start(); }
    void stop() { reader_.stop(); }

private:
    StreamReader<beebium::SidewaysEvent> reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_SIDEWAYS_EVENT_STREAM_HPP
