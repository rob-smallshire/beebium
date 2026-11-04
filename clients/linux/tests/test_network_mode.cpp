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

// The Network mode (client specification 12.4, 13.1), as the macOS
// client's: the Econet link and station, a pending station adopted at the
// next Break, a station editor bounded by the server, the collision gauge,
// the paused banner, and each transport's own panel by its opaque id.

#include "app/ExtensionPanel.hpp"
#include "app/MainWindow.hpp"
#include "app/NetworkSidebar.hpp"
#include "app/SessionEconet.hpp"
#include "app/SessionExtensions.hpp"
#include "app/Sidebar.hpp"

#include "FakeBeebiumServer.hpp"

#include <QAbstractButton>
#include <QLabel>
#include <QLineEdit>

#include <catch2/catch_test_macros.hpp>

#include <grpcpp/grpcpp.h>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

beebium::GetEconetStatusResponse fitted(std::uint32_t configured, std::uint32_t in_force) {
    beebium::GetEconetStatusResponse status;
    status.set_has_econet_socket(true);
    status.set_enabled(true);
    status.set_station_id(configured);
    status.set_station_in_force(in_force);
    status.set_station_min(1);
    status.set_station_max(254);
    status.set_connected(true);
    return status;
}

struct Fixture {
    FakeServer server;
    SessionEconet econet;
    SessionExtensions extensions;
    int breaks = 0;
    std::unique_ptr<NetworkSidebar> mode;

    explicit Fixture(beebium::GetEconetStatusResponse status = fitted(254, 254)) {
        server.econet.set(status);
        {
            std::lock_guard lock(server.transports.mutex);
            auto* none = server.transports.transports.add_transports();
            none->set_id("transport-1");
            none->set_name("ignored by the client");
            auto* with_ui = server.transports.transports.add_transports();
            with_ui->set_id("transport-2");
            with_ui->set_has_ui(true);
            with_ui->set_active(true);
        }
        beebium::View view;
        view.set_extension_id("transport-2");
        view.set_view_revision(1);
        view.mutable_root()->set_id("peers");
        view.mutable_root()->mutable_label()->set_text("transport panel");
        server.extension_ui.push(view);
        auto channel = grpc::CreateChannel(server.endpoint().toStdString(),
                                           grpc::InsecureChannelCredentials());
        econet.start(channel);
        extensions.start(channel);
        mode = std::make_unique<NetworkSidebar>(
            econet, extensions, [] { return true; }, [this] { ++breaks; },
            [] { return QStringLiteral("F12"); });
        mode->resize(Sidebar::WIDTH, 600);
        mode->show();
    }
    template <typename T>
    T* find(const char* name) {
        return mode->findChild<T*>(QString::fromLatin1(name));
    }
    QString text(const char* name) {
        auto* label = find<QLabel>(name);
        REQUIRE(label);
        return label->isVisibleTo(mode.get()) ? label->text() : QString();
    }
};

QStringList visible_texts(QWidget& widget) {
    QStringList all;
    for (QLabel* label : widget.findChildren<QLabel*>()) {
        if (label->isVisibleTo(&widget) && !label->text().isEmpty()) {
            all << label->text();
        }
    }
    return all;
}

}  // namespace

TEST_CASE("The Network mode shows the link and the station in force", "[network][mode]") {
    Fixture f;
    REQUIRE(wait_until([&] { return f.text("connection") == QStringLiteral("Connected"); }));
    CHECK(f.text("stationInForce") == QStringLiteral("254"));
    CHECK(f.text("stationPending").isEmpty());
    CHECK_FALSE(f.find<QAbstractButton>("pressBreak")->isVisibleTo(f.mode.get()));
    CHECK(visible_texts(*f.mode).contains(QStringLiteral("Econet Station")));
    CHECK(visible_texts(*f.mode).contains(QStringLiteral("Connection")));
}

TEST_CASE("A station set but not adopted is pending, with Break to adopt it",
          "[network][mode][station]") {
    Fixture f(fitted(100, 254));
    REQUIRE(wait_until([&] { return f.text("stationPending") == QStringLiteral("→ 100"); }));
    CHECK(f.text("stationInForce") == QStringLiteral("254"));
    auto* break_button = f.find<QAbstractButton>("pressBreak");
    REQUIRE(break_button->isVisibleTo(f.mode.get()));
    CHECK(break_button->text() == QStringLiteral("F12"));
    CHECK(break_button->toolTip() ==
          QStringLiteral("100 takes effect at the next Break. Click to press Break."));
    break_button->click();
    CHECK(f.breaks == 1);
    f.server.econet.set(fitted(100, 100));
    REQUIRE(wait_until([&] { return f.text("stationPending").isEmpty(); }));
    CHECK(f.text("stationInForce") == QStringLiteral("100"));
}

TEST_CASE("The station editor takes a number within the server's bounds",
          "[network][mode][station]") {
    Fixture f;
    REQUIRE(wait_until([&] { return f.text("connection") == QStringLiteral("Connected"); }));
    auto* editor = f.find<QWidget>("stationEditor");
    CHECK_FALSE(editor->isVisibleTo(f.mode.get()));
    f.find<QAbstractButton>("editStation")->click();
    REQUIRE(editor->isVisibleTo(f.mode.get()));
    auto* field = f.find<QLineEdit>("stationField");
    CHECK(field->placeholderText() == QStringLiteral("1–254"));
    CHECK(f.text("stationInfo") == QStringLiteral("Takes effect on next Break (F12)."));
    field->setText(QStringLiteral("300"));
    f.find<QAbstractButton>("saveStation")->click();
    CHECK(f.text("stationError") == QStringLiteral("Station number must be between 1 and 254."));
    CHECK(editor->isVisibleTo(f.mode.get()));
    field->setText(QStringLiteral("17"));
    f.find<QAbstractButton>("saveStation")->click();
    CHECK_FALSE(editor->isVisibleTo(f.mode.get()));
    REQUIRE(wait_until([&] { return f.server.econet.set_station_requests == 1; }));
    CHECK(f.server.econet.last_station == 17u);
    // Configured, not yet adopted: pending.
    REQUIRE(wait_until([&] { return f.text("stationPending") == QStringLiteral("→ 17"); }));
    // Cancel discards.
    f.find<QAbstractButton>("editStation")->click();
    f.find<QLineEdit>("stationField")->setText(QStringLiteral("5"));
    f.find<QAbstractButton>("cancelStation")->click();
    CHECK_FALSE(editor->isVisibleTo(f.mode.get()));
    wait_until([] { return false; }, std::chrono::milliseconds(100));
    CHECK(f.server.econet.set_station_requests == 1);
}

