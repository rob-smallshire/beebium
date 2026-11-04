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

#include "app/FileReveal.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDesktopServices>
#include <QFileInfo>
#include <QStringList>

namespace beebium::linux_client {

RevealRoutes desktop_reveal_routes() {
    return RevealRoutes{
        [](const QString& file_uri) {
            QDBusMessage call = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.FileManager1"),
                QStringLiteral("/org/freedesktop/FileManager1"),
                QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
            call << QStringList{file_uri} << QString();
            const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 2000);
            return reply.type() == QDBusMessage::ReplyMessage;
        },
        [](const QUrl& folder) { QDesktopServices::openUrl(folder); },
    };
}

Revealed reveal_file(const QString& path, const RevealRoutes& routes) {
    if (routes.show_items && routes.show_items(QUrl::fromLocalFile(path).toString())) {
        return Revealed::selected_in_file_manager;
    }
    if (routes.open_folder) {
        routes.open_folder(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    }
    return Revealed::folder_opened;
}

QString reveal_refusal(const QString& path, bool server_shares_filesystem) {
    if (!server_shares_filesystem) {
        return QCoreApplication::translate("FileReveal", "The server is on another host");
    }
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return QCoreApplication::translate("FileReveal", "The file is not on this computer");
    }
    return {};
}

}  // namespace beebium::linux_client
