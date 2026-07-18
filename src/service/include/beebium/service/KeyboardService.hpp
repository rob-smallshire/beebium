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
#include <functional>
#include <mutex>

namespace beebium {

class SystemViaPeripheral;

namespace service {

/// Callback interface for Break key operations (separate from keyboard matrix)
struct BreakCallbacks {
    std::function<void()> break_down;      // Called when Break is pressed (halt CPU)
    std::function<void()> break_up;        // Called when Break is released (soft reset)
    std::function<bool()> is_in_reset;     // Returns true if Break is currently held
};

/// gRPC service implementation for keyboard input
class KeyboardServiceImpl final : public KeyboardService::Service {
public:
    explicit KeyboardServiceImpl(SystemViaPeripheral& keyboard);
    KeyboardServiceImpl(SystemViaPeripheral& keyboard, BreakCallbacks break_callbacks);
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

    grpc::Status GetState(
        grpc::ServerContext* context,
        const GetStateRequest* request,
        KeyboardState* response) override;

    // Keyboard links (raw byte access)
    grpc::Status SetLinks(
        grpc::ServerContext* context,
        const SetLinksRequest* request,
        SetLinksResponse* response) override;

    grpc::Status GetLinks(
        grpc::ServerContext* context,
        const GetLinksRequest* request,
        LinksState* response) override;

    // Semantic accessors
    grpc::Status SetStartupScreenMode(
        grpc::ServerContext* context,
        const SetStartupScreenModeRequest* request,
        SetStartupScreenModeResponse* response) override;

    grpc::Status GetStartupScreenMode(
        grpc::ServerContext* context,
        const GetStartupScreenModeRequest* request,
        StartupScreenModeState* response) override;

    grpc::Status SetStartupAutoBoot(
        grpc::ServerContext* context,
        const SetStartupAutoBootRequest* request,
        SetStartupAutoBootResponse* response) override;

    grpc::Status GetStartupAutoBoot(
        grpc::ServerContext* context,
        const GetStartupAutoBootRequest* request,
        StartupAutoBootState* response) override;

    // Type-ahead (TypeQuickly)
    grpc::Status TypeQuickly(
        grpc::ServerContext* context,
        const TypeQuicklyRequest* request,
        TypeQuicklyResponse* response) override;

    grpc::Status GetTypingStatus(
        grpc::ServerContext* context,
        const GetTypingStatusRequest* request,
        TypingStatus* response) override;

    grpc::Status WatchTypingStatus(
        grpc::ServerContext* context,
        const WatchTypingStatusRequest* request,
        grpc::ServerWriter<TypingStatus>* writer) override;

    grpc::Status ClearTyping(
        grpc::ServerContext* context,
        const ClearTypingRequest* request,
        ClearTypingResponse* response) override;

    // Character-to-key mapping
    grpc::Status GetKeyMapping(
        grpc::ServerContext* context,
        const GetKeyMappingRequest* request,
        KeyMappingEntry* response) override;

    grpc::Status GetAllKeyMappings(
        grpc::ServerContext* context,
        const GetAllKeyMappingsRequest* request,
        AllKeyMappingsResponse* response) override;

    // Break key operations
    grpc::Status BreakDown(
        grpc::ServerContext* context,
        const BreakDownRequest* request,
        BreakDownResponse* response) override;

    grpc::Status BreakUp(
        grpc::ServerContext* context,
        const BreakUpRequest* request,
        BreakUpResponse* response) override;

    grpc::Status GetBreakState(
        grpc::ServerContext* context,
        const GetBreakStateRequest* request,
        BreakKeyState* response) override;

    grpc::Status GetLockState(
        grpc::ServerContext* context,
        const GetLockStateRequest* request,
        LockKeyState* response) override;

private:
    SystemViaPeripheral& keyboard_;
    BreakCallbacks break_callbacks_;
    std::mutex mutex_;
};

} // namespace service
} // namespace beebium

#endif // BEEBIUM_SERVICE_KEYBOARD_SERVICE_HPP
