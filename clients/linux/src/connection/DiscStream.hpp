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

#ifndef BEEBIUM_LINUX_CONNECTION_DISC_STREAM_HPP
#define BEEBIUM_LINUX_CONNECTION_DISC_STREAM_HPP

#include "connection/StreamReader.hpp"

#include "disc.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <functional>
#include <memory>

namespace beebium::linux_client {

// DiscService's state on a thread of its own (client specification 9.7):
// GetDriveStatus once, then SubscribeDiscEvents, each event passed on.
// Event-driven: never a poll. Callbacks run on the reader thread.
class DiscStream {
public:
    struct Callbacks {
        std::function<void(const beebium::GetDriveStatusResponse&)> status;
        std::function<void(const beebium::DiscEvent&)> event;
        std::function<void(const StreamEnd&)> finished;
    };

    DiscStream(std::shared_ptr<grpc::Channel> channel, Callbacks callbacks);

    DiscStream(const DiscStream&) = delete;
    DiscStream& operator=(const DiscStream&) = delete;

    void start() { reader_.start(); }
    void stop() { reader_.stop(); }

private:
    std::unique_ptr<grpc::ClientReader<beebium::DiscEvent>> open(grpc::ClientContext& context);

    std::shared_ptr<beebium::DiscService::Stub> stub_;
    Callbacks callbacks_;
    StreamReader<beebium::DiscEvent> reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_DISC_STREAM_HPP
