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

// The screen text calls through the session (client specification 6.5):
// a hold brings its grid and still, reads name it, a gone hold is told
// apart from other failures, and a release reaches the server.

#include "app/MachineSession.hpp"
#include "app/SessionScreenText.hpp"

#include "FakeBeebiumServer.hpp"

#include "video.pb.h"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

void connect_session(MachineSession& session, FakeServer& server) {
    bool connected = false;
    auto connection = QObject::connect(&session, &MachineSession::connected_to_server,
                                       [&] { connected = true; });
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return connected; }));
    QObject::disconnect(connection);
}

void script_screen(FakeServer& server) {
    std::lock_guard lock(server.video.screen_mutex);
    auto* band = server.video.hold_geometry.add_bands();
    band->set_bottom(256);
    band->set_cell_width(8);
    band->set_cell_height(8);
    band->set_column_pitch(8);
    band->set_row_pitch(8);
    server.video.reading.set_supported(true);
    server.video.reading.set_text("HELLO");
    auto* run = server.video.reading.add_runs();
    run->set_text("HELLO");
}

std::optional<HeldScreen> hold(SessionScreenText& screen_text) {
    std::optional<std::optional<HeldScreen>> answer;
    screen_text.hold([&](std::optional<HeldScreen> held) { answer = std::move(held); });
    REQUIRE(wait_until([&] { return answer.has_value(); }));
    return std::move(*answer);
}

ScreenReadResult read(SessionScreenText& screen_text, const beebium::GetScreenTextRequest& request) {
    std::optional<ScreenReadResult> answer;
    screen_text.read(request, [&](ScreenReadResult result) { answer = std::move(result); });
    REQUIRE(wait_until([&] { return answer.has_value(); }));
    return std::move(*answer);
}

}  // namespace

TEST_CASE("A hold brings its id, its grid and the still it holds", "[screen-text][session]") {
    FakeServer server;
    script_screen(server);
    MachineSession session;
    connect_session(session, server);
    const auto held = hold(session.screen_text());
    REQUIRE(held);
    CHECK(held->hold_id == 42);
    CHECK(held->grid.bands.size() == 1);
    REQUIRE(held->frame);
    CHECK(held->frame->width == 640);
    CHECK(server.video.holds_asked_frame);
}

TEST_CASE("A refused hold is no hold", "[screen-text][session]") {
    FakeServer server;
    server.video.refuse_hold = true;
    MachineSession session;
    connect_session(session, server);
    CHECK_FALSE(hold(session.screen_text()));
}

TEST_CASE("A read names the hold and brings the reading", "[screen-text][session]") {
    FakeServer server;
    script_screen(server);
    MachineSession session;
    connect_session(session, server);
    const auto held = hold(session.screen_text());
    REQUIRE(held);
    const auto result = read(session.screen_text(),
                             screen_text_request(Interpretation::columns, Mode7Characters::displayed,
                                                 PixelRect{0, 0, 64, 16}, held->hold_id));
    const auto* reading = std::get_if<ScreenReading>(&result);
    REQUIRE(reading != nullptr);
    CHECK(reading->text == "HELLO");
    std::lock_guard lock(server.video.screen_mutex);
    REQUIRE(server.video.text_requests.size() == 1);
    CHECK(server.video.text_requests[0].hold_id() == 42);
    CHECK(server.video.text_requests[0].characters() == beebium::SCREEN_TEXT_CHARACTERS_DISPLAYED);
}

TEST_CASE("A read of a released or expired hold says the hold is gone",
          "[screen-text][session]") {
    FakeServer server;
    script_screen(server);
    MachineSession session;
    connect_session(session, server);
    SessionScreenText& screen_text = session.screen_text();
    const auto first = hold(screen_text);
    REQUIRE(first);
    screen_text.release(first->hold_id);
    const auto after_release = read(
        screen_text, screen_text_request(Interpretation::rows, Mode7Characters::codes, {}, first->hold_id));
    const auto* failure = std::get_if<ScreenReadFailure>(&after_release);
    REQUIRE(failure != nullptr);
    CHECK(failure->hold_lost);
    CHECK(failure->message == QStringLiteral("no screen is held under that id"));
    {
        std::lock_guard lock(server.video.screen_mutex);
        CHECK(server.video.released == std::vector<std::uint64_t>{42});
        server.video.expired.push_back(43);
    }
    const auto second = hold(screen_text);
    REQUIRE(second);
    CHECK(std::get<ScreenReadFailure>(read(screen_text, screen_text_request(Interpretation::rows,
                                                                            Mode7Characters::codes,
                                                                            {}, second->hold_id)))
              .hold_lost);
}

TEST_CASE("The live screen is read with no hold", "[screen-text][session]") {
    FakeServer server;
    script_screen(server);
    MachineSession session;
    connect_session(session, server);
    const auto result = read(session.screen_text(),
                             screen_text_request(Interpretation::rows, Mode7Characters::codes, {}, {}));
    CHECK(std::holds_alternative<ScreenReading>(result));
    CHECK(server.video.holds == 0);
}

TEST_CASE("Nothing answers after the connection ends", "[screen-text][session]") {
    FakeServer server;
    script_screen(server);
    MachineSession session;
    connect_session(session, server);
    bool answered = false;
    session.screen_text().hold([&](std::optional<HeldScreen>) { answered = true; });
    session.disconnect_from_server();
    CHECK_FALSE(session.screen_text().started());
    wait_until([] { return false; }, std::chrono::milliseconds(200));
    CHECK_FALSE(answered);
}
