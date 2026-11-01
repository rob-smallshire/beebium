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

#ifndef BEEBIUM_LINUX_CONNECTION_SERVER_STATUS_STREAM_HPP
#define BEEBIUM_LINUX_CONNECTION_SERVER_STATUS_STREAM_HPP

#include "connection/StreamReader.hpp"

#include "system.grpc.pb.h"

#include <functional>
#include <memory>

namespace beebium::linux_client {

// SystemService.WatchServerStatus on a thread of its own: the connection's
// liveness, identity, reset and guest-ready events (client specification 4.3,
// 5.7, 7.3). Callbacks run on the reader thread.
class ServerStatusStream {
public:
    struct Callbacks {
        std::function<void(const beebium::ServerStatusEvent&)> event;
        std::function<void(const StreamEnd&)> finished;
    };

    ServerStatusStream(const std::shared_ptr<grpc::Channel>& channel, Callbacks callbacks)
        : reader_("bbm-status",
                  [stub = std::shared_ptr<beebium::SystemService::Stub>(
                       beebium::SystemService::NewStub(channel))](grpc::ClientContext& context) {
                      return stub->WatchServerStatus(&context,
                                                     beebium::WatchServerStatusRequest{});
                  },
                  {[event = std::move(callbacks.event)](beebium::ServerStatusEvent& message) {
                       if (event) {
                           event(message);
                       }
                   },
                   std::move(callbacks.finished)}) {}

    void start() { reader_.start(); }
    void stop() { reader_.stop(); }

private:
    StreamReader<beebium::ServerStatusEvent> reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_SERVER_STATUS_STREAM_HPP
