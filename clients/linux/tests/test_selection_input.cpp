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

// The display's mouse and keys as a screen text selection reads them
// (client specification 6.5): the chord that starts one, the qualifiers
// that choose its interpretation during the drag and after, the
// modifiers kept from the machine, and Escape or a plain click to end it.

#include "app/KeyboardInput.hpp"
#include "app/SelectionInput.hpp"

#include "SampleKeyTable.hpp"
#include "SelectionFakes.hpp"

#include <QKeyEvent>
#include <QMouseEvent>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

constexpr quint32 XKB_LEFT_SHIFT = 42 + 8;
constexpr quint32 XKB_LEFT_ALT = 56 + 8;
constexpr quint32 XKB_LEFT_CONTROL = 29 + 8;
constexpr quint32 XKB_ESCAPE = 1 + 8;
constexpr quint32 XKB_A = 30 + 8;

QMouseEvent mouse(QEvent::Type type, ViewPoint point, Qt::KeyboardModifiers modifiers,
                  Qt::MouseButton button = Qt::LeftButton) {
    const QPointF position(point.x, point.y);
    const Qt::MouseButtons buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : button;
    return QMouseEvent(type, position, position, position, button, buttons, modifiers);
}

QKeyEvent key(QEvent::Type type, int qt_key, quint32 scan_code,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    return QKeyEvent(type, qt_key, modifiers, scan_code, 0, 0);
}

struct Wire {
    std::vector<WireEvent> events;
    KeyboardInput::Sender sender() {
        return [this](const std::vector<WireEvent>& sent) {
            events.insert(events.end(), sent.begin(), sent.end());
        };
    }
};

struct Rig {
    Fakes fakes;
    Wire wire;
    KeyboardInput keyboard;
    std::unique_ptr<SelectionCoordinator> coordinator;
    std::unique_ptr<SelectionInput> input;

    Rig() {
        keyboard.attach(sample_key_table(), wire.sender());
        SelectionServices services = fakes.services();
        services.release_machine_keys = [this] {
            ++fakes.keys_released;
            keyboard.yield_to_window();
        };
        coordinator = std::make_unique<SelectionCoordinator>(std::move(services));
        input = std::make_unique<SelectionInput>(*coordinator, keyboard, [] { return filling(); });
    }
};

constexpr auto CONTROL_ALT = Qt::ControlModifier | Qt::AltModifier;

}  // namespace

TEST_CASE("Meta or Ctrl+Alt starts a selection; nothing else does", "[selection][input]") {
    CHECK(selection_chord(MODIFIER_META) == MODIFIER_META);
    CHECK(selection_chord(MODIFIER_CONTROL | MODIFIER_ALT) == (MODIFIER_CONTROL | MODIFIER_ALT));
    CHECK(selection_chord(MODIFIER_META | MODIFIER_SHIFT) == MODIFIER_META);
    CHECK_FALSE(selection_chord(0));
    CHECK_FALSE(selection_chord(MODIFIER_CONTROL));
    CHECK_FALSE(selection_chord(MODIFIER_ALT | MODIFIER_SHIFT));
}

TEST_CASE("Shift qualifies anywhere, Alt columns, Shift winning", "[selection][input]") {
    CHECK(interpretation_for(0) == Interpretation::rows);
    CHECK(interpretation_for(MODIFIER_ALT) == Interpretation::columns);
    CHECK(interpretation_for(MODIFIER_SHIFT) == Interpretation::anywhere);
    CHECK(interpretation_for(MODIFIER_SHIFT | MODIFIER_ALT) == Interpretation::anywhere);
    CHECK(interpretation_for(MODIFIER_CONTROL) == Interpretation::rows);
}

