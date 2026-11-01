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

// Caps Lock synchronisation, client specification 7.3: the decision, the
// host state readers, and the guest-ready sync point.

#include "app/HostLockState.hpp"
#include "connection/GuestReadyWatcher.hpp"
#include "keyboard/CapsLockSync.hpp"

#include "keyboard.grpc.pb.h"

#include <catch2/catch_test_macros.hpp>

#include <xkbcommon/xkbcommon.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace beebium::linux_client;
using namespace std::chrono_literals;

TEST_CASE("The guest is tapped only when sync is on and the states differ",
          "[caps-lock]") {
    CHECK(should_tap_caps_lock(true, true, false));
    CHECK(should_tap_caps_lock(true, false, true));
    CHECK_FALSE(should_tap_caps_lock(true, true, true));
    CHECK_FALSE(should_tap_caps_lock(true, false, false));
    CHECK_FALSE(should_tap_caps_lock(false, true, false));
}

TEST_CASE("An unknown state on either side taps nothing", "[caps-lock]") {
    CHECK_FALSE(should_tap_caps_lock(true, std::nullopt, false));
    CHECK_FALSE(should_tap_caps_lock(true, true, std::nullopt));
}

TEST_CASE("Caps Lock is read from the locked modifiers by the keymap's index",
          "[caps-lock][xkb]") {
    // A real keymap, as a compositor would send it.
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    REQUIRE(context);
    const xkb_rule_names names{"evdev", "pc105", "gb", "", ""};
    xkb_keymap* keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    REQUIRE(keymap);
    const xkb_mod_index_t caps = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
    REQUIRE(caps != XKB_MOD_INVALID);
    const xkb_mod_index_t num = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_NUM);

    CHECK(caps_locked_in_mask(1u << caps, caps) == true);
    CHECK(caps_locked_in_mask(0, caps) == false);
    // Num Lock alone is not Caps Lock.
    CHECK(caps_locked_in_mask(1u << num, caps) == false);
    CHECK(caps_locked_in_mask((1u << num) | (1u << caps), caps) == true);

    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
}

TEST_CASE("A keymap without a Caps modifier gives no answer", "[caps-lock]") {
    CHECK_FALSE(caps_locked_in_mask(0xffffffffu, std::nullopt).has_value());
    CHECK_FALSE(caps_locked_in_mask(1, 40).has_value());
}

TEST_CASE("With no windowing system the host state is unknown", "[caps-lock]") {
    // The tests run under the offscreen platform.
    const auto locks = HostLockState::create();
    REQUIRE(locks);
    CHECK_FALSE(locks->caps_lock().has_value());
}

namespace {

class TypingStatusService final : public beebium::KeyboardService::Service {
public:
    std::vector<bool> guest_ready_sequence;

    grpc::Status WatchTypingStatus(grpc::ServerContext* context,
                                   const beebium::WatchTypingStatusRequest*,
                                   grpc::ServerWriter<beebium::TypingStatus>* writer) override {
        for (bool ready : guest_ready_sequence) {
            beebium::TypingStatus status;
            status.set_guest_ready(ready);
            writer->Write(status);
        }
        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(5ms);
        }
        return grpc::Status::OK;
    }
};

}  // namespace

TEST_CASE("Each time the guest becomes ready is one sync point", "[caps-lock][guest-ready]") {
    TypingStatusService service;
    // Ready at once, a reset, ready again; repeats of a state are not edges.
    service.guest_ready_sequence = {true, true, false, false, true, true};
    int port = 0;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    REQUIRE(server);

    std::atomic<int> ready_count{0};
    GuestReadyWatcher watcher(grpc::CreateChannel("127.0.0.1:" + std::to_string(port),
                                                  grpc::InsecureChannelCredentials()),
                              [&] { ++ready_count; });
    watcher.start();
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (ready_count < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    std::this_thread::sleep_for(100ms);  // no third call follows
    watcher.stop();
    server->Shutdown(std::chrono::system_clock::now() + 1s);
    CHECK(ready_count == 2);
}
