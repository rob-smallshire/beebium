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

#include "app/DisplayWidget.hpp"
#include "app/KeyboardInput.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"

#include "FakeBeebiumServer.hpp"
#include "WaitUntil.hpp"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

#include <catch2/catch_test_macros.hpp>

using beebium::linux_client::MainWindow;
using beebium::linux_client::testing::FakeServer;
using beebium::linux_client::testing::wait_until;

TEST_CASE("A machine window has a menu bar and a display area", "[app][window]") {
    MainWindow window;

    REQUIRE(window.menuWidget() != nullptr);
    QStringList menus;
    for (QAction* action : window.menuBar()->actions()) {
        menus << action->text().remove(QLatin1Char('&'));
    }
    // As the macOS client's: no menu of its own beyond the standard set.
    CHECK(menus == QStringList{QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("View"),
                               QStringLiteral("Window"), QStringLiteral("Help")});

    REQUIRE(window.centralWidget() != nullptr);
    CHECK(window.centralWidget()->findChild<QWidget*>(QStringLiteral("display")) != nullptr);
}

TEST_CASE("A machine window shows under the offscreen platform", "[app][window]") {
    MainWindow window;
    window.show();
    CHECK(window.isVisible());
}

TEST_CASE("The status bar holds the LEDs, then the keyboard mapping and the machine's model",
          "[app][window][status]") {
    FakeServer server;
    MainWindow window(QRhiWidget::Api::OpenGL);
    window.show();
    window.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return window.session().connected(); }));
    auto* model = window.findChild<QLabel*>(QStringLiteral("modelName"));
    auto* mapping = window.findChild<QLabel*>(QStringLiteral("mappingName"));
    REQUIRE(model);
    REQUIRE(mapping);
    REQUIRE(wait_until([&] { return model->text() == QStringLiteral("BBC Model B"); }));
    CHECK(mapping->text() ==
          QString::fromStdString(window.display().keyboard().mapping().name));
    // Right of the LEDs, the mapping then the model.
    auto* leds = window.findChild<QWidget*>(QStringLiteral("indicators"));
    REQUIRE(leds);
    CHECK(leds->x() < mapping->x());
    CHECK(mapping->x() < model->x());
    // Nothing the macOS bar does not show.
    for (const char* gone : {"breakButton", "keysLabel", "unsavedLabel", "descriptionLabel"}) {
        CHECK(window.findChild<QWidget*>(QString::fromLatin1(gone)) == nullptr);
    }
    window.set_quitting(true);
    window.close();
}

TEST_CASE("The window marks unsaved changes the platform's way, from the server's signal",
          "[app][window][unsaved]") {
    FakeServer server;
    MainWindow window(QRhiWidget::Api::OpenGL);
    window.show();
    window.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return window.session().connected(); }));
    const auto status = [](beebium::ServerStatusType kind) {
        beebium::ServerStatusEvent event;
        event.set_status(kind);
        return event;
    };
    // The title is a template the platform fills with its own marker.
    auto named = status(beebium::SERVER_STATUS_IDENTITY_CHANGED);
    named.mutable_identity()->set_name("Elite [*] Box");
    server.system.push(named);
    REQUIRE(wait_until([&] { return window.windowTitle().startsWith(QStringLiteral("Elite")); }));
    // A "[*]" in the name is the name's, not a marker.
    CHECK(window.windowTitle() == QStringLiteral("Elite [*][*] Box[*]"));
    CHECK_FALSE(window.isWindowModified());

    auto written = status(beebium::SERVER_STATUS_HEARTBEAT);
    written.set_template_media_written(true);
    server.system.push(written);
    REQUIRE(wait_until([&] { return window.isWindowModified(); }));

    // Cleared when the server says so (after a save, say).
    server.system.push(status(beebium::SERVER_STATUS_HEARTBEAT));
    REQUIRE(wait_until([&] { return !window.isWindowModified(); }));
    window.set_quitting(true);
    window.close();
}

