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

#include "app/MachineMove.hpp"

#include "app/CloseDecision.hpp"

#include <QCoreApplication>
#include <QDir>

namespace beebium::linux_client {

QString move_destination(const QString& folder_dirpath, const QString& name) {
    return machine_dirpath_for(QDir(folder_dirpath).filePath(name));
}

QString move_failure_sentence(grpc::StatusCode code, const QString& name) {
    switch (code) {
    case grpc::StatusCode::ALREADY_EXISTS:
        return QCoreApplication::translate("MachineMove",
                                           "A machine named “%1” already exists in that "
                                           "folder.")
            .arg(name);
    case grpc::StatusCode::NOT_FOUND:
        return QCoreApplication::translate("MachineMove",
                                           "The destination folder could not be found.");
    case grpc::StatusCode::INVALID_ARGUMENT:
        return QCoreApplication::translate("MachineMove", "That location can’t be used.");
    default:
        return QCoreApplication::translate("MachineMove", "Couldn’t move the machine.");
    }
}

}  // namespace beebium::linux_client
