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

// A machine window's presentations against an in-process server (client
// specification 6.4): immersive hides the chrome and fills the window with
// the picture, F11 and the exit bar lead out, and losing the connection
// ends it.

#include "app/ConnectionRecovery.hpp"
#include "app/DisplayFrame.hpp"
#include "app/ImmersiveExit.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"

#include "FakeBeebiumServer.hpp"

#include <QAction>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

QAction* view_action(MainWindow& window, const QString& starts_with) {
    for (QAction* action : window.actions()) {
        if (action->text().remove(QLatin1Char('&')).startsWith(starts_with)) {
            return action;
        }
    }
    return nullptr;
}

struct LiveWindow {
    FakeServer server;
    MainWindow window{QRhiWidget::Api::OpenGL};

    LiveWindow() {
        window.show();
        window.connect_to(server.endpoint());
        REQUIRE(wait_until([&] { return window.session().connected(); }));
        REQUIRE(wait_until([&] { return is_live(window.recovery().verdict()); }));
    }
    ~LiveWindow() {
        window.set_quitting(true);
        window.close();
    }

    QAction* immersive() {
        QAction* entering = view_action(window, QStringLiteral("Enter Immersive"));
        return entering ? entering : view_action(window, QStringLiteral("Exit Immersive"));
    }
    QAction* full_screen() { return view_action(window, QStringLiteral("Full Screen")); }
    ImmersiveExit* exit_bar() { return window.findChild<ImmersiveExit*>(); }
    DisplayFrame* frame() { return window.findChild<DisplayFrame*>(); }
};

}  // namespace

TEST_CASE("A window asked to open immersive goes immersive once its machine is live",
          "[immersive][window]") {
    FakeServer server;
    MainWindow window{QRhiWidget::Api::OpenGL};
    window.set_immersive_when_live();
    window.show();
    CHECK(window.presentation() == Presentation::windowed);  // nothing live yet
    window.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return window.presentation() == Presentation::immersive; }));
    // Once: leaving it is the user's to choose, and stays chosen.
    window.set_presentation(Presentation::windowed);
    window.session().disconnect_from_server();
    window.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return window.session().connected(); }));
    CHECK(window.presentation() == Presentation::windowed);
    window.set_quitting(true);
    window.close();
}

TEST_CASE("Immersive waits for a live machine", "[immersive][window]") {
    MainWindow window(QRhiWidget::Api::OpenGL);
    QAction* immersive = view_action(window, QStringLiteral("Enter Immersive"));
    REQUIRE(immersive);
    CHECK_FALSE(immersive->isEnabled());
}

TEST_CASE("Immersive hides the chrome and gives the picture the whole window",
          "[immersive][window]") {
    LiveWindow live;
    REQUIRE(live.immersive());
    REQUIRE(live.immersive()->isEnabled());
    live.immersive()->trigger();

    CHECK(live.window.presentation() == Presentation::immersive);
    CHECK_FALSE(live.window.menuBar()->isVisible());
    CHECK_FALSE(live.window.statusBar()->isVisible());
    CHECK(live.frame()->bare());
    CHECK(live.exit_bar()->bar_shown());
    CHECK(live.window.isFullScreen());
    // The command reads as the way back (6.4 scenario).
    CHECK(live.immersive()->text().remove(QLatin1Char('&')) == QStringLiteral("Exit Immersive Mode"));
}

TEST_CASE("F11 leaves immersive for a window with its chrome back", "[immersive][window]") {
    LiveWindow live;
    live.immersive()->trigger();
    REQUIRE(live.window.presentation() == Presentation::immersive);

    // The menu bar is hidden, so the shortcut must live on the window.
    CHECK(live.window.actions().contains(live.full_screen()));
    live.full_screen()->trigger();
    CHECK(live.window.presentation() == Presentation::windowed);
    CHECK_FALSE(live.window.menuBar()->isHidden());
    CHECK_FALSE(live.window.statusBar()->isHidden());
    CHECK_FALSE(live.frame()->bare());
    CHECK_FALSE(live.exit_bar()->bar_shown());
    CHECK_FALSE(live.window.isFullScreen());
}

TEST_CASE("Show Beebium is reachable while immersive, its menu bar hidden",
          "[immersive][window]") {
    LiveWindow live;
    live.immersive()->trigger();
    REQUIRE(live.window.presentation() == Presentation::immersive);
    QAction* home = view_action(live.window, QStringLiteral("Show Beebium"));
    REQUIRE(home);  // on the window, so its Ctrl+Shift+H works with the menus hidden
    CHECK(home->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+H")));
    CHECK(home->shortcutContext() == Qt::WindowShortcut);
    int requested = 0;
    QObject::connect(&live.window, &MainWindow::home_requested, [&] { ++requested; });
    home->trigger();
    CHECK(requested == 1);
}

TEST_CASE("The exit bar leaves immersive", "[immersive][window]") {
    LiveWindow live;
    live.immersive()->trigger();
    auto* button = live.exit_bar()->findChild<QPushButton*>(QStringLiteral("exitImmersive"));
    REQUIRE(button);
    button->click();
    CHECK(live.window.presentation() == Presentation::windowed);
    CHECK_FALSE(live.window.menuBar()->isHidden());
}

TEST_CASE("Full screen keeps the menus and status bar, and is not immersive",
          "[immersive][window]") {
    LiveWindow live;
    live.full_screen()->trigger();
    CHECK(live.window.presentation() == Presentation::full_screen);
    CHECK(live.window.isFullScreen());
    CHECK_FALSE(live.window.menuBar()->isHidden());
    CHECK_FALSE(live.frame()->bare());
    CHECK(live.full_screen()->isChecked());
    live.full_screen()->trigger();
    CHECK(live.window.presentation() == Presentation::windowed);
    CHECK_FALSE(live.window.isFullScreen());
}

TEST_CASE("A hidden status bar stays hidden on leaving immersive", "[immersive][window]") {
    LiveWindow live;
    QAction* status_bar = view_action(live.window, QStringLiteral("Show Status Bar"));
    REQUIRE(status_bar);
    status_bar->setChecked(false);
    live.immersive()->trigger();
    live.immersive()->trigger();
    CHECK(live.window.presentation() == Presentation::windowed);
    CHECK(live.window.statusBar()->isHidden());
    status_bar->setChecked(true);
}

TEST_CASE("Losing the connection ends immersive, showing the overlay's window",
          "[immersive][window]") {
    LiveWindow live;
    live.immersive()->trigger();
    REQUIRE(live.window.presentation() == Presentation::immersive);
    live.server.system.push(event(beebium::SERVER_STATUS_SHUTTING_DOWN));
    live.server.system.push(std::nullopt);
    REQUIRE(wait_until([&] { return !is_live(live.window.recovery().verdict()); }));
    CHECK(live.window.presentation() == Presentation::windowed);
    CHECK_FALSE(live.window.menuBar()->isHidden());
    CHECK_FALSE(live.immersive()->isEnabled());
}