namespace {

QAction* file_action(MainWindow& window, const QString& text) {
    for (QAction* menu : window.menuBar()->actions()) {
        if (menu->text().remove(QLatin1Char('&')) != QStringLiteral("File")) {
            continue;
        }
        for (QAction* action : menu->menu()->actions()) {
            if (action->text().remove(QLatin1Char('&')) == text) {
                return action;
            }
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Move To is offered for a saved machine only, after Rename", "[app][window][move]") {
    const QString move_text = QStringLiteral("Move To...");
    {
        // Transient: there is no document to move (5.4).
        FakeServer server;
        MainWindow window(QRhiWidget::Api::OpenGL);
        window.show();
        window.connect_to(server.endpoint());
        REQUIRE(wait_until([&] { return window.session().connected(); }));
        QAction* move = file_action(window, move_text);
        REQUIRE(move);
        CHECK_FALSE(move->isEnabled());
        window.set_quitting(true);
        window.close();
    }
    FakeServer server;
    {
        std::lock_guard lock(server.system.calls_mutex);
        server.system.saved_machine_dirpath = "/home/me/Elite.beebium-machine";
    }
    MainWindow window(QRhiWidget::Api::OpenGL);
    window.show();
    window.connect_to(server.endpoint());
    QAction* move = file_action(window, move_text);
    REQUIRE(move);
    REQUIRE(wait_until([&] { return move->isEnabled(); }));
    QStringList order;
    for (QAction* menu : window.menuBar()->actions()) {
        if (menu->text().remove(QLatin1Char('&')) == QStringLiteral("File")) {
            for (QAction* action : menu->menu()->actions()) {
                order << action->text().remove(QLatin1Char('&'));
            }
        }
    }
    CHECK(order.indexOf(move_text) == order.indexOf(QStringLiteral("Rename Machine...")) + 1);
    window.set_quitting(true);
    window.close();
}

TEST_CASE("A server on another host cannot be sent a destination chosen here",
          "[app][window][gating]") {
    const QString remote_reason = QStringLiteral("The server is on another host");
    {
        // Transient: its first Save chooses a destination, so it waits.
        FakeServer server;
        server.system.host_fingerprint = "somewhere-else";
        MainWindow window(QRhiWidget::Api::OpenGL);
        window.show();
        window.connect_to(server.endpoint());
        QAction* rename = file_action(window, QStringLiteral("Rename Machine..."));
        REQUIRE(rename);
        REQUIRE(wait_until([&] { return rename->isEnabled(); }));
        QAction* save = file_action(window, QStringLiteral("Save"));
        CHECK_FALSE(save->isEnabled());
        CHECK(save->toolTip().startsWith(remote_reason));
        CHECK_FALSE(file_action(window, QStringLiteral("Save As..."))->isEnabled());
        window.set_quitting(true);
        window.close();
    }
    // Saved: no saving of any kind until a remote server's folders can be
    // chosen (#263); Rename stays.
    FakeServer server;
    server.system.host_fingerprint = "somewhere-else";
    {
        std::lock_guard lock(server.system.calls_mutex);
        server.system.saved_machine_dirpath = "/srv/Elite.beebium-machine";
    }
    MainWindow window(QRhiWidget::Api::OpenGL);
    window.show();
    window.connect_to(server.endpoint());
    QAction* rename = file_action(window, QStringLiteral("Rename Machine..."));
    REQUIRE(wait_until([&] { return rename->isEnabled(); }));
    REQUIRE(wait_until([&] { return window.session().machine_saved(); }));
    QAction* save = file_action(window, QStringLiteral("Save"));
    CHECK_FALSE(save->isEnabled());
    CHECK(save->toolTip().startsWith(remote_reason));
    // Neither the shortcut nor the action can reach the server.
    save->trigger();
    wait_until([] { return false; }, std::chrono::milliseconds(200));
    {
        std::lock_guard lock(server.system.calls_mutex);
        CHECK(server.system.save_requests == 0);
    }
    QAction* save_as = file_action(window, QStringLiteral("Save As..."));
    CHECK_FALSE(save_as->isEnabled());
    CHECK(save_as->toolTip().startsWith(remote_reason));
    QAction* move = file_action(window, QStringLiteral("Move To..."));
    CHECK_FALSE(move->isEnabled());
    CHECK(move->toolTip().startsWith(remote_reason));
    window.set_quitting(true);
    window.close();
}

TEST_CASE("A server on this host gets every saving command", "[app][window][gating]") {
    FakeServer server;
    MainWindow window(QRhiWidget::Api::OpenGL);
    window.show();
    window.connect_to(server.endpoint());
    QAction* save = file_action(window, QStringLiteral("Save"));
    REQUIRE(wait_until([&] { return save->isEnabled(); }));
    CHECK_FALSE(save->toolTip().startsWith(QStringLiteral("The server is on another host")));
    // Transient: Save As waits for the first save, as before.
    CHECK_FALSE(file_action(window, QStringLiteral("Save As..."))->isEnabled());
    window.set_quitting(true);
    window.close();
}
