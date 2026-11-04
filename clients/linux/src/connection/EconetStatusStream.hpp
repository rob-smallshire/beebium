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

#ifndef BEEBIUM_LINUX_CONNECTION_ECONET_STATUS_STREAM_HPP
#define BEEBIUM_LINUX_CONNECTION_ECONET_STATUS_STREAM_HPP

#include "connection/StreamReader.hpp"

#include "econet.grpc.pb.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace beebium::linux_client {

// EconetService.WatchEconetStatus on a thread of its own: a snapshot at
// once, then one whenever the Econet status changes (client specification
// 12.4). Callbacks run on the reader thread.
class EconetStatusStream {
public:
    static constexpr std::uint32_t MIN_INTERVAL_MS = 100;

    struct Callbacks {
        std::function<void(const beebium::GetEconetStatusResponse&)> status;
        std::function<void(const StreamEnd&)> finished;
    };

    EconetStatusStream(const std::shared_ptr<grpc::Channel>& channel, Callbacks callbacks)
        : reader_("bbm-econet",
                  [stub = std::shared_ptr<beebium::EconetService::Stub>(
                       beebium::EconetService::NewStub(channel))](grpc::ClientContext& context) {
                      beebium::WatchEconetStatusRequest request;
                      request.set_min_interval_ms(MIN_INTERVAL_MS);
                      return stub->WatchEconetStatus(&context, request);
                  },
                  {[status = std::move(callbacks.status)](
                       beebium::GetEconetStatusResponse& message) {
                       if (status) {
                           status(message);
                       }
                   },
                   std::move(callbacks.finished)}) {}

    void start() { reader_.start(); }
    void stop() { reader_.stop(); }

private:
    StreamReader<beebium::GetEconetStatusResponse> reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_ECONET_STATUS_STREAM_HPP
