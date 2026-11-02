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

#include "app/Presentation.hpp"

namespace beebium::linux_client {

Chrome chrome_for(Presentation presentation) {
    switch (presentation) {
    case Presentation::windowed: return {true, true, false};
    case Presentation::full_screen: return {true, true, true};
    case Presentation::immersive: return {false, false, true};
    }
    return {};
}

Presentation after_full_screen_key(Presentation current) {
    return current == Presentation::windowed ? Presentation::full_screen : Presentation::windowed;
}

Presentation after_immersive_command(Presentation current) {
    return current == Presentation::immersive ? Presentation::windowed : Presentation::immersive;
}

bool left_full_screen(Qt::WindowStates old_state, Qt::WindowStates new_state) {
    return old_state.testFlag(Qt::WindowFullScreen) && !new_state.testFlag(Qt::WindowFullScreen);
}

Presentation after_connection_lost(Presentation current) {
    return current == Presentation::immersive ? Presentation::windowed : current;
}

}  // namespace beebium::linux_client
