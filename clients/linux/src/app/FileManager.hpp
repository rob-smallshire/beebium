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

#ifndef BEEBIUM_LINUX_APP_FILE_MANAGER_HPP
#define BEEBIUM_LINUX_APP_FILE_MANAGER_HPP

#include <QString>

namespace beebium::linux_client {

// Show a file or directory in the desktop's file manager, selected in its
// folder, through the freedesktop FileManager1 interface; without one, the
// folder is opened.
void show_in_file_manager(const QString& path);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_FILE_MANAGER_HPP
