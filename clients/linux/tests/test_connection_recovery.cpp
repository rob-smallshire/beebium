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

// Recovery driving a real session against a fake server, and what the
// overlay says (client specification 4.3, 4.4).

#include "app/ConnectionOverlay.hpp"
#include "app/ConnectionRecovery.hpp"
#include "app/MachineSession.hpp"
#include "app/SystemPowerMonitor.hpp"

#include "FakeBeebiumServer.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

class FakePowerMonitor final : public SystemPowerMonitor {
public:
    void sleep() { emit will_sleep(); }
    void wake() { emit did_wake(); }
};

struct Harness {
    FakeServer server;
    MachineSession session;
    FakePowerMonitor power;
    ConnectionRecovery recovery{session, &power};
    int connections = 0;
    std::vector<ConnectionVerdict> verdicts;

    Harness() {
        QObject::connect(&session, &MachineSession::connected_to_server,
                         [this] { ++connections; });
        QObject::connect(&recovery, &ConnectionRecovery::verdict_changed,
                         [this](ConnectionVerdict v, int) { verdicts.push_back(v); });
        session.connect_to(server.endpoint());
        REQUIRE(wait_until([this] { return connections == 1; }));
        server.system.push(event(beebium::SERVER_STATUS_READY));
    }

    bool saw(ConnectionVerdict verdict) const {
        return std::find(verdicts.begin(), verdicts.end(), verdict) != verdicts.end();
    }
};

}  // namespace

TEST_CASE("A connection that dies is rebuilt, and the window goes live again",
          "[recovery]") {
    Harness h;
    h.server.system.push(std::nullopt);  // the status stream ends, unannounced
    REQUIRE(wait_until([&] { return h.saw(ConnectionVerdict::reconnecting); }));
    // The first attempt comes after half a second and finds the server.
    REQUIRE(wait_until([&] { return h.connections == 2; }));
    CHECK(h.recovery.verdict() == ConnectionVerdict::live);
    CHECK(h.verdicts.back() == ConnectionVerdict::live);
    CHECK_FALSE(h.saw(ConnectionVerdict::died));
}

TEST_CASE("A graceful stop is shown as stopped and never reconnected", "[recovery]") {
    Harness h;
    h.server.system.push(event(beebium::SERVER_STATUS_SHUTTING_DOWN));
    h.server.system.push(std::nullopt);
    REQUIRE(wait_until([&] { return h.recovery.verdict() == ConnectionVerdict::stopped; }));
    wait_until([] { return false; }, 1500ms);
    CHECK(h.connections == 1);
    CHECK_FALSE(h.saw(ConnectionVerdict::reconnecting));
}

TEST_CASE("Waking from sleep rebuilds a connection that looked live", "[recovery]") {
    Harness h;
    h.power.wake();
    REQUIRE(wait_until([&] { return h.connections == 2; }));
    CHECK(h.recovery.verdict() == ConnectionVerdict::live);
}

TEST_CASE("A closing window cancels recovery", "[recovery]") {
    Harness h;
    h.server.system.push(std::nullopt);
    REQUIRE(wait_until([&] { return h.saw(ConnectionVerdict::reconnecting); }));
    h.recovery.window_closing();
    wait_until([] { return false; }, 1500ms);
    CHECK(h.connections == 1);
}

TEST_CASE("The overlay says nothing while live", "[overlay]") {
    CHECK_FALSE(overlay_content(ConnectionVerdict::live, "Model B", 0).visible);
}

TEST_CASE("Each loss reads differently, and only terminal ones have no spinner",
          "[overlay]") {
    const auto unreachable = overlay_content(ConnectionVerdict::unreachable, "Model B", 0);
    const auto reconnecting = overlay_content(ConnectionVerdict::reconnecting, "Model B", 3);
    const auto gave_up = overlay_content(ConnectionVerdict::could_not_reconnect, "Model B", 8);
    const auto stopped = overlay_content(ConnectionVerdict::stopped, "Model B", 0);
    const auto died = overlay_content(ConnectionVerdict::died, "Model B", 0);

    CHECK(unreachable.spinner);
    CHECK(reconnecting.spinner);
    CHECK(reconnecting.detail.contains(QStringLiteral("3")));
    CHECK_FALSE(stopped.spinner);
    CHECK_FALSE(died.spinner);
    CHECK(gave_up.retry);
    CHECK_FALSE(stopped.retry);
    CHECK_FALSE(died.retry);
    for (const auto& content : {unreachable, reconnecting, gave_up, stopped, died}) {
        CHECK(content.visible);
        CHECK(content.close);
        CHECK(content.title.contains(QStringLiteral("Model B")));
    }
    // Stopped and died must not read alike (4.3).
    CHECK(stopped.title != died.title);
}
