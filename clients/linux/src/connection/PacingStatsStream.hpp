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

#ifndef BEEBIUM_LINUX_CONNECTION_PACING_STATS_STREAM_HPP
#define BEEBIUM_LINUX_CONNECTION_PACING_STATS_STREAM_HPP

#include "connection/StreamReader.hpp"

#include "system.grpc.pb.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace beebium::linux_client {

// SystemService.WatchPacingStats on a thread of its own: the configured,
// achieved and attainable speeds (client specification 5.6), at most every
// interval_ms. Callbacks run on the reader thread.
class PacingStatsStream {
public:
    struct Callbacks {
        std::function<void(const beebium::PacingStats&)> stats;
        std::function<void(const StreamEnd&)> finished;
    };

    PacingStatsStream(const std::shared_ptr<grpc::Channel>& channel, std::uint32_t interval_ms,
                      Callbacks callbacks)
        : reader_("bbm-pacing",
                  [stub = std::shared_ptr<beebium::SystemService::Stub>(
                       beebium::SystemService::NewStub(channel)),
                   interval_ms](grpc::ClientContext& context) {
                      beebium::WatchPacingStatsRequest request;
                      request.set_interval_ms(interval_ms);
                      return stub->WatchPacingStats(&context, request);
                  },
                  {[stats = std::move(callbacks.stats)](beebium::PacingStats& message) {
                       if (stats) {
                           stats(message);
                       }
                   },
                   std::move(callbacks.finished)}) {}

    void start() { reader_.start(); }
    void stop() { reader_.stop(); }

private:
    StreamReader<beebium::PacingStats> reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_PACING_STATS_STREAM_HPP