TEST_CASE("A plain drag is the machine's", "[selection][input]") {
    Rig rig;
    CHECK_FALSE(rig.input->mouse_press(mouse(QEvent::MouseButtonPress, at(0.5, 0.5), Qt::NoModifier)));
    CHECK_FALSE(rig.coordinator->selecting());
    CHECK_FALSE(rig.input->mouse_move(mouse(QEvent::MouseMove, at(0.6, 0.6), Qt::NoModifier)));
}

TEST_CASE("A Ctrl+Alt drag selects rows, and Shift beyond the chord anywhere",
          "[selection][input]") {
    Rig rig;
    CHECK(rig.input->mouse_press(mouse(QEvent::MouseButtonPress, at(0.2, 0.2), CONTROL_ALT)));
    CHECK(rig.coordinator->selecting());
    CHECK(rig.coordinator->interpretation() == Interpretation::rows);
    CHECK(rig.input->mouse_move(mouse(QEvent::MouseMove, at(0.6, 0.6), CONTROL_ALT | Qt::ShiftModifier)));
    CHECK(rig.coordinator->interpretation() == Interpretation::anywhere);
    CHECK(rig.coordinator->focus().x > rig.coordinator->anchor().x);
    CHECK(rig.input->mouse_move(mouse(QEvent::MouseMove, at(0.7, 0.7), CONTROL_ALT)));
    CHECK(rig.coordinator->interpretation() == Interpretation::rows);
}

TEST_CASE("A Meta drag with Alt selects columns", "[selection][input]") {
    Rig rig;
    CHECK(rig.input->mouse_press(
        mouse(QEvent::MouseButtonPress, at(0.2, 0.2), Qt::MetaModifier | Qt::AltModifier)));
    CHECK(rig.coordinator->interpretation() == Interpretation::columns);
}

TEST_CASE("A drag outside the picture starts nothing and is the machine's", "[selection][input]") {
    Rig rig;
    rig.input = std::make_unique<SelectionInput>(*rig.coordinator, rig.keyboard,
                                                 [] { return filling(2000, 1000); });
    CHECK_FALSE(rig.input->mouse_press(mouse(QEvent::MouseButtonPress, {10, 500}, CONTROL_ALT)));
    CHECK_FALSE(rig.coordinator->selecting());
}

TEST_CASE("Beginning lets go of the CTRL the chord pressed on the machine",
          "[selection][input]") {
    Rig rig;
    // Ctrl reached the machine on its way down; Alt made it a chord.
    rig.keyboard.key_press(key(QEvent::KeyPress, Qt::Key_Control, XKB_LEFT_CONTROL));
    rig.keyboard.key_press(key(QEvent::KeyPress, Qt::Key_Alt, XKB_LEFT_ALT, Qt::ControlModifier));
    REQUIRE_FALSE(rig.keyboard.held_keys().empty());
    rig.input->mouse_press(mouse(QEvent::MouseButtonPress, at(0.2, 0.2), CONTROL_ALT));
    CHECK(rig.keyboard.held_keys().empty());
    CHECK(rig.fakes.keys_released == 1);
}

