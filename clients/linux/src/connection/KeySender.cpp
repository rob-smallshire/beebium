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

#include <chrono>
#include <cstdio>

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
    }
    if (!status.ok()) {
        // Logged rather than retried: a key event late is worse than lost.
        std::fprintf(stderr, "beebium: key event not delivered: %s\n",
                     status.error_message().c_str());
    }
    return status.ok();
}

}  // namespace beebium::linux_client
