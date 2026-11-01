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
// native scan code, host modifiers from tracking, characters, auto-repeat,
// the built-in mappings, and release on demand.

#include "app/KeyboardInput.hpp"

#include "SampleKeyTable.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using beebium::linux_client::testing::sample_key_table;

namespace {

// XKB keycodes: evdev + 8.
constexpr quint32 XKB_A = 30 + 8;
constexpr quint32 XKB_M = 50 + 8;
constexpr quint32 XKB_LEFT_SHIFT = 42 + 8;
constexpr quint32 XKB_LEFT_ALT = 56 + 8;
constexpr quint32 XKB_F12 = 88 + 8;
constexpr quint32 XKB_CAPS_LOCK = 58 + 8;

QKeyEvent press(int key, quint32 scan_code, const QString& text = {},
                Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool auto_repeat = false) {
    return QKeyEvent(QEvent::KeyPress, key, modifiers, scan_code, 0, 0, text, auto_repeat);
}

QKeyEvent release(int key, quint32 scan_code, const QString& text = {},
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    return QKeyEvent(QEvent::KeyRelease, key, modifiers, scan_code, 0, 0, text);
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
    const auto host = host_key_from_event(press(Qt::Key_A, XKB_A, QStringLiteral("a")), 0);
    REQUIRE(host);
    CHECK(host->code == "KeyA");
    CHECK(host->evdev == 30u);
    CHECK(host->text == "a");
    CHECK(host->modifiers == 0);
}

TEST_CASE("Under CTRL the character comes from the key, not the text", "[keyboard][qt]") {
    // Qt reports Ctrl+M as "\r"; the key is M.
    const auto event = press(Qt::Key_M, XKB_M, QStringLiteral("\r"), Qt::ControlModifier);
    CHECK(host_key_from_event(event, MODIFIER_CONTROL)->text == "m");
    CHECK(host_key_from_event(event, MODIFIER_CONTROL | MODIFIER_SHIFT)->text == "M");
}

TEST_CASE("An event with no native scan code has no physical identity", "[keyboard][qt]") {
    CHECK_FALSE(host_key_from_event(press(Qt::Key_A, 0, QStringLiteral("a")), 0).has_value());
}

TEST_CASE("The built-in mappings are compiled in", "[keyboard][qt]") {
    for (const char* name :
         {"Default", "Logical", "Arcadians", "Boffin", "Planetoid", "Snapper", "Thrust"}) {
        INFO(name);
        CHECK(built_in_mapping(QString::fromLatin1(name)).has_value());
    }
    CHECK_FALSE(built_in_mapping(QStringLiteral("Nonesuch")).has_value());
    KeyboardInput input;
    CHECK(input.mapping().name == "Default");
}

TEST_CASE("Typing reaches the wire, and auto-repeat does not", "[keyboard][qt]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());

    CHECK(input.key_press(press(Qt::Key_A, XKB_A, QStringLiteral("a"))));
    CHECK(input.key_press(press(Qt::Key_A, XKB_A, QStringLiteral("a"), Qt::NoModifier, true)));
    CHECK(input.key_release(release(Qt::Key_A, XKB_A, QStringLiteral("a"))));
    CHECK(wire.events ==
          std::vector<WireEvent>{{WireAction::key_down, 0x41}, {WireAction::key_up, 0x41}});
}

TEST_CASE("Host modifiers come from the keys tracked as down, not the event's flags",
          "[keyboard][qt]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());

    // Alt+F12 is Break under Default. The F12 event's own flags claim nothing.
    input.key_press(press(Qt::Key_Alt, XKB_LEFT_ALT));
    CHECK(input.held_modifiers() == MODIFIER_ALT);
    CHECK(input.key_press(press(Qt::Key_F12, XKB_F12)));
    CHECK(wire.events == std::vector<WireEvent>{{WireAction::break_down, 0}});
    input.key_release(release(Qt::Key_F12, XKB_F12));
    input.key_release(release(Qt::Key_Alt, XKB_LEFT_ALT));
    CHECK(input.held_modifiers() == 0);

    // And flags claiming Alt with no Alt key down are not believed.
    wire.events.clear();
    CHECK_FALSE(input.key_press(press(Qt::Key_F12, XKB_F12, {}, Qt::AltModifier)));
    CHECK(wire.events.empty());
}

TEST_CASE("Releasing everything forgets the host modifiers too", "[keyboard][qt]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());
    input.key_press(press(Qt::Key_Alt, XKB_LEFT_ALT));
    input.release_all();
    CHECK(input.held_modifiers() == 0);
}

TEST_CASE("Detaching releases what is held and sends nothing after", "[keyboard][qt]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());
    input.key_press(press(Qt::Key_Shift, XKB_LEFT_SHIFT, {}, Qt::ShiftModifier));
    input.detach();
    CHECK(wire.events == std::vector<WireEvent>{{WireAction::key_down, BBC_SHIFT_IK},
                                                {WireAction::key_up, BBC_SHIFT_IK}});

    wire.events.clear();
    CHECK_FALSE(input.key_press(press(Qt::Key_A, XKB_A, QStringLiteral("a"))));
    CHECK(wire.events.empty());
}

namespace {

std::vector<WireEvent> caps_syncs(const std::vector<WireEvent>& events) {
    std::vector<WireEvent> syncs;
    for (const auto& event : events) {
        if (event.action == WireAction::caps_lock_sync) {
            syncs.push_back(event);
        }
    }
    return syncs;
}

}  // namespace

TEST_CASE("Syncing sends the host Caps Lock state when the mapping asks",
          "[keyboard][qt][caps-lock]") {
    Wire wire;
    KeyboardInput input;  // Default: synchronizeCapsLock on
    input.attach(sample_key_table(), wire.sender());
    input.set_host_caps_lock([] { return std::optional<bool>(false); });
    input.sync_caps_lock();
    const auto syncs = caps_syncs(wire.events);
    REQUIRE(syncs.size() == 1);
    CHECK_FALSE(syncs[0].caps_lock_on);
}

TEST_CASE("Syncing does nothing when the host state is unknown or sync is off",
          "[keyboard][qt][caps-lock]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());
    input.sync_caps_lock();  // no host reader
    input.set_host_caps_lock([] { return std::optional<bool>(); });
    input.sync_caps_lock();  // unknown
    CHECK(caps_syncs(wire.events).empty());

    input.set_host_caps_lock([] { return std::optional<bool>(true); });
    input.set_mapping(*built_in_mapping(QStringLiteral("Thrust")));  // sync off
    input.sync_caps_lock();
    CHECK(caps_syncs(wire.events).empty());
}

TEST_CASE("Choosing a synchronising mapping is a sync point", "[keyboard][qt][caps-lock]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());
    input.set_host_caps_lock([] { return std::optional<bool>(true); });
    input.set_mapping(*built_in_mapping(QStringLiteral("Logical")));
    CHECK(caps_syncs(wire.events).size() == 1);
}

TEST_CASE("A synchronised host Caps Lock key is a toggle, not a held key",
          "[keyboard][qt][caps-lock]") {
    Wire wire;
    KeyboardInput input;
    input.attach(sample_key_table(), wire.sender());
    int pressed = 0;
    input.set_caps_lock_pressed([&] { ++pressed; });
    CHECK(input.key_press(press(Qt::Key_CapsLock, XKB_CAPS_LOCK)));
    input.key_release(release(Qt::Key_CapsLock, XKB_CAPS_LOCK));
    CHECK(pressed == 1);
    CHECK(wire.events.empty());
    CHECK(input.held_keys().empty());
}

TEST_CASE("Unsynchronised, the host Caps Lock key resolves like any other",
          "[keyboard][qt][caps-lock]") {
    Wire wire;
    KeyboardInput input;
    input.set_mapping(*built_in_mapping(QStringLiteral("Thrust")));
    input.attach(sample_key_table(), wire.sender());
    int pressed = 0;
    input.set_caps_lock_pressed([&] { ++pressed; });
    // Thrust names no CapsLock entry and maps no characters: ignored.
    CHECK_FALSE(input.key_press(press(Qt::Key_CapsLock, XKB_CAPS_LOCK)));
    CHECK(pressed == 0);
}

TEST_CASE("A BBC key goes down and up by its catalogue name", "[keyboard][qt]") {
    Wire wire;
    KeyboardInput input;
    CHECK_FALSE(input.send_named_key("Shift Lock", true));  // detached
    input.attach(sample_key_table(), wire.sender());
    CHECK(input.send_named_key("Shift Lock", true));
    CHECK(input.send_named_key("Shift Lock", false));
    CHECK_FALSE(input.send_named_key("Nonesuch", true));
    REQUIRE(wire.events.size() == 2);
    CHECK(wire.events[0] == WireEvent{WireAction::key_down, 0x50, false});
    CHECK(wire.events[1] == WireEvent{WireAction::key_up, 0x50, false});
}
