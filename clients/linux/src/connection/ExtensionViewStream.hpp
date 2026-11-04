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

#ifndef BEEBIUM_LINUX_CONNECTION_EXTENSION_VIEW_STREAM_HPP
#define BEEBIUM_LINUX_CONNECTION_EXTENSION_VIEW_STREAM_HPP

#include "connection/StreamReader.hpp"

#include "extension_ui.grpc.pb.h"

#include <functional>
#include <memory>
#include <string>

namespace beebium::linux_client {

// ExtensionUiService.SubscribeView for one extension, on a thread of its
// own: its view at once, then a fresh one on every change (client
// specification 12.1). Callbacks run on the reader thread.
class ExtensionViewStream {
public:
    struct Callbacks {
        std::function<void(const beebium::View&)> view;
        std::function<void(const StreamEnd&)> finished;
    };

    ExtensionViewStream(const std::shared_ptr<grpc::Channel>& channel,
                        const std::string& extension_id, Callbacks callbacks)
        : reader_("bbm-ext-view",
                  [stub = std::shared_ptr<beebium::ExtensionUiService::Stub>(
                       beebium::ExtensionUiService::NewStub(channel)),
                   extension_id](grpc::ClientContext& context) {
                      beebium::SubscribeViewRequest request;
                      request.set_extension_id(extension_id);
                      return stub->SubscribeView(&context, request);
                  },
                  {[view = std::move(callbacks.view)](beebium::View& message) {
                       if (view) {
                           view(message);
                       }
                   },
                   std::move(callbacks.finished)}) {}

    void start() { reader_.start(); }
    void stop() { reader_.stop(); }

private:
    StreamReader<beebium::View> reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_EXTENSION_VIEW_STREAM_HPP
