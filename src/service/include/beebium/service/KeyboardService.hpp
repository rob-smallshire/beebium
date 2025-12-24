// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SERVICE_KEYBOARD_SERVICE_HPP
#define BEEBIUM_SERVICE_KEYBOARD_SERVICE_HPP

#include "keyboard.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <mutex>

namespace beebium {

class SystemViaPeripheral;

namespace service {

/// gRPC service implementation for keyboard input
class KeyboardServiceImpl final : public KeyboardService::Service {
public:
    explicit KeyboardServiceImpl(SystemViaPeripheral& keyboard);
    ~KeyboardServiceImpl() override;

    // Non-copyable
    KeyboardServiceImpl(const KeyboardServiceImpl&) = delete;
    KeyboardServiceImpl& operator=(const KeyboardServiceImpl&) = delete;

    grpc::Status KeyDown(
        grpc::ServerContext* context,
        const KeyRequest* request,
        KeyResponse* response) override;

    grpc::Status KeyUp(
        grpc::ServerContext* context,
        const KeyRequest* request,
        KeyResponse* response) override;

    grpc::Status TypeText(
        grpc::ServerContext* context,
        const TypeTextRequest* request,
        TypeTextResponse* response) override;

    grpc::Status GetState(
        grpc::ServerContext* context,
        const GetStateRequest* request,
        KeyboardState* response) override;

private:
    SystemViaPeripheral& keyboard_;
    std::mutex mutex_;
};

} // namespace service
} // namespace beebium

#endif // BEEBIUM_SERVICE_KEYBOARD_SERVICE_HPP
