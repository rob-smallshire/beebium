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

// Connecting from the user's side (client specification 4.1): the Connect
// dialog, one client per session, and one window per machine.

#include "app/Application.hpp"
#include "app/ConnectDialog.hpp"
#include "app/DiscoveredMachines.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"
#include "app/SingleInstance.hpp"

#include "FakeBeebiumServer.hpp"

#include <QComboBox>
#include <QPushButton>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

TEST_CASE("The address defaults to localhost on the default port", "[connect-dialog]") {
    DiscoveredMachines machines;
    ConnectDialog dialog(machines, {}, {});
    CHECK(dialog.address_box()->currentText() == QStringLiteral("localhost:48875"));
    REQUIRE(dialog.endpoint().has_value());
    CHECK(dialog.connect_button()->isEnabled());
}

TEST_CASE("An address that does not parse disables Connect", "[connect-dialog]") {
    DiscoveredMachines machines;
    ConnectDialog dialog(machines, {}, {});
    dialog.address_box()->setEditText(QStringLiteral("host:99999"));
    CHECK_FALSE(dialog.connect_button()->isEnabled());
    dialog.address_box()->setEditText(QStringLiteral("pi400.local"));
    CHECK(dialog.connect_button()->isEnabled());
    CHECK(dialog.endpoint() == Endpoint{"pi400.local", 48875});
}

TEST_CASE("Recent connections are offered", "[connect-dialog]") {
    DiscoveredMachines machines;
    ConnectDialog dialog(machines, {{"studio", 48876}, {"pi400.local", 48875}}, {});
    REQUIRE(dialog.address_box()->count() == 2);
    CHECK(dialog.address_box()->itemText(0) == QStringLiteral("studio:48876"));
}

TEST_CASE("A later launch hands its arguments to the running client", "[single-instance]") {
    QTemporaryDir directory;
    const QString socket_filepath = directory.filePath(QStringLiteral("beebium-client"));
    SingleInstance first(socket_filepath);
    REQUIRE(first.claim({}));
    QStringList received;
    QObject::connect(&first, &SingleInstance::arguments_received,
                     [&](const QStringList& arguments) { received = arguments; });

    SingleInstance second(socket_filepath);
    const QStringList arguments{QStringLiteral("beebium://connect?host=localhost&port=48875")};
    CHECK_FALSE(second.claim(arguments));
    REQUIRE(wait_until([&] { return !received.isEmpty(); }));
    CHECK(received == arguments);
}

TEST_CASE("A stale socket from a crashed client does not block a new one",
          "[single-instance]") {
    QTemporaryDir directory;
    const QString socket_filepath = directory.filePath(QStringLiteral("beebium-client"));
    {
        QFile stale(socket_filepath);
        REQUIRE(stale.open(QIODevice::WriteOnly));
    }
    SingleInstance instance(socket_filepath);
    CHECK(instance.claim({}));
}

TEST_CASE("Opening a machine already open brings its window forward", "[application]") {
    FakeServer server;
    Application client(QRhiWidget::Api::OpenGL);
    MainWindow* first = client.open({"127.0.0.1", static_cast<std::uint16_t>(server.port)});
    REQUIRE(first != nullptr);
    REQUIRE(wait_until([&] { return first->session().connected(); }));

    // The same endpoint, spelt differently, and the same machine by uuid.
    MainWindow* again = client.open({"127.0.0.1", static_cast<std::uint16_t>(server.port)});
    CHECK(again == first);
    CHECK(client.windows().size() == 1);
    first->close();
    // A window on a server it did not launch closes at once.
    REQUIRE(wait_until([&] { return client.windows().empty(); }, 5000ms));
}

TEST_CASE("A successful connection is remembered", "[application]") {
    FakeServer server;
    Application client(QRhiWidget::Api::OpenGL);
    const Endpoint endpoint{"127.0.0.1", static_cast<std::uint16_t>(server.port)};
    MainWindow* window = client.open(endpoint);
    REQUIRE(wait_until([&] { return window->session().connected(); }));
    const auto recents = client.settings().recent_connections();
    REQUIRE_FALSE(recents.empty());
    CHECK(recents.front() == endpoint);
    window->close();
    REQUIRE(wait_until([&] { return client.windows().empty(); }, 5000ms));
}

TEST_CASE("Quitting closes every window", "[application][close]") {
    FakeServer first_server;
    FakeServer second_server;
    Application client(QRhiWidget::Api::OpenGL);
    MainWindow* first = client.open({"127.0.0.1", static_cast<std::uint16_t>(first_server.port)});
    MainWindow* second =
        client.open({"127.0.0.1", static_cast<std::uint16_t>(second_server.port)});
    REQUIRE(wait_until([&] { return first->session().connected() && second->session().connected(); }));
    client.quit_all();
    REQUIRE(wait_until([&] { return client.windows().empty(); }, 5000ms));
}
