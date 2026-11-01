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

#include "connection/KeySender.hpp"

#include "keyboard.grpc.pb.h"

#include "keyboard/CapsLockSync.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#if defined(__linux__)
#include <pthread.h>
#endif

namespace beebium::linux_client {

namespace {

constexpr std::chrono::milliseconds SEND_DEADLINE{2000};

}  // namespace

KeySender::KeySender(std::shared_ptr<grpc::Channel> channel) : channel_(std::move(channel)) {
    worker_ = std::thread([this] { run(); });
}

KeySender::~KeySender() {
    stop();
}

void KeySender::send(const std::vector<WireEvent>& events) {
    if (events.empty()) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        queue_.insert(queue_.end(), events.begin(), events.end());
    }
    wake_.notify_one();
}

void KeySender::stop() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void KeySender::run() {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), "bbm-keys");
#endif
    for (;;) {
        WireEvent event;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                return;  // stopping, and everything queued has gone
            }
            event = queue_.front();
            queue_.pop_front();
        }
        if (deliver(event)) {
            sent_.fetch_add(1);
        } else {
            failed_.fetch_add(1);
        }
    }
}

bool KeySender::sync_caps_lock(beebium::KeyboardService::Stub& stub, bool host_on) {
    beebium::LockKeyState guest;
    {
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + SEND_DEADLINE);
        const grpc::Status status =
            stub.GetLockState(&context, beebium::GetLockStateRequest{}, &guest);
        if (!status.ok()) {
            std::fprintf(stderr, "beebium: cannot read the machine's Caps Lock: %s\n",
                         status.error_message().c_str());
            return false;
        }
    }
    const bool tap = should_tap_caps_lock(true, host_on, guest.caps_lock_on());
    std::fprintf(stderr, "beebium: Caps Lock read: machine %s, host %s%s\n",
                 guest.caps_lock_on() ? "on" : "off", host_on ? "on" : "off",
                 tap ? ", tapping" : "");
    if (!tap) {
        return true;
    }
    beebium::KeyRequest request;
    request.set_ik_number(BBC_CAPS_LOCK_IK);
    beebium::KeyResponse response;
    grpc::ClientContext down_context;
    down_context.set_deadline(std::chrono::system_clock::now() + SEND_DEADLINE);
    if (!stub.KeyDown(&down_context, request, &response).ok()) {
        return false;
    }
    // Long enough for the MOS to scan the key down; it toggles on key-down.
    std::this_thread::sleep_for(CAPS_LOCK_TAP_HOLD);
    grpc::ClientContext up_context;
    up_context.set_deadline(std::chrono::system_clock::now() + SEND_DEADLINE);
    const bool released = stub.KeyUp(&up_context, request, &response).ok();
    caps_lock_taps_.fetch_add(1);
    return released;
}

bool KeySender::deliver(const WireEvent& event) {
    auto stub = beebium::KeyboardService::NewStub(channel_);
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + SEND_DEADLINE);
    grpc::Status status;
    switch (event.action) {
    case WireAction::key_down:
    case WireAction::key_up: {
        beebium::KeyRequest request;
        request.set_ik_number(event.ik_number);
        beebium::KeyResponse response;
        status = event.action == WireAction::key_down
                     ? stub->KeyDown(&context, request, &response)
                     : stub->KeyUp(&context, request, &response);
        break;
    }
    case WireAction::break_down: {
        beebium::BreakDownResponse response;
        status = stub->BreakDown(&context, beebium::BreakDownRequest{}, &response);
        break;
    }
    case WireAction::break_up: {
        beebium::BreakUpResponse response;
        status = stub->BreakUp(&context, beebium::BreakUpRequest{}, &response);
        break;
    }
    case WireAction::caps_lock_sync:
        return sync_caps_lock(*stub, event.caps_lock_on);
    }
    if (!status.ok()) {
        // Logged rather than retried: a key event late is worse than lost.
        std::fprintf(stderr, "beebium: key event not delivered: %s\n",
                     status.error_message().c_str());
    }
    return status.ok();
}

}  // namespace beebium::linux_client
