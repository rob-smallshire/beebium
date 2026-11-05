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

// Screen text selection in a machine window (client specification 6.5):
// a chord drag over the picture freezes it and holds the screen, the
// held still stands in for the stream, and Escape or a new connection
// let it go.

#include "app/DisplayWidget.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"

#include "FakeBeebiumServer.hpp"
#include "WaitUntil.hpp"

#include <QKeyEvent>
#include <QMouseEvent>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

DecodedFrame picture(std::uint32_t width) {
    DecodedFrame frame;
    frame.width = width;
    frame.height = 256;
    frame.display_width = 640;
    frame.display_height = 256;
    frame.regions = {{0, 256, width}};
    frame.pixels.assign(std::size_t{width} * 256 * 4, '\x10');
    return frame;
}

void send_mouse(QWidget* widget, QEvent::Type type, QPointF point, Qt::KeyboardModifiers modifiers) {
    const Qt::MouseButtons buttons =
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(Qt::LeftButton);
    QMouseEvent event(type, point, widget->mapToGlobal(point), Qt::LeftButton, buttons, modifiers);
    QCoreApplication::sendEvent(widget, &event);
}

void connect_window(MainWindow& window, FakeServer& server) {
    window.show();
    window.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return window.session().connected(); }));
}

}  // namespace

TEST_CASE("A frozen display shows a still in place of the stream until it resumes",
          "[selection][window]") {
    MainWindow window(QRhiWidget::Api::OpenGL);
    DisplayWidget& display = window.display();
    CHECK_FALSE(display.live_content());
    display.freeze();
    CHECK(display.frozen());
    display.show_still(picture(320));
    REQUIRE(display.live_content());
    CHECK(display.live_content()->width == 320);
    display.resume();
    CHECK_FALSE(display.frozen());
}

TEST_CASE("A chord drag with no picture yet starts no selection", "[selection][window]") {
    FakeServer server;
    MainWindow window(QRhiWidget::Api::OpenGL);
    connect_window(window, server);
    DisplayWidget& display = window.display();
    const QPointF centre(display.width() / 2.0, display.height() / 2.0);
    send_mouse(&display, QEvent::MouseButtonPress, centre, Qt::MetaModifier);
    CHECK_FALSE(display.frozen());
    CHECK(server.video.holds == 0);
}

TEST_CASE("A chord drag holds the screen; Escape releases it and the stream resumes",
          "[selection][window]") {
    FakeServer server;
    MainWindow window(QRhiWidget::Api::OpenGL);
    connect_window(window, server);
    DisplayWidget& display = window.display();
    display.show_still(picture(640));
    const QPointF centre(display.width() / 2.0, display.height() / 2.0);
    send_mouse(&display, QEvent::MouseButtonPress, centre, Qt::ControlModifier | Qt::AltModifier);
    CHECK(display.frozen());
    auto* overlay = display.findChild<QWidget*>(QStringLiteral("selectionOverlay"));
    REQUIRE(overlay);
    CHECK(overlay->isVisible());
    CHECK(overlay->testAttribute(Qt::WA_TransparentForMouseEvents));
    CHECK(overlay->geometry() == display.rect());
    REQUIRE(wait_until([&] { return server.video.holds == 1; }));
    send_mouse(&display, QEvent::MouseMove, centre + QPointF(40, 20),
               Qt::ControlModifier | Qt::AltModifier);
    send_mouse(&display, QEvent::MouseButtonRelease, centre + QPointF(40, 20),
               Qt::ControlModifier | Qt::AltModifier);
    // Still frozen once the button is up: the selection stays.
    CHECK(display.frozen());
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, 1 + 8, 0, 0);
    QCoreApplication::sendEvent(&display, &escape);
    CHECK_FALSE(display.frozen());
    CHECK_FALSE(overlay->isVisible());
    REQUIRE(wait_until([&] {
        std::lock_guard lock(server.video.screen_mutex);
        return server.video.released == std::vector<std::uint64_t>{42};
    }));
}

TEST_CASE("Connecting afresh ends a selection", "[selection][window]") {
    FakeServer server;
    MainWindow window(QRhiWidget::Api::OpenGL);
    connect_window(window, server);
    DisplayWidget& display = window.display();
    display.show_still(picture(640));
    const QPointF centre(display.width() / 2.0, display.height() / 2.0);
    send_mouse(&display, QEvent::MouseButtonPress, centre, Qt::MetaModifier);
    REQUIRE(display.frozen());
    window.connect_to(server.endpoint());
    CHECK_FALSE(display.frozen());
}
