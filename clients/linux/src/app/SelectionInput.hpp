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

#ifndef BEEBIUM_LINUX_APP_SELECTION_INPUT_HPP
#define BEEBIUM_LINUX_APP_SELECTION_INPUT_HPP

#include "app/SelectionCoordinator.hpp"
#include "keyboard/MappingFile.hpp"

#include <Qt>

#include <functional>

class QKeyEvent;
class QMouseEvent;

namespace beebium::linux_client {

class KeyboardInput;

// The display's mouse and keys as a screen text selection reads them
// (client specification 6.5). A plain drag is the machine's (a future BBC
// mouse), and a Meta drag the desktop's (it moves the window), so a
// selection starts on a drag with Alt held. The other modifiers held
// qualify the interpretation, Shift for anywhere and Meta for columns,
// Shift winning; Alt and Ctrl qualify nothing. They are read afresh from
// each event, never remembered from presses and releases, so a release
// that went elsewhere cannot leave a qualifier behind. Once the drag has
// begun the modifiers are the selection's and never reach the machine.
// Escape or a plain click ends it.

HostModifiers host_modifiers(Qt::KeyboardModifiers modifiers);

// The modifiers held once this key event has happened: a modifier key's
// own flag set by its press and cleared by its release, whatever the
// platform put in the event; any other key's as the event carries them.
Qt::KeyboardModifiers live_modifiers(const QKeyEvent& event);

// Whether a drag with these modifiers starts a selection.
bool starts_selection(HostModifiers held);

Interpretation interpretation_for(HostModifiers held);

class SelectionInput {
public:
    using Presentation = std::function<ScreenPresentation()>;

    SelectionInput(SelectionCoordinator& coordinator, KeyboardInput& keyboard,
                   Presentation presentation);

    // Each true when the event was the selection's, and goes no further.
    bool mouse_press(const QMouseEvent& event);
    bool mouse_move(const QMouseEvent& event);
    bool mouse_release(const QMouseEvent& event);
    bool key_press(const QKeyEvent& event);
    bool key_release(const QKeyEvent& event);

    bool dragging() const { return dragging_; }

private:
    void requalify(Qt::KeyboardModifiers held);

    SelectionCoordinator& coordinator_;
    KeyboardInput& keyboard_;
    Presentation presentation_;
    bool dragging_ = false;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SELECTION_INPUT_HPP
