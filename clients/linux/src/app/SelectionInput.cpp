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

#include "app/SelectionInput.hpp"

#include "app/KeyboardInput.hpp"

#include <QKeyEvent>
#include <QMouseEvent>

namespace beebium::linux_client {

HostModifiers host_modifiers(Qt::KeyboardModifiers modifiers) {
    HostModifiers held = 0;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        held |= MODIFIER_SHIFT;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        held |= MODIFIER_CONTROL;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        held |= MODIFIER_ALT;
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        held |= MODIFIER_META;
    }
    return held;
}

Qt::KeyboardModifiers live_modifiers(const QKeyEvent& event) {
    Qt::KeyboardModifiers held = event.modifiers();
    Qt::KeyboardModifier own = Qt::NoModifier;
    switch (event.key()) {
    case Qt::Key_Shift:
        own = Qt::ShiftModifier;
        break;
    case Qt::Key_Control:
        own = Qt::ControlModifier;
        break;
    case Qt::Key_Alt:
        own = Qt::AltModifier;
        break;
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
        own = Qt::MetaModifier;
        break;
    default:
        return held;
    }
    held.setFlag(own, event.type() != QEvent::KeyRelease);
    return held;
}

bool starts_selection(HostModifiers held) {
    return (held & MODIFIER_ALT) != 0;
}

Interpretation interpretation_for(HostModifiers held) {
    if (held & MODIFIER_SHIFT) {
        return Interpretation::anywhere;
    }
    if (held & MODIFIER_META) {
        return Interpretation::columns;
    }
    return Interpretation::rows;
}

SelectionInput::SelectionInput(SelectionCoordinator& coordinator, KeyboardInput& keyboard,
                               Presentation presentation)
    : coordinator_(coordinator), keyboard_(keyboard), presentation_(std::move(presentation)) {}

bool SelectionInput::mouse_press(const QMouseEvent& event) {
    const HostModifiers held = host_modifiers(event.modifiers());
    if (event.button() == Qt::LeftButton && starts_selection(held)) {
        const QPointF point = event.position();
        if (coordinator_.begin({point.x(), point.y()}, interpretation_for(held), presentation_())) {
            dragging_ = true;
            return true;
        }
        return false;
    }
    if (coordinator_.selecting()) {
        // A plain click gives the machine back, and is not the machine's.
        coordinator_.cancel();
        dragging_ = false;
        return true;
    }
    return false;
}

bool SelectionInput::mouse_move(const QMouseEvent& event) {
    if (!dragging_) {
        return false;
    }
    requalify(event.modifiers());
    const QPointF point = event.position();
    coordinator_.update({point.x(), point.y()});
    return true;
}

bool SelectionInput::mouse_release(const QMouseEvent& event) {
    if (!dragging_ || event.button() != Qt::LeftButton) {
        return dragging_;
    }
    // The selection stays, to be requalified and copied.
    requalify(event.modifiers());
    dragging_ = false;
    return true;
}

bool SelectionInput::key_press(const QKeyEvent& event) {
    if (!coordinator_.selecting()) {
        return false;
    }
    if (event.key() == Qt::Key_Escape) {
        // Ends the selection, never reaching the machine.
        if (!event.isAutoRepeat()) {
            coordinator_.cancel();
            dragging_ = false;
        }
        return true;
    }
    requalify(live_modifiers(event));
    // A modifier key is the selection's; any other key is typed as before.
    return keyboard_.track_host_modifier(event);
}

bool SelectionInput::key_release(const QKeyEvent& event) {
    if (!coordinator_.selecting()) {
        return false;
    }
    requalify(live_modifiers(event));
    return keyboard_.track_host_modifier(event);
}

void SelectionInput::requalify(Qt::KeyboardModifiers held) {
    coordinator_.set_interpretation(interpretation_for(host_modifiers(held)));
}

}  // namespace beebium::linux_client
