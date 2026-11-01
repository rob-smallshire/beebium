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

#ifndef BEEBIUM_LINUX_APP_CLOSE_DECISION_HPP
#define BEEBIUM_LINUX_APP_CLOSE_DECISION_HPP

#include <QString>

namespace beebium::linux_client {

// What closing a machine window does to its server (client specification
// 4.5), a pure function of whether this client launched the server and how
// many other clients watch it.
enum class CloseAction {
    disconnect,  // the server is not ours, or others share it on quit: leave it
    shut_down,   // ours, and no other client: shut it down
    ask,         // ours, with other clients: the user decides
};

// `client_count` counts this window's own status stream too.
CloseAction close_action(bool launched_by_this_client, int client_count);

// On quitting the whole client, a shared server is left running without
// asking: only servers this client both launched and is the sole client of
// are shut down (4.5).
CloseAction quit_action(bool launched_by_this_client, int client_count);

// A machine directory's path for a chosen name: the .beebium-machine suffix
// added if missing (machine-directory.md).
QString machine_dirpath_for(const QString& chosen_path);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CLOSE_DECISION_HPP
