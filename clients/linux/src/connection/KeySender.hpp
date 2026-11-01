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

#ifndef BEEBIUM_LINUX_CONNECTION_KEY_SENDER_HPP
#define BEEBIUM_LINUX_CONNECTION_KEY_SENDER_HPP

#include "keyboard/HeldKeys.hpp"

#include "keyboard.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace beebium::linux_client {

// Sends key events to KeyboardService one at a time, in the order given, on
// a thread of its own: the GUI thread enqueues and never waits on the
// network, and the machine sees transitions in generation order
// (frontend-modifier-keys.md section 3; client specification 7.1, 7.2).
// Caps Lock syncs ride the same queue (7.3), so a tap never interleaves
// with a key the user is typing.
class KeySender {
public:
    explicit KeySender(std::shared_ptr<grpc::Channel> channel);
    ~KeySender();

    KeySender(const KeySender&) = delete;
    KeySender& operator=(const KeySender&) = delete;

    void send(const std::vector<WireEvent>& events);

    // Send what is queued, then stop. Each call is bounded by a deadline, so
    // a dead server cannot hold this up for long.
    void stop();

    std::uint64_t sent_count() const { return sent_.load(); }
    std::uint64_t failed_count() const { return failed_.load(); }
    // Caps Lock syncs that found the guest out of step and tapped it.
    std::uint64_t caps_lock_taps() const { return caps_lock_taps_.load(); }

private:
    void run();
    bool deliver(const WireEvent& event);
    bool sync_caps_lock(beebium::KeyboardService::Stub& stub, bool host_on);

    std::shared_ptr<grpc::Channel> channel_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<WireEvent> queue_;
    bool stopping_ = false;
    std::atomic<std::uint64_t> sent_{0};
    std::atomic<std::uint64_t> failed_{0};
    std::atomic<std::uint64_t> caps_lock_taps_{0};
    std::thread worker_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_KEY_SENDER_HPP
