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

// The machine window's application shortcuts: every one carries Alt or
// Meta, since the BBC Micro has Ctrl and Shift but neither of those, and
// each answers to a Meta form and a Ctrl+Alt form. With the keys in the
// display, either form fires its action and nothing of it reaches the
// machine.

#include "app/ConnectionRecovery.hpp"
#include "app/DisplayWidget.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"

#include "FakeBeebiumServer.hpp"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QStatusBar>
#include <qpa/qwindowsysteminterface.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

// XKB keycodes: evdev + 8.
constexpr quint32 XKB_LEFT_CONTROL = 29 + 8;
constexpr quint32 XKB_LEFT_SHIFT = 42 + 8;
constexpr quint32 XKB_LEFT_ALT = 56 + 8;
constexpr quint32 XKB_LEFT_META = 125 + 8;
constexpr quint32 XKB_H = 35 + 8;
constexpr quint32 XKB_SLASH = 53 + 8;

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

    QAction* action(const QString& text) {
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->text().remove(QLatin1Char('&')) == text) {
                return action;
            }
        }
        return nullptr;
    }

    std::vector<std::uint32_t> keys_down() {
        std::lock_guard lock(server.keyboard.keys_mutex);
        return server.keyboard.keys_down;
    }
};

QSet<QString> forms(const QAction* action) {
    QSet<QString> named;
    for (const QKeySequence& sequence : action->shortcuts()) {
        named.insert(sequence.toString(QKeySequence::PortableText));
    }
    return named;
}

// A key as the window system delivers it, through the shortcut map.
void deliver(QWindow* window, QEvent::Type type, int key, quint32 scan_code,
             Qt::KeyboardModifiers modifiers, const QString& text = {}) {
    QWindowSystemInterface::handleExtendedKeyEvent(window, type, key, modifiers, scan_code, 0, 0,
                                                   text);
    QWindowSystemInterface::flushWindowSystemEvents();
}

struct Chord {
    std::vector<std::pair<int, quint32>> modifiers;  // Qt key, scan code, pressed in order
    Qt::KeyboardModifiers flags;
};

void press_chord(QWindow* window, const Chord& chord, int key, quint32 scan_code,
                 const QString& text) {
    Qt::KeyboardModifiers held;
    const auto flag_of = [](int qt_key) {
        return qt_key == Qt::Key_Control ? Qt::ControlModifier
               : qt_key == Qt::Key_Alt   ? Qt::AltModifier
               : qt_key == Qt::Key_Meta  ? Qt::MetaModifier
                                         : Qt::ShiftModifier;
    };
    for (const auto& [qt_key, code] : chord.modifiers) {
        deliver(window, QEvent::KeyPress, qt_key, code, held);
        held |= flag_of(qt_key);
    }
    deliver(window, QEvent::KeyPress, key, scan_code, chord.flags, text);
    deliver(window, QEvent::KeyRelease, key, scan_code, chord.flags, text);
    for (auto it = chord.modifiers.rbegin(); it != chord.modifiers.rend(); ++it) {
        held &= ~flag_of(it->first);
        deliver(window, QEvent::KeyRelease, it->first, it->second, held);
    }
}

const Chord CTRL_ALT{{{Qt::Key_Control, XKB_LEFT_CONTROL}, {Qt::Key_Alt, XKB_LEFT_ALT}},
                     Qt::ControlModifier | Qt::AltModifier};
const Chord META{{{Qt::Key_Meta, XKB_LEFT_META}}, Qt::MetaModifier};
const Chord META_SHIFT{{{Qt::Key_Meta, XKB_LEFT_META}, {Qt::Key_Shift, XKB_LEFT_SHIFT}},
                       Qt::MetaModifier | Qt::ShiftModifier};

}  // namespace

TEST_CASE("Every machine-window shortcut carries Alt or Meta, F11 aside",
          "[shortcuts][window]") {
    LiveWindow live;
    int bound = 0;
    for (QAction* action : live.window.findChildren<QAction*>()) {
        for (const QKeySequence& sequence : action->shortcuts()) {
            INFO(action->text().toStdString() << ": " << sequence.toString().toStdString());
            const QKeyCombination combination = sequence[0];
            if (combination.key() == Qt::Key_F11 &&
                combination.keyboardModifiers() == Qt::NoModifier) {
                continue;  // full screen, never the machine's
            }
            CHECK((combination.keyboardModifiers() & (Qt::AltModifier | Qt::MetaModifier)));
            ++bound;
        }
    }
    CHECK(bound > 20);
}

