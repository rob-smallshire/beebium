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

#ifndef BEEBIUM_LINUX_APP_PRESENTATION_HPP
#define BEEBIUM_LINUX_APP_PRESENTATION_HPP

namespace beebium::linux_client {

// How a machine window presents itself (client specification 6.4).
enum class Presentation {
    windowed,     // menus, status bar, the window manager's frame
    full_screen,  // the desktop's full screen, menus and status bar kept (F11)
    immersive,    // the picture alone, filling the screen (View > Enter
                  // Immersive Mode, as on macOS)
};

// What a presentation shows besides the picture.
struct Chrome {
    bool menu_bar = true;
    bool status_bar = true;   // when the user also has it shown
    bool os_full_screen = false;
};

Chrome chrome_for(Presentation presentation);

// F11: full screen on and off; from immersive, back to a window.
Presentation after_full_screen_key(Presentation current);

// View > Enter/Exit Immersive Mode: into immersive, and out of it to a
// window.
Presentation after_immersive_command(Presentation current);

// Immersive is only meaningful while live: a connection that is lost takes
// the window back to its normal layout, where the overlay explains (6.4).
Presentation after_connection_lost(Presentation current);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_PRESENTATION_HPP
