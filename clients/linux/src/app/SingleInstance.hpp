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

#ifndef BEEBIUM_LINUX_APP_SINGLE_INSTANCE_HPP
#define BEEBIUM_LINUX_APP_SINGLE_INSTANCE_HPP

#include <QObject>
#include <QStringList>

class QLocalServer;

namespace beebium::linux_client {

// One client process per user session: a later launch, a desktop launcher or
// a beebium:// link hands its arguments to the running one and exits, so a
// connect link to a machine already open activates its window instead of
// starting a second client (client specification 4.1).
class SingleInstance : public QObject {
    Q_OBJECT

public:
    // The session's socket: $XDG_RUNTIME_DIR/beebium-client.
    SingleInstance();
    // A socket at a given path, for tests.
    explicit SingleInstance(QString socket_filepath);

    // Become the running instance, or, if there already is one, give it
    // `arguments` and return false: the caller then exits.
    bool claim(const QStringList& arguments);

signals:
    // Another launch's arguments, on the running instance.
    void arguments_received(const QStringList& arguments);

private:
    QString socket_filepath_;
    QLocalServer* server_ = nullptr;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SINGLE_INSTANCE_HPP
