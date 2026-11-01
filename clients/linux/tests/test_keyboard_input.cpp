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

// Qt key events into the keyboard path: the physical identity from the
// native scan code, characters, auto-repeat, and release on demand.

#include "app/KeyboardInput.hpp"

#include "SampleKeyTable.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using beebium::linux_client::testing::sample_key_table;

namespace {

constexpr quint32 XKB_A = 30 + 8;
constexpr quint32 XKB_M = 50 + 8;
constexpr quint32 XKB_LEFT_SHIFT = 42 + 8;

QKeyEvent key_event(QEvent::Type type, int key, Qt::KeyboardModifiers modifiers,
                    quint32 scan_code, const QString& text, bool auto_repeat = false) {
    return QKeyEvent(type, key, modifiers, scan_code, 0, 0, text, auto_repeat);
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

TEST_CASE("A Qt key event names its physical key by W3C code", "[keyboard][qt]") {
    const auto event = key_event(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, XKB_A,
                                 QStringLiteral("a"));
    const auto host = host_key_from_event(event);
    REQUIRE(host);
    CHECK(host->code == "KeyA");
    CHECK(host->text == "a");
    CHECK_FALSE(host->ctrl_held);
}

TEST_CASE("Under CTRL the unmodified character comes from the key, not the text",
          "[keyboard][qt]") {
    const auto event = key_event(QEvent::KeyPress, Qt::Key_M, Qt::ControlModifier, XKB_M,
                                 QStringLiteral("\r"));
    const auto host = host_key_from_event(event);
    REQUIRE(host);
    CHECK(host->ctrl_held);
    CHECK(host->text_ignoring_ctrl == "m");
}

TEST_CASE("An event with no native scan code has no physical identity", "[keyboard][qt]") {
    const auto event =
        key_event(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, 0, QStringLiteral("a"));
    CHECK_FALSE(host_key_from_event(event).has_value());
}

TEST_CASE("Typing reaches the wire, and auto-repeat does not", "[keyboard][qt]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());

    CHECK(input.key_press(key_event(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, XKB_A,
                                    QStringLiteral("a"))));
    CHECK(input.key_press(key_event(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, XKB_A,
                                    QStringLiteral("a"), true)));
    CHECK(input.key_release(key_event(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, XKB_A,
                                      QStringLiteral("a"))));
    CHECK(wire.events ==
          std::vector<WireEvent>{{WireAction::key_down, 0x41}, {WireAction::key_up, 0x41}});
}

TEST_CASE("Detaching releases what is held and sends nothing after", "[keyboard][qt]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());
    input.key_press(key_event(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier,
                              XKB_LEFT_SHIFT, QString()));
    input.detach();
    CHECK(wire.events ==
          std::vector<WireEvent>{{WireAction::key_down, BBC_SHIFT_IK},
                                 {WireAction::key_up, BBC_SHIFT_IK}});

    wire.events.clear();
    CHECK_FALSE(input.key_press(key_event(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, XKB_A,
                                          QStringLiteral("a"))));
    CHECK(wire.events.empty());
}
