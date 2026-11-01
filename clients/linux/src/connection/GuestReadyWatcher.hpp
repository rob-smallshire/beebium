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

#ifndef BEEBIUM_LINUX_CONNECTION_GUEST_READY_WATCHER_HPP
#define BEEBIUM_LINUX_CONNECTION_GUEST_READY_WATCHER_HPP

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace beebium::linux_client {

// Watches KeyboardService.WatchTypingStatus for the guest becoming ready:
// the first time, after a reset, that the MOS asks for typed input. That is
// when the MOS has finished re-initialising its lock latches, so it is when
// Caps Lock can be aligned without the MOS overwriting it (client
// specification 7.3: the MOS boot as a sync point, and every reset after).
// Exact at any emulation speed, where a wall-clock settle delay is not.
//
// became_ready runs on the watcher's thread, once per transition to ready,
// including a guest already ready when the watch begins.
class GuestReadyWatcher {
public:
    GuestReadyWatcher(std::shared_ptr<grpc::Channel> channel, std::function<void()> became_ready);
    ~GuestReadyWatcher();

    GuestReadyWatcher(const GuestReadyWatcher&) = delete;
    GuestReadyWatcher& operator=(const GuestReadyWatcher&) = delete;

    void start();
    void stop();

private:
    void run();

    std::shared_ptr<grpc::Channel> channel_;
    std::function<void()> became_ready_;
    grpc::ClientContext context_;
    std::atomic<bool> stop_requested_{false};
    std::thread reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_GUEST_READY_WATCHER_HPP