TEST_CASE("The collision gauge shows the collisions in effect, and the banner a paused transport",
          "[network][mode][collision]") {
    Fixture f;
    REQUIRE(wait_until([&] { return f.text("connection") == QStringLiteral("Connected"); }));
    CHECK(f.text("collision").isEmpty());
    CHECK_FALSE(f.find<QWidget>("pausedBanner")->isVisibleTo(f.mode.get()));
    auto status = fitted(254, 254);
    status.set_aun_station_collision_count(2);
    status.set_aun_last_station_collision("0.254 at 192.0.2.7");
    status.set_requires_real_time(true);
    status.set_gated_by_speed(true);
    f.server.econet.set(status);
    REQUIRE(wait_until([&] {
        return f.text("collision").endsWith(QStringLiteral("Station collision: 0.254 at 192.0.2.7 (2)"));
    }));
    CHECK(f.find<QWidget>("pausedBanner")->isVisibleTo(f.mode.get()));
    CHECK(visible_texts(*f.mode).contains(QStringLiteral("Econet paused")));
    status.set_aun_station_collision_count(0);
    status.set_gated_by_speed(false);
    f.server.econet.set(status);
    REQUIRE(wait_until([&] { return f.text("collision").isEmpty(); }));
    CHECK_FALSE(f.find<QWidget>("pausedBanner")->isVisibleTo(f.mode.get()));
}

TEST_CASE("A lost status stream keeps the last station, stale, and clears the gauges",
          "[network][mode][stale]") {
    auto status = fitted(254, 254);
    status.set_aun_station_collision_count(1);
    status.set_aun_last_station_collision("0.254");
    Fixture f(status);
    REQUIRE(wait_until([&] { return !f.text("collision").isEmpty(); }));
    f.server.econet.end_watches(grpc::StatusCode::UNAVAILABLE);
    REQUIRE(wait_until([&] { return f.text("connection") == QStringLiteral("Unknown"); }));
    CHECK(f.text("stationInForce") == QStringLiteral("254"));
    CHECK(f.text("collision").isEmpty());
    CHECK(f.text("staleNote") == QStringLiteral("Econet status unavailable"));
    CHECK_FALSE(f.find<QAbstractButton>("editStation")->isEnabled());
}

TEST_CASE("Each transport with a UI gets its panel by id; none is chosen by name",
          "[network][mode][transport]") {
    Fixture f;
    REQUIRE(wait_until([&] { return f.mode->findChildren<ExtensionPanel*>().size() == 1; }));
    REQUIRE(wait_until([&] { return f.server.extension_ui.subscribers("transport-2") == 1; }));
    REQUIRE(wait_until([&] { return visible_texts(*f.mode).contains(QStringLiteral("transport panel")); }));
    CHECK(f.server.transports.active_requests == 0);
    // The status is watched, and the panel subscribed, only while the mode shows.
    CHECK(f.server.econet.watchers == 1);
    f.mode->hide();
    REQUIRE(wait_until([&] {
        return f.server.econet.watchers == 0 && f.server.extension_ui.subscribers("transport-2") == 0;
    }));
}

TEST_CASE("Unfitted Econet, a lost server and a failed transport list each say so",
          "[network][mode]") {
    {
        beebium::GetEconetStatusResponse unfitted;
        unfitted.set_has_econet_socket(true);
        Fixture f(unfitted);
        REQUIRE(wait_until([&] { return visible_texts(*f.mode).contains(QStringLiteral("Econet Not Fitted")); }));
        CHECK(visible_texts(*f.mode).contains(
            QStringLiteral("Econet hardware is not fitted in this machine configuration.")));
    }
    {
        SessionEconet econet;
        SessionExtensions extensions;
        NetworkSidebar mode(econet, extensions, [] { return true; }, [] {}, [] { return QString(); });
        mode.show();
        CHECK(visible_texts(mode).contains(QStringLiteral("Econet status unavailable")));
    }
    {
        FakeServer server;
        server.econet.set(fitted(254, 254));
        server.transports.fail = true;
        SessionEconet econet;
        SessionExtensions extensions;
        NetworkSidebar mode(econet, extensions, [] { return true; }, [] {}, [] { return QString(); });
        mode.show();
        econet.start(grpc::CreateChannel(server.endpoint().toStdString(),
                                         grpc::InsecureChannelCredentials()));
        REQUIRE(wait_until([&] {
            return visible_texts(mode).contains(QStringLiteral("Transport list unavailable"));
        }));
    }
}

TEST_CASE("A machine window offers the Network mode on Meta+8", "[network][window]") {
    MainWindow window(QRhiWidget::Api::OpenGL);
    auto* sidebar = window.findChild<Sidebar*>();
    REQUIRE(sidebar);
    CHECK(sidebar->has_mode(SidebarMode::network));
    CHECK(window.findChild<NetworkSidebar*>() != nullptr);
}