TEST_CASE("After release the chord's keys qualify nothing until let go", "[selection][input]") {
    Rig rig;
    rig.keyboard.key_press(key(QEvent::KeyPress, Qt::Key_Control, XKB_LEFT_CONTROL));
    rig.keyboard.key_press(key(QEvent::KeyPress, Qt::Key_Alt, XKB_LEFT_ALT, Qt::ControlModifier));
    rig.input->mouse_press(mouse(QEvent::MouseButtonPress, at(0.2, 0.2), CONTROL_ALT));
    rig.input->mouse_move(mouse(QEvent::MouseMove, at(0.6, 0.6), CONTROL_ALT));
    CHECK(rig.input->mouse_release(mouse(QEvent::MouseButtonRelease, at(0.6, 0.6), CONTROL_ALT)));
    CHECK(rig.coordinator->selecting());
    rig.fakes.drain();
    rig.wire.events.clear();

    // Ctrl comes up: Alt is still the chord's, so still rows.
    CHECK(rig.input->key_release(key(QEvent::KeyRelease, Qt::Key_Control, XKB_LEFT_CONTROL, Qt::AltModifier)));
    CHECK(rig.coordinator->interpretation() == Interpretation::rows);
    // Shift alone: anywhere.
    CHECK(rig.input->key_press(key(QEvent::KeyPress, Qt::Key_Shift, XKB_LEFT_SHIFT, Qt::AltModifier)));
    CHECK(rig.coordinator->interpretation() == Interpretation::anywhere);
    CHECK(rig.input->key_release(key(QEvent::KeyRelease, Qt::Key_Shift, XKB_LEFT_SHIFT, Qt::AltModifier)));
    CHECK(rig.input->key_release(key(QEvent::KeyRelease, Qt::Key_Alt, XKB_LEFT_ALT)));
    CHECK(rig.coordinator->interpretation() == Interpretation::rows);
    // Alt pressed afresh: columns.
    CHECK(rig.input->key_press(key(QEvent::KeyPress, Qt::Key_Alt, XKB_LEFT_ALT)));
    CHECK(rig.coordinator->interpretation() == Interpretation::columns);
    // None of it reached the machine.
    CHECK(rig.wire.events.empty());
}

TEST_CASE("Other keys still reach the machine during a selection", "[selection][input]") {
    Rig rig;
    rig.input->mouse_press(mouse(QEvent::MouseButtonPress, at(0.2, 0.2), Qt::MetaModifier));
    CHECK_FALSE(rig.input->key_press(key(QEvent::KeyPress, Qt::Key_A, XKB_A)));
}

TEST_CASE("Escape ends the selection and never reaches the machine", "[selection][input]") {
    Rig rig;
    rig.input->mouse_press(mouse(QEvent::MouseButtonPress, at(0.2, 0.2), Qt::MetaModifier));
    rig.fakes.drain();
    CHECK(rig.input->key_press(key(QEvent::KeyPress, Qt::Key_Escape, XKB_ESCAPE)));
    CHECK_FALSE(rig.coordinator->selecting());
    CHECK(rig.fakes.released == std::vector<std::uint64_t>{42});
    CHECK(rig.fakes.resumed == 1);
    // With no selection, Escape is the machine's again.
    CHECK_FALSE(rig.input->key_press(key(QEvent::KeyPress, Qt::Key_Escape, XKB_ESCAPE)));
}

TEST_CASE("A plain click ends the selection and is not the machine's", "[selection][input]") {
    Rig rig;
    rig.input->mouse_press(mouse(QEvent::MouseButtonPress, at(0.2, 0.2), Qt::MetaModifier));
    rig.input->mouse_release(mouse(QEvent::MouseButtonRelease, at(0.2, 0.2), Qt::MetaModifier));
    CHECK(rig.input->mouse_press(mouse(QEvent::MouseButtonPress, at(0.5, 0.5), Qt::NoModifier)));
    CHECK_FALSE(rig.coordinator->selecting());
    // The next plain click is the machine's.
    CHECK_FALSE(rig.input->mouse_press(mouse(QEvent::MouseButtonPress, at(0.5, 0.5), Qt::NoModifier)));
}

TEST_CASE("A second chord drag starts afresh", "[selection][input]") {
    Rig rig;
    rig.input->mouse_press(mouse(QEvent::MouseButtonPress, at(0.2, 0.2), Qt::MetaModifier));
    rig.fakes.drain();
    rig.input->mouse_release(mouse(QEvent::MouseButtonRelease, at(0.3, 0.3), Qt::MetaModifier));
    rig.fakes.next_hold = 43;
    CHECK(rig.input->mouse_press(mouse(QEvent::MouseButtonPress, at(0.6, 0.6), Qt::MetaModifier)));
    rig.fakes.drain();
    CHECK(rig.fakes.released == std::vector<std::uint64_t>{42});
    CHECK(rig.coordinator->hold_id() == std::uint64_t{43});
}
