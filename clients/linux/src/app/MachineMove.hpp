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

#ifndef BEEBIUM_LINUX_APP_MACHINE_MOVE_HPP
#define BEEBIUM_LINUX_APP_MACHINE_MOVE_HPP

#include <QString>

#include <grpcpp/support/status.h>

namespace beebium::linux_client {

// Where a saved machine named `name` goes when moved into a folder: the
// folder's <name>.beebium-machine (client specification 5.4).
QString move_destination(const QString& folder_dirpath, const QString& name);

// The sentence a refused move is told in, chosen by the server's status
// code, never its text, which follows it in full (3.4).
QString move_failure_sentence(grpc::StatusCode code, const QString& name);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_MACHINE_MOVE_HPP
