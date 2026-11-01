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

// KeySender against an in-process KeyboardService: every event arrives, in
// the order sent, and stopping drains the queue first.

#include "connection/KeySender.hpp"

#include "keyboard.grpc.pb.h"

#include <catch2/catch_test_macros.hpp>

#include <mutex>

using namespace beebium::linux_client;
using namespace std::chrono_literals;

namespace {

class RecordingKeyboardService final : public beebium::KeyboardService::Service {
public:
    std::mutex mutex;
    std::vector<WireEvent> received;
    std::vector<std::chrono::steady_clock::time_point> received_at;
    bool guest_caps_lock_on = true;

    grpc::Status GetLockState(grpc::ServerContext*, const beebium::GetLockStateRequest*,
                              beebium::LockKeyState* state) override {
        std::lock_guard lock(mutex);
        state->set_caps_lock_on(guest_caps_lock_on);
        return grpc::Status::OK;
    }

    grpc::Status KeyDown(grpc::ServerContext*, const beebium::KeyRequest* request,
                         beebium::KeyResponse*) override {
        record({WireAction::key_down, static_cast<std::uint8_t>(request->ik_number())});
        return grpc::Status::OK;
    }
    grpc::Status KeyUp(grpc::ServerContext*, const beebium::KeyRequest* request,
                       beebium::KeyResponse*) override {
        record({WireAction::key_up, static_cast<std::uint8_t>(request->ik_number())});
        return grpc::Status::OK;
    }
    grpc::Status BreakDown(grpc::ServerContext*, const beebium::BreakDownRequest*,
                           beebium::BreakDownResponse*) override {
        record({WireAction::break_down, 0});
        return grpc::Status::OK;
    }
    grpc::Status BreakUp(grpc::ServerContext*, const beebium::BreakUpRequest*,
                         beebium::BreakUpResponse*) override {
        record({WireAction::break_up, 0});
        return grpc::Status::OK;
    }

private:
    void record(WireEvent event) {
        std::lock_guard lock(mutex);
        received.push_back(event);
        received_at.push_back(std::chrono::steady_clock::now());
        // The MOS toggles its latch on a Caps Lock key-down.
        if (event.action == WireAction::key_down && event.ik_number == 0x40) {
            guest_caps_lock_on = !guest_caps_lock_on;
        }
    }
};

}  // namespace

TEST_CASE("Key events reach the server in the order sent", "[connection][keys]") {
    RecordingKeyboardService service;
    int port = 0;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    REQUIRE(server);

    std::vector<WireEvent> sent;
    for (std::uint8_t ik = 0x10; ik < 0x40; ++ik) {
        sent.push_back({WireAction::key_down, ik});
        sent.push_back({WireAction::key_up, ik});
    }
    sent.push_back({WireAction::break_down, 0});
    sent.push_back({WireAction::break_up, 0});

    {
        KeySender sender(grpc::CreateChannel("127.0.0.1:" + std::to_string(port),
                                             grpc::InsecureChannelCredentials()));
        // In several batches, as the GUI thread would.
        for (std::size_t i = 0; i < sent.size(); i += 7) {
            sender.send(std::vector<WireEvent>(
                sent.begin() + static_cast<std::ptrdiff_t>(i),
                sent.begin() + static_cast<std::ptrdiff_t>(std::min(i + 7, sent.size()))));
        }
        sender.stop();
        CHECK(sender.sent_count() == sent.size());
        CHECK(sender.failed_count() == 0);
    }
    server->Shutdown(std::chrono::system_clock::now() + 1s);

    std::lock_guard lock(service.mutex);
    CHECK(service.received == sent);
}

TEST_CASE("Events sent after stopping are dropped", "[connection][keys]") {
    KeySender sender(
        grpc::CreateChannel("127.0.0.1:1", grpc::InsecureChannelCredentials()));
    sender.stop();
    sender.send({{WireAction::key_down, 0x41}});
    CHECK(sender.sent_count() == 0);
    CHECK(sender.failed_count() == 0);
}

namespace {

struct RecordingServer {
    RecordingKeyboardService service;
    int port = 0;
    std::unique_ptr<grpc::Server> server;

    RecordingServer() {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
        builder.RegisterService(&service);
        server = builder.BuildAndStart();
    }
    ~RecordingServer() { server->Shutdown(std::chrono::system_clock::now() + 1s); }
    std::shared_ptr<grpc::Channel> channel() const {
        return grpc::CreateChannel("127.0.0.1:" + std::to_string(port),
                                   grpc::InsecureChannelCredentials());
    }
};

WireEvent caps_sync(bool on) {
    WireEvent event;
    event.action = WireAction::caps_lock_sync;
    event.caps_lock_on = on;
    return event;
}

}  // namespace

TEST_CASE("A Caps Lock sync taps a guest that differs, holding it for a scan",
          "[connection][keys][caps-lock]") {
    RecordingServer server;
    server.service.guest_caps_lock_on = true;  // the MOS's power-on state
    {
        KeySender sender(server.channel());
        sender.send({caps_sync(false)});
        sender.stop();
        CHECK(sender.caps_lock_taps() == 1);
        CHECK(sender.failed_count() == 0);
    }
    std::lock_guard lock(server.service.mutex);
    REQUIRE(server.service.received ==
            std::vector<WireEvent>{{WireAction::key_down, 0x40}, {WireAction::key_up, 0x40}});
    CHECK(server.service.received_at[1] - server.service.received_at[0] >= 50ms);
    CHECK_FALSE(server.service.guest_caps_lock_on);
}

TEST_CASE("A Caps Lock sync leaves an agreeing guest alone", "[connection][keys][caps-lock]") {
    RecordingServer server;
    server.service.guest_caps_lock_on = true;
    {
        KeySender sender(server.channel());
        sender.send({caps_sync(true)});
        sender.stop();
        CHECK(sender.caps_lock_taps() == 0);
    }
    std::lock_guard lock(server.service.mutex);
    CHECK(server.service.received.empty());
}

TEST_CASE("Repeated syncs never fight: one tap, then agreement", "[connection][keys][caps-lock]") {
    RecordingServer server;
    server.service.guest_caps_lock_on = true;
    KeySender sender(server.channel());
    sender.send({caps_sync(false), caps_sync(false), caps_sync(false)});
    sender.stop();
    CHECK(sender.caps_lock_taps() == 1);
}

TEST_CASE("A Caps Lock sync keeps its place among key events", "[connection][keys][caps-lock]") {
    RecordingServer server;
    server.service.guest_caps_lock_on = true;
    {
        KeySender sender(server.channel());
        sender.send({{WireAction::key_down, 0x41}, {WireAction::key_up, 0x41}, caps_sync(false),
                     {WireAction::key_down, 0x42}, {WireAction::key_up, 0x42}});
        sender.stop();
    }
    std::lock_guard lock(server.service.mutex);
    CHECK(server.service.received ==
          std::vector<WireEvent>{{WireAction::key_down, 0x41},
                                 {WireAction::key_up, 0x41},
                                 {WireAction::key_down, 0x40},
                                 {WireAction::key_up, 0x40},
                                 {WireAction::key_down, 0x42},
                                 {WireAction::key_up, 0x42}});
}