TEST_CASE("Each machine-window action answers to a Meta form and a Ctrl+Alt form",
          "[shortcuts][window]") {
    LiveWindow live;
    const std::vector<std::pair<QString, QSet<QString>>> table{
        {QStringLiteral("Save"), {QStringLiteral("Meta+S"), QStringLiteral("Ctrl+Alt+S")}},
        {QStringLiteral("Save As..."),
         {QStringLiteral("Meta+Shift+S"), QStringLiteral("Ctrl+Alt+Shift+S")}},
        {QStringLiteral("Close Window"), {QStringLiteral("Meta+W"), QStringLiteral("Ctrl+Alt+W")}},
        {QStringLiteral("Quit"), {QStringLiteral("Meta+Q"), QStringLiteral("Ctrl+Alt+Q")}},
        {QStringLiteral("Paste"), {QStringLiteral("Meta+V"), QStringLiteral("Ctrl+Alt+V")}},
        {QStringLiteral("Show Status Bar"),
         {QStringLiteral("Meta+/"), QStringLiteral("Ctrl+Alt+/")}},
        {QStringLiteral("Show Sidebar"),
         {QStringLiteral("Meta+Alt+S"), QStringLiteral("Ctrl+Alt+0")}},
        {QStringLiteral("Storage"), {QStringLiteral("Meta+1"), QStringLiteral("Ctrl+Alt+1")}},
        {QStringLiteral("Video"), {QStringLiteral("Meta+4"), QStringLiteral("Ctrl+Alt+4")}},
        {QStringLiteral("Sound"), {QStringLiteral("Meta+5"), QStringLiteral("Ctrl+Alt+5")}},
        {QStringLiteral("Keyboard"), {QStringLiteral("Meta+6"), QStringLiteral("Ctrl+Alt+6")}},
        {QStringLiteral("Processor"), {QStringLiteral("Meta+7"), QStringLiteral("Ctrl+Alt+7")}},
        {QStringLiteral("Enter Immersive Mode"),
         {QStringLiteral("Meta+Shift+F"), QStringLiteral("Ctrl+Alt+F")}},
        {QStringLiteral("Full Screen"), {QStringLiteral("F11")}},
        {QStringLiteral("Minimize"), {QStringLiteral("Meta+M"), QStringLiteral("Ctrl+Alt+M")}},
        {QStringLiteral("Show Beebium"),
         {QStringLiteral("Meta+Shift+H"), QStringLiteral("Ctrl+Alt+H")}},
    };
    for (const auto& [text, expected] : table) {
        INFO(text.toStdString());
        QAction* action = live.action(text);
        REQUIRE(action);
        CHECK(forms(action) == expected);
    }
}

TEST_CASE("With the keys in the display, both forms fire and nothing reaches the machine",
          "[shortcuts][window]") {
    LiveWindow live;
    live.window.activateWindow();
    live.window.display().setFocus();
    REQUIRE(wait_until([&] { return live.window.isActiveWindow(); }));
    REQUIRE(live.window.display().hasFocus());
    QWindow* handle = live.window.windowHandle();
    REQUIRE(handle);
    // The keys do reach the machine: an unmodified H is typed.
    press_chord(handle, Chord{}, Qt::Key_H, XKB_H, QStringLiteral("h"));
    REQUIRE(wait_until([&] { return live.keys_down() == std::vector<std::uint32_t>{0x54}; }));

    QAction* status_bar = live.action(QStringLiteral("Show Status Bar"));
    REQUIRE(status_bar);
    int home = 0;
    QObject::connect(&live.window, &MainWindow::home_requested, [&] { ++home; });
    const bool shown = status_bar->isChecked();

    press_chord(handle, META, Qt::Key_Slash, XKB_SLASH, QStringLiteral("/"));
    CHECK(status_bar->isChecked() == !shown);
    press_chord(handle, CTRL_ALT, Qt::Key_Slash, XKB_SLASH, QStringLiteral("/"));
    CHECK(status_bar->isChecked() == shown);
    press_chord(handle, META_SHIFT, Qt::Key_H, XKB_H, QStringLiteral("H"));
    CHECK(home == 1);
    live.window.display().setFocus();
    press_chord(handle, CTRL_ALT, Qt::Key_H, XKB_H, QStringLiteral("h"));
    CHECK(home == 2);

    // Give any stray key time to arrive: none of H or / did. CTRL, which
    // reached the machine on its way down, is all there is beside the
    // first H.
    press_chord(handle, Chord{}, Qt::Key_H, XKB_H, QStringLiteral("h"));
    REQUIRE(wait_until([&] {
        const auto keys = live.keys_down();
        return std::count(keys.begin(), keys.end(), 0x54u) == 2;
    }));
    for (const std::uint32_t ik : live.keys_down()) {
        CHECK((ik == 0x54 || ik == 0x01));
    }
    const auto keys = live.keys_down();
    CHECK(std::count(keys.begin(), keys.end(), 0x68u) == 0);
}
