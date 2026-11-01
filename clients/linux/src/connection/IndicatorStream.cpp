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

#include "connection/IndicatorStream.hpp"

#include <chrono>

namespace beebium::linux_client {

std::vector<IndicatorInfo> indicator_infos(const beebium::ListIndicatorsResponse& response) {
    std::vector<IndicatorInfo> infos;
    for (const auto& indicator : response.indicators()) {
        IndicatorInfo info;
        info.name = indicator.name();
        for (const auto& [key, value] : indicator.metadata()) {
            if (key == "label") {
                info.label = value;
            } else if (key == "color") {
                info.color = value;
            } else if (key == "shape") {
                info.shape = value;
            } else if (key == "related_key") {
                info.related_key = value;
            }
        }
        if (info.label.empty()) {
            info.label = info.name;
        }
        infos.push_back(std::move(info));
    }
    return infos;
}

IndicatorStream::IndicatorStream(std::shared_ptr<grpc::Channel> channel, Callbacks callbacks)
    : stub_(beebium::IndicatorService::NewStub(channel)),
      callbacks_(std::move(callbacks)),
      reader_("bbm-leds",
              [this](grpc::ClientContext& context) { return open(context); },
              {[this](beebium::IndicatorUpdate& update) {
                   if (!callbacks_.values) {
                       return;
                   }
                   Values values;
                   for (const auto& [name, value] : update.values()) {
                       values.emplace_back(name, value);
                   }
                   callbacks_.values(values);
               },
               [this](const StreamEnd& end) {
                   if (callbacks_.finished) {
                       callbacks_.finished(end);
                   }
               }}) {}

std::unique_ptr<grpc::ClientReader<beebium::IndicatorUpdate>> IndicatorStream::open(
    grpc::ClientContext& context) {
    grpc::ClientContext list_context;
    list_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    beebium::ListIndicatorsResponse listed;
    if (stub_->ListIndicators(&list_context, {}, &listed).ok() && callbacks_.listed) {
        callbacks_.listed(indicator_infos(listed));
    }
    beebium::SubscribeIndicatorsRequest request;
    request.set_min_interval_ms(MIN_INTERVAL_MS);
    return stub_->Subscribe(&context, request);
}

}  // namespace beebium::linux_client
