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

// Typing (client specification 7.3, 7.5): the guest_ready edge that syncs
// the lock latches, and pasting as server-queued typing.

#include "app/ConnectionRecovery.hpp"
#include "app/DisplayWidget.hpp"
#include "app/KeyboardInput.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"
#include "app/SessionTyping.hpp"

#include "FakeBeebiumServer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

std::shared_ptr<grpc::Channel> channel_to(const FakeServer& server) {
    return grpc::CreateChannel(server.endpoint().toStdString(), grpc::InsecureChannelCredentials());
}

struct Typing {
    FakeServer server;
    SessionTyping typing;
    std::optional<PasteOutcome> outcome;
    int ready_edges = 0;
    Typing() {
        QObject::connect(&typing, &SessionTyping::paste_finished,
                         [this](const PasteOutcome& o) { outcome = o; });
        QObject::connect(&typing, &SessionTyping::guest_ready_rose, [this] { ++ready_edges; });
        typing.start(channel_to(server));
        REQUIRE(wait_until([&] { return server.keyboard.typing_watchers == 1; }));
    }
    ~Typing() { typing.stop(); }
};

}  // namespace

TEST_CASE("The guest's readiness is an edge, told once per boot or reset", "[typing][locks]") {
    Typing t;
    t.server.keyboard.push_typing(typing_status(false, 0));
    t.server.keyboard.push_typing(typing_status(true, 0));
    t.server.keyboard.push_typing(typing_status(true, 0));
    REQUIRE(wait_until([&] { return t.typing.guest_ready(); }));
    CHECK(t.ready_edges == 1);
    // A reset clears it; the next readiness is the next edge.
    t.server.keyboard.push_typing(typing_status(false, 0));
    t.server.keyboard.push_typing(typing_status(true, 0));
    REQUIRE(wait_until([&] { return t.ready_edges == 2; }));
}

TEST_CASE("A paste is typed by the server and done when it says the queue drained",
          "[typing][paste]") {
    Typing t;
    t.typing.paste(QStringLiteral("10 PRINT \"HELLO\"\n20 GOTO 10\n"));
    CHECK(t.typing.pasting());
    REQUIRE(wait_until([&] {
        std::lock_guard lock(t.server.keyboard.typing_mutex);
        return !t.server.keyboard.typed.empty();
    }));
    {
        std::lock_guard lock(t.server.keyboard.typing_mutex);
        CHECK(t.server.keyboard.typed.front() == "10 PRINT \"HELLO\"\n20 GOTO 10\n");
    }
    t.server.keyboard.push_typing(typing_status(true, 20));
    t.server.keyboard.push_typing(typing_status(true, 3));
    // No timeout of the client's own: still pasting until the server says so.
    CHECK_FALSE(wait_until([&] { return t.outcome.has_value(); }, std::chrono::milliseconds(300)));
    t.server.keyboard.push_typing(typing_status(true, 0));
    REQUIRE(wait_until([&] { return t.outcome.has_value(); }));
    CHECK(t.outcome->result == PasteOutcome::Result::typed);
    CHECK_FALSE(t.typing.pasting());
}

TEST_CASE("A paste the server refuses says why, in its words", "[typing][paste]") {
    Typing t;
    {
        std::lock_guard lock(t.server.keyboard.typing_mutex);
        t.server.keyboard.refuse = "\xE2\x82\xAC";  // a euro sign
    }
    t.typing.paste(QStringLiteral("price: €5"));
    REQUIRE(wait_until([&] { return t.outcome.has_value(); }));
    CHECK(t.outcome->result == PasteOutcome::Result::refused);
    CHECK(t.outcome->message.contains(QStringLiteral("cannot type")));
}

TEST_CASE("Cancel drops what is still to type", "[typing][paste]") {
    Typing t;
    t.typing.paste(QStringLiteral("a long program"));
    t.server.keyboard.push_typing(typing_status(true, 9));
    REQUIRE(wait_until([&] { return t.typing.pending_characters() == 9; }));
    t.typing.cancel_paste();
    REQUIRE(wait_until([&] { return t.outcome.has_value(); }));
    CHECK(t.outcome->result == PasteOutcome::Result::cancelled);
    CHECK(t.outcome->count == 7);
    CHECK(t.server.keyboard.clears == 1);
}

TEST_CASE("A reset during a paste is told, with what it discarded", "[typing][paste]") {
    Typing t;
    t.typing.paste(QStringLiteral("typed into a reset"));
    t.server.keyboard.push_typing(typing_status(true, 10));
    t.server.keyboard.push_typing(typing_status(false, 0, 10));
    REQUIRE(wait_until([&] { return t.outcome.has_value(); }));
    CHECK(t.outcome->result == PasteOutcome::Result::cut_short_by_reset);
    CHECK(t.outcome->count == 10);
}

// --- the lock latches in a machine window -------------------------------------------

TEST_CASE("A machine window reads the latches on a lock LED change and on guest readiness, "
          "tapping only on disagreement",
          "[typing][locks][window]") {
    FakeServer server;
    MainWindow window(QRhiWidget::Api::OpenGL);
    // The host's Caps Lock, as the windowing system would report it.
    bool host_caps = true;
    window.display().keyboard().set_host_caps_lock([&] { return std::optional<bool>(host_caps); });
    window.show();
    window.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return window.session().connected(); }));
    REQUIRE(wait_until([&] { return window.display().keyboard().attached(); }));
    REQUIRE(wait_until([&] { return server.indicators.subscriptions == 1; }));
    REQUIRE(wait_until([&] { return server.keyboard.typing_watchers == 1; }));
    // Attaching synced already: host on, machine off, one tap.
    REQUIRE(wait_until([&] { return server.keyboard.caps_taps == 1; }));
    const int reads = server.keyboard.lock_reads;

    // A reset re-inits the latches: the lock LED changes, and later the guest
    // is ready again. Each is a read; neither taps, since they now agree.
    server.indicators.push(std::vector<std::pair<std::string, std::uint32_t>>{{"caps-lock-led", 255}});
    REQUIRE(wait_until([&] { return server.keyboard.lock_reads == reads + 1; }));
    server.keyboard.push_typing(typing_status(true, 0));
    REQUIRE(wait_until([&] { return server.keyboard.lock_reads == reads + 2; }));
    CHECK(server.keyboard.caps_taps == 1);

    // The machine's own latch changed under it (a reset turned Caps Lock
    // off): the next sync point brings it back to the host's.
    server.keyboard.caps_lock_on = false;
    server.indicators.push(std::vector<std::pair<std::string, std::uint32_t>>{{"caps-lock-led", 0}});
    REQUIRE(wait_until([&] { return server.keyboard.caps_taps == 2; }));
    CHECK(server.keyboard.caps_lock_on);
    window.set_quitting(true);
    window.close();
}
