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

#include "app/ImmersiveExit.hpp"
#include "app/Presentation.hpp"

#include <QApplication>
#include <QCursor>
#include <QMouseEvent>
#include <QPushButton>
#include <QWidget>

#include <catch2/catch_test_macros.hpp>

#include "WaitUntil.hpp"

using namespace beebium::linux_client;
using beebium::linux_client::testing::wait_until;

TEST_CASE("Each presentation shows its own chrome", "[presentation]") {
    const auto windowed = chrome_for(Presentation::windowed);
    CHECK(windowed.menu_bar);
    CHECK(windowed.status_bar);
    CHECK_FALSE(windowed.os_full_screen);

    const auto full = chrome_for(Presentation::full_screen);
    CHECK(full.menu_bar);
    CHECK(full.status_bar);
    CHECK(full.os_full_screen);

    // 6.4: no application chrome at all while immersive.
    const auto immersive = chrome_for(Presentation::immersive);
    CHECK_FALSE(immersive.menu_bar);
    CHECK_FALSE(immersive.status_bar);
    CHECK(immersive.os_full_screen);
}

TEST_CASE("Only a change out of full screen is the desktop leaving it", "[presentation]") {
    CHECK(left_full_screen(Qt::WindowFullScreen, Qt::WindowNoState));
    CHECK(left_full_screen(Qt::WindowFullScreen, Qt::WindowMaximized));
    // Entering full screen on labwc: an interim change from no state to no
    // state, then the full screen itself.
    CHECK_FALSE(left_full_screen(Qt::WindowNoState, Qt::WindowNoState));
    CHECK_FALSE(left_full_screen(Qt::WindowNoState, Qt::WindowFullScreen));
    CHECK_FALSE(left_full_screen(Qt::WindowFullScreen, Qt::WindowFullScreen | Qt::WindowActive));
}

TEST_CASE("F11 toggles full screen and leaves immersive", "[presentation]") {
    CHECK(after_full_screen_key(Presentation::windowed) == Presentation::full_screen);
    CHECK(after_full_screen_key(Presentation::full_screen) == Presentation::windowed);
    CHECK(after_full_screen_key(Presentation::immersive) == Presentation::windowed);
}

TEST_CASE("The immersive command enters and leaves immersive", "[presentation]") {
    CHECK(after_immersive_command(Presentation::windowed) == Presentation::immersive);
    CHECK(after_immersive_command(Presentation::full_screen) == Presentation::immersive);
    CHECK(after_immersive_command(Presentation::immersive) == Presentation::windowed);
}

TEST_CASE("Losing the connection ends immersive and nothing else", "[presentation]") {
    CHECK(after_connection_lost(Presentation::immersive) == Presentation::windowed);
    CHECK(after_connection_lost(Presentation::full_screen) == Presentation::full_screen);
    CHECK(after_connection_lost(Presentation::windowed) == Presentation::windowed);
}

namespace {

std::chrono::milliseconds rest_wait() {
    return std::chrono::milliseconds(ImmersiveExit::REST_MS * 3);
}

// Let the enter and leave events a pointer move or a show makes arrive.
void settle() {
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
}

void move_mouse(QWidget& widget) {
    QMouseEvent event(QEvent::MouseMove, QPointF(10, 10), widget.mapToGlobal(QPointF(10, 10)),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

}  // namespace

TEST_CASE("The exit bar is seen on entering immersive, then rests", "[presentation][immersive]") {
    QWidget display;
    display.resize(640, 512);
    display.show();
    QCursor::setPos(display.mapToGlobal(QPoint(320, 400)));
    ImmersiveExit exit(&display);
    CHECK_FALSE(exit.bar_shown());

    exit.set_active(true);
    CHECK(exit.bar_shown());
    CHECK_FALSE(exit.pointer_hidden());
    CHECK(exit.width() == display.width());

    REQUIRE(wait_until([&] { return !exit.bar_shown(); }, rest_wait()));
    CHECK(exit.pointer_hidden());
    CHECK(display.cursor().shape() == Qt::BlankCursor);
}

TEST_CASE("A pointer position never seen to enter the bar does not hold it", "[presentation][immersive]") {
    // On Wayland, QCursor::pos() is not the pointer's place on the screen:
    // on labwc it read 0,0, inside the bar's strip, with the pointer away,
    // and the bar never rested.
    QWidget display;
    display.resize(640, 512);
    display.show();
    ImmersiveExit exit(&display);
    exit.set_active(true);
    QCursor::setPos(exit.mapToGlobal(QPoint(5, 5)));
    settle();
    // The position reads as on the bar, but no enter says the pointer is.
    exit.setAttribute(Qt::WA_UnderMouse, false);
    REQUIRE(wait_until([&] { return !exit.bar_shown(); }, rest_wait()));
}

TEST_CASE("A pointer resting on the bar keeps it", "[presentation][immersive]") {
    QWidget display;
    display.resize(640, 512);
    display.show();
    QCursor::setPos(display.mapToGlobal(QPoint(320, 400)));
    ImmersiveExit exit(&display);
    exit.set_active(true);
    settle();
    exit.setAttribute(Qt::WA_UnderMouse, true);  // as an enter leaves it
    CHECK_FALSE(wait_until([&] { return !exit.bar_shown(); }, rest_wait()));
    exit.setAttribute(Qt::WA_UnderMouse, false);
    REQUIRE(wait_until([&] { return !exit.bar_shown(); }, rest_wait()));
}

TEST_CASE("Moving the mouse brings the exit bar and the pointer back",
          "[presentation][immersive]") {
    QWidget display;
    display.resize(640, 512);
    display.show();
    QCursor::setPos(display.mapToGlobal(QPoint(320, 400)));
    ImmersiveExit exit(&display);
    exit.set_active(true);
    REQUIRE(wait_until([&] { return exit.pointer_hidden(); }, rest_wait()));

    move_mouse(display);
    CHECK(exit.bar_shown());
    CHECK_FALSE(exit.pointer_hidden());
    CHECK(display.cursor().shape() != Qt::BlankCursor);
}

TEST_CASE("The exit bar asks to leave and never takes the keyboard",
          "[presentation][immersive]") {
    QWidget display;
    display.resize(640, 512);
    display.show();
    QCursor::setPos(display.mapToGlobal(QPoint(320, 400)));
    ImmersiveExit exit(&display);
    exit.set_active(true);
    auto* button = exit.findChild<QPushButton*>(QStringLiteral("exitImmersive"));
    REQUIRE(button);
    CHECK(button->focusPolicy() == Qt::NoFocus);

    int requested = 0;
    QObject::connect(&exit, &ImmersiveExit::exit_requested, [&] { ++requested; });
    button->click();
    CHECK(requested == 1);
}

TEST_CASE("Ending immersive hides the bar and restores the pointer",
          "[presentation][immersive]") {
    QWidget display;
    display.resize(640, 512);
    display.show();
    QCursor::setPos(display.mapToGlobal(QPoint(320, 400)));
    ImmersiveExit exit(&display);
    exit.set_active(true);
    REQUIRE(wait_until([&] { return exit.pointer_hidden(); }, rest_wait()));

    exit.set_active(false);
    CHECK_FALSE(exit.bar_shown());
    CHECK_FALSE(exit.pointer_hidden());
    CHECK(display.cursor().shape() != Qt::BlankCursor);

    // Mouse movement outside immersive shows nothing.
    move_mouse(display);
    CHECK_FALSE(exit.bar_shown());
}
