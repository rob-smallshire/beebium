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
