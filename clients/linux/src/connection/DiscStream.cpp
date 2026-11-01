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

#include "connection/DiscStream.hpp"

#include <chrono>

namespace beebium::linux_client {

DiscStream::DiscStream(std::shared_ptr<grpc::Channel> channel, Callbacks callbacks)
    : stub_(beebium::DiscService::NewStub(channel)),
      callbacks_(std::move(callbacks)),
      reader_("bbm-discs",
              [this](grpc::ClientContext& context) { return open(context); },
              {[this](beebium::DiscEvent& event) {
                   if (callbacks_.event) {
                       callbacks_.event(event);
                   }
               },
               [this](const StreamEnd& end) {
                   if (callbacks_.finished) {
                       callbacks_.finished(end);
                   }
               }}) {}

std::unique_ptr<grpc::ClientReader<beebium::DiscEvent>> DiscStream::open(
    grpc::ClientContext& context) {
    // The events first, so nothing between the snapshot and them is lost:
    // the subscription is opened, then the snapshot taken.
    auto events = stub_->SubscribeDiscEvents(&context, {});
    grpc::ClientContext status_context;
    status_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    beebium::GetDriveStatusResponse status;
    if (stub_->GetDriveStatus(&status_context, {}, &status).ok() && callbacks_.status) {
        callbacks_.status(status);
    }
    return events;
}

}  // namespace beebium::linux_client
