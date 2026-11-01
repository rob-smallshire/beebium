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

// Keys and focus, client specification 7.1 and 13.2 as ruled: while the
// display has focus the emulated keyboard wins, CTRL included; the display
// shows that it has focus and the status bar says where keys go; Ctrl+Tab
// leaves it when the mapping does not claim it.

#include "app/DisplayFrame.hpp"
#include "app/DisplayWidget.hpp"
#include "app/KeyboardInput.hpp"
#include "connection/FrameMailbox.hpp"

#include "FakeBeebiumServer.hpp"
#include "SampleKeyTable.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using beebium::linux_client::testing::sample_key_table;
using beebium::linux_client::testing::wait_until;
using namespace std::chrono_literals;

namespace {

constexpr quint32 XKB_M = 50 + 8;
constexpr quint32 XKB_F = 33 + 8;
constexpr quint32 XKB_A = 30 + 8;
constexpr quint32 XKB_TAB = 15 + 8;
constexpr quint32 XKB_F11 = 87 + 8;
constexpr quint32 XKB_F12 = 88 + 8;
constexpr quint32 XKB_LEFT_CTRL = 29 + 8;
constexpr quint32 XKB_LEFT_ALT = 56 + 8;

QKeyEvent key(QEvent::Type type, int qt_key, quint32 scan_code,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString& text = {}) {
    return QKeyEvent(type, qt_key, modifiers, scan_code, 0, 0, text);
}

struct Wire {
    std::vector<WireEvent> events;
    KeyboardInput::Sender sender() {
        return [this](const std::vector<WireEvent>& sent) {
            events.insert(events.end(), sent.begin(), sent.end());
        };
    }
};

}  // namespace

TEST_CASE("Keys go to the machine only while the display has focus and a machine is there",
          "[focus]") {
    CHECK(keys_go_to(true, true) == KeysGoTo::machine);
    CHECK(keys_go_to(true, false) == KeysGoTo::application);
    CHECK(keys_go_to(false, true) == KeysGoTo::application);
    CHECK(keys_go_to(false, false) == KeysGoTo::application);
}

TEST_CASE("Under the Default mapping Ctrl shortcuts are the machine's, Ctrl+Tab is not",
          "[focus][shortcuts]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());
    // Ctrl held, as the user holds it.
    input.key_press(key(QEvent::KeyPress, Qt::Key_Control, XKB_LEFT_CTRL, Qt::ControlModifier));
    // (The sample table has M and A; the server's has every letter.)
    CHECK(input.takes(key(QEvent::ShortcutOverride, Qt::Key_M, XKB_M, Qt::ControlModifier)));
    CHECK(input.takes(key(QEvent::ShortcutOverride, Qt::Key_A, XKB_A, Qt::ControlModifier)));
    CHECK_FALSE(input.takes(key(QEvent::ShortcutOverride, Qt::Key_Tab, XKB_TAB, Qt::ControlModifier)));
    input.release_all();

    CHECK_FALSE(input.takes(key(QEvent::ShortcutOverride, Qt::Key_F11, XKB_F11)));
    input.key_press(key(QEvent::KeyPress, Qt::Key_Alt, XKB_LEFT_ALT, Qt::AltModifier));
    // Alt with a menu letter is the application's; Alt+F12 is BREAK.
    CHECK_FALSE(input.takes(key(QEvent::ShortcutOverride, Qt::Key_F, XKB_F, Qt::AltModifier)));
    CHECK(input.takes(key(QEvent::ShortcutOverride, Qt::Key_F12, XKB_F12, Qt::AltModifier)));
}

TEST_CASE("Nothing is the machine's while no machine is attached", "[focus][shortcuts]") {
    KeyboardInput input;
    CHECK_FALSE(input.takes(key(QEvent::ShortcutOverride, Qt::Key_M, XKB_M, Qt::ControlModifier)));
}

TEST_CASE("CTRL and CTRL with a letter reach the machine as matrix events", "[focus][ctrl]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());
    input.key_press(key(QEvent::KeyPress, Qt::Key_Control, XKB_LEFT_CTRL, Qt::ControlModifier));
    input.key_press(key(QEvent::KeyPress, Qt::Key_M, XKB_M, Qt::ControlModifier,
                        QStringLiteral("\r")));
    input.key_release(key(QEvent::KeyRelease, Qt::Key_M, XKB_M, Qt::ControlModifier));
    input.key_release(key(QEvent::KeyRelease, Qt::Key_Control, XKB_LEFT_CTRL));
    const std::vector<WireEvent> expected{{WireAction::key_down, BBC_CTRL_IK},
                                          {WireAction::key_down, 0x65},
                                          {WireAction::key_up, 0x65},
                                          {WireAction::key_up, BBC_CTRL_IK}};
    CHECK(wire.events == expected);
}

TEST_CASE("BREAK from a control is held like a key and released with the rest",
          "[focus][break]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());
    input.press_break();
    input.release_break();
    CHECK(wire.events == std::vector<WireEvent>{{WireAction::break_down, 0}, {WireAction::break_up, 0}});
    wire.events.clear();
    input.press_break();
    input.detach();  // focus loss or disconnect: not left in reset
    CHECK(wire.events == std::vector<WireEvent>{{WireAction::break_down, 0}, {WireAction::break_up, 0}});
}

TEST_CASE("The display shows focus, says where keys go, and Ctrl+Tab leaves it",
          "[focus][display]") {
    FrameMailbox mailbox;
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* display = new DisplayWidget(mailbox);
    auto* frame = new DisplayFrame(display);
    auto* button = new QPushButton(QStringLiteral("Break"));
    button->setFocusPolicy(Qt::TabFocus);
    layout->addWidget(frame);
    layout->addWidget(button);
    window.show();
    window.activateWindow();
    REQUIRE(wait_until([&] { return window.isActiveWindow(); }, 2000ms));

    std::vector<KeysGoTo> destinations;
    QObject::connect(display, &DisplayWidget::keys_go_to_changed,
                     [&](KeysGoTo d) { destinations.push_back(d); });
    Wire wire;
    display->keyboard().attach(sample_key_table(), wire.sender());
    display->keyboard_changed();
    display->setFocus();
    REQUIRE(wait_until([&] { return display->hasFocus(); }, 2000ms));
    CHECK(frame->showing_focus());
    CHECK(display->keys_go_to() == KeysGoTo::machine);
    CHECK(destinations.back() == KeysGoTo::machine);

    QKeyEvent ctrl_tab(QEvent::KeyPress, Qt::Key_Tab, Qt::ControlModifier, XKB_TAB, 0, 0, {});
    QApplication::sendEvent(display, &ctrl_tab);
    REQUIRE(wait_until([&] { return button->hasFocus(); }, 2000ms));
    CHECK_FALSE(frame->showing_focus());
    CHECK(display->keys_go_to() == KeysGoTo::application);
    CHECK(destinations.back() == KeysGoTo::application);
    // Leaving released anything held on the machine, and sent nothing else.
    CHECK(wire.events.empty());
}
