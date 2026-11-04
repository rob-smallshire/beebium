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

#ifndef BEEBIUM_LINUX_APP_FILE_REVEAL_HPP
#define BEEBIUM_LINUX_APP_FILE_REVEAL_HPP

#include <QString>
#include <QUrl>

#include <functional>

namespace beebium::linux_client {

// The two ways to show a file on the desktop: ask the file manager to show
// it selected (the freedesktop org.freedesktop.FileManager1 ShowItems call,
// which Nautilus, Dolphin, Nemo, Thunar and PCManFM-Qt answer), or open its
// folder. Injectable, for tests.
struct RevealRoutes {
    // ShowItems for one file URI; false when no file manager answered.
    std::function<bool(const QString& file_uri)> show_items;
    std::function<void(const QUrl& folder)> open_folder;
};

RevealRoutes desktop_reveal_routes();

enum class Revealed { selected_in_file_manager, folder_opened };

// Show a file or directory selected in its folder; failing that, open the
// folder.
Revealed reveal_file(const QString& path, const RevealRoutes& routes = desktop_reveal_routes());

// Why a path the server reported cannot be revealed here, or empty when it
// can: the server must share this filesystem, and the path must exist here
// (client specification 3.2, 11). A remote server with a similar layout
// could otherwise reveal a different file of the same name.
QString reveal_refusal(const QString& path, bool server_shares_filesystem);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_FILE_REVEAL_HPP
