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

#include "app/CloseDecision.hpp"

namespace beebium::linux_client {

CloseAction close_action(bool launched_by_this_client, int client_count) {
    if (!launched_by_this_client) {
        return CloseAction::disconnect;
    }
    return client_count > 1 ? CloseAction::ask : CloseAction::shut_down;
}

CloseAction quit_action(bool launched_by_this_client, int client_count) {
    const CloseAction action = close_action(launched_by_this_client, client_count);
    return action == CloseAction::ask ? CloseAction::disconnect : action;
}

QString machine_dirpath_for(const QString& chosen_path) {
    static const QString SUFFIX = QStringLiteral(".beebium-machine");
    QString path = chosen_path;
    while (path.endsWith(QLatin1Char('/')) && path.size() > 1) {
        path.chop(1);
    }
    return path.endsWith(SUFFIX) ? path : path + SUFFIX;
}

}  // namespace beebium::linux_client
