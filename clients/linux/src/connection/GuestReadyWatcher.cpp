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

#include "connection/GuestReadyWatcher.hpp"

#include "keyboard.grpc.pb.h"

#include <cstdint>

#if defined(__linux__)
#include <pthread.h>
#endif

namespace beebium::linux_client {

namespace {

constexpr std::uint32_t WATCH_INTERVAL_MS = 250;

}  // namespace

GuestReadyWatcher::GuestReadyWatcher(std::shared_ptr<grpc::Channel> channel,
                                     std::function<void()> became_ready)
    : channel_(std::move(channel)), became_ready_(std::move(became_ready)) {}

GuestReadyWatcher::~GuestReadyWatcher() {
    stop();
}

void GuestReadyWatcher::start() {
    if (reader_.joinable() || stop_requested_.load()) {
        return;
    }
    reader_ = std::thread([this] { run(); });
}

void GuestReadyWatcher::stop() {
    stop_requested_.store(true);
    context_.TryCancel();
    if (reader_.joinable()) {
        reader_.join();
    }
}

void GuestReadyWatcher::run() {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), "bbm-typing");
#endif
    auto stub = beebium::KeyboardService::NewStub(channel_);
    beebium::WatchTypingStatusRequest request;
    // Until the guest is ready the server pushes as emulated time passes, at
    // the stream's default of every 20 ms; a Caps Lock sync a quarter of a
    // second after the guest is ready is soon enough.
    request.set_min_interval_ms(WATCH_INTERVAL_MS);
    auto reader = stub->WatchTypingStatus(&context_, request);
    bool ready = false;
    beebium::TypingStatus status;
    while (reader->Read(&status)) {
        if (status.guest_ready() && !ready && became_ready_) {
            became_ready_();
        }
        ready = status.guest_ready();
    }
    // A lost stream only ends the sync points it provided; the connection's
    // liveness is not this stream's to judge.
    reader->Finish();
}

}  // namespace beebium::linux_client
