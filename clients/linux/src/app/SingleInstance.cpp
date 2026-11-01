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

#include "app/SingleInstance.hpp"

#include <QDataStream>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

namespace beebium::linux_client {

namespace {

constexpr int TIMEOUT_MS = 1000;

QString session_socket_filepath() {
    QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (directory.isEmpty()) {
        directory = QDir::tempPath();
    }
    return QDir(directory).filePath(QStringLiteral("beebium-client"));
}

}  // namespace

SingleInstance::SingleInstance() : SingleInstance(session_socket_filepath()) {}

SingleInstance::SingleInstance(QString socket_filepath)
    : socket_filepath_(std::move(socket_filepath)) {}

bool SingleInstance::claim(const QStringList& arguments) {
    {
        QLocalSocket socket;
        socket.connectToServer(socket_filepath_);
        if (socket.waitForConnected(TIMEOUT_MS)) {
            QByteArray message;
            QDataStream stream(&message, QIODevice::WriteOnly);
            stream << arguments;
            socket.write(message);
            socket.waitForBytesWritten(TIMEOUT_MS);
            socket.disconnectFromServer();
            return false;
        }
    }
    // No one answered: a socket left by a client that crashed is stale.
    QLocalServer::removeServer(socket_filepath_);
    server_ = new QLocalServer(this);
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_->listen(socket_filepath_)) {
        // Still the running instance, just not reachable by later launches.
        return true;
    }
    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* socket = server_->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                QDataStream stream(socket);
                stream.startTransaction();
                QStringList arguments;
                stream >> arguments;
                if (stream.commitTransaction()) {
                    Q_EMIT arguments_received(arguments);
                }
            });
        }
    });
    return true;
}

}  // namespace beebium::linux_client
