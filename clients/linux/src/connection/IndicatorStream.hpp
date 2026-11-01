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

#ifndef BEEBIUM_LINUX_CONNECTION_INDICATOR_STREAM_HPP
#define BEEBIUM_LINUX_CONNECTION_INDICATOR_STREAM_HPP

#include "connection/StreamReader.hpp"

#include "indicator.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace beebium::linux_client {

// One of the machine's LEDs as the server registers it (client
// specification 10): never known to the client beforehand.
struct IndicatorInfo {
    std::string name;
    std::string label;
    std::string color;        // "#rrggbb", "rgb(...)", "hsl(...)" or "625nm"
    std::string shape;
    std::string related_key;  // a BBC key by catalogue name, or empty
};

// The indicators from ListIndicators, in the server's registration order.
// Metadata maps are read by iteration, never by lookup (CLAUDE.md, the
// protobuf Map hash bug).
std::vector<IndicatorInfo> indicator_infos(const beebium::ListIndicatorsResponse& response);

// IndicatorService on a thread of its own: ListIndicators once, then
// Subscribe at MIN_INTERVAL_MS, each update's brightness values (0 to 255)
// passed on. Callbacks run on the reader thread.
class IndicatorStream {
public:
    static constexpr std::uint32_t MIN_INTERVAL_MS = 50;
    using Values = std::vector<std::pair<std::string, std::uint32_t>>;

    struct Callbacks {
        std::function<void(const std::vector<IndicatorInfo>&)> listed;
        std::function<void(const Values&)> values;
        std::function<void(const StreamEnd&)> finished;
    };

    IndicatorStream(std::shared_ptr<grpc::Channel> channel, Callbacks callbacks);

    IndicatorStream(const IndicatorStream&) = delete;
    IndicatorStream& operator=(const IndicatorStream&) = delete;

    void start() { reader_.start(); }
    void stop() { reader_.stop(); }

private:
    std::unique_ptr<grpc::ClientReader<beebium::IndicatorUpdate>> open(grpc::ClientContext& context);

    std::shared_ptr<beebium::IndicatorService::Stub> stub_;
    Callbacks callbacks_;
    StreamReader<beebium::IndicatorUpdate> reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_INDICATOR_STREAM_HPP
