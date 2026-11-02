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

#ifndef BEEBIUM_LINUX_APP_SIDEBAR_MODE_HPP
#define BEEBIUM_LINUX_APP_SIDEBAR_MODE_HPP

#include "connection/ReconnectPolicy.hpp"

#include <QIcon>
#include <QString>

#include <vector>

namespace beebium::linux_client {

// The sidebar's modes, in the fixed order of client specification 13.1, as
// the macOS client numbers them (its Command-digit is Ctrl+digit here).
enum class SidebarMode {
    storage = 1,
    memory = 2,
    peripherals = 3,
    video = 4,
    sound = 5,
    keyboard = 6,
    processor = 7,
    network = 8,
};

QString sidebar_mode_label(SidebarMode mode);
// The mode's freedesktop icon name, from the theme's devices where one
// fits, so the modes' icons are one set; and the icon, the style's own
// where the theme lacks the name.
QString sidebar_mode_icon_name(SidebarMode mode);
QIcon sidebar_mode_icon(SidebarMode mode);
// Whether the mode shows or drives the server's state, rather than this
// client's own settings (display style, keyboard mapping).
bool shows_server_state(SidebarMode mode);

// The modes this client offers so far, in order.
std::vector<SidebarMode> offered_sidebar_modes();

// How a mode presents itself while the server is not there to back it
// (13.1): a banner saying why, over its content dimmed and disabled, so
// last-known values are not taken for current ones. The macOS client's
// wording.
struct StalePresentation {
    bool stale = false;
    QString title;
    QString detail;
};

StalePresentation stale_presentation(SidebarMode mode, ConnectionVerdict verdict);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SIDEBAR_MODE_HPP
