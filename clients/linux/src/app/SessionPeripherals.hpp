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

#ifndef BEEBIUM_LINUX_APP_SESSION_PERIPHERALS_HPP
#define BEEBIUM_LINUX_APP_SESSION_PERIPHERALS_HPP

#include "connection/PeripheralTree.hpp"

#include <QObject>
#include <QString>

#include <memory>

namespace grpc {
class Channel;
}  // namespace grpc

namespace beebium::linux_client {

class RpcWorker;
class SerialStatusStream;

// A machine window's peripherals (client specification 12.3, 12.4): the
// set the server lists once on connect, fixed for the server's life, and
// the on-board serial interface's state while something watches it. Set
// up and torn down with the connection; the last tree stays when the
// server is lost.
class SessionPeripherals : public QObject {
    Q_OBJECT

public:
    explicit SessionPeripherals(QObject* parent = nullptr);
    ~SessionPeripherals() override;

    void start(std::shared_ptr<grpc::Channel> channel);
    void stop();

    bool loaded() const { return loaded_; }
    // Why the listing failed, when it did.
    const QString& error() const { return error_; }
    const PeripheralTree& tree() const { return tree_; }

    // The serial interface's line, kept across reconnects.
    void set_watching_serial(bool watching);
    bool watching_serial() const { return watching_serial_; }
    const SerialLine& serial() const { return serial_; }

Q_SIGNALS:
    void changed();
    void serial_changed();

private:
    void list();
    void open_serial();
    void close_serial();

    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<RpcWorker> rpc_;
    std::unique_ptr<SerialStatusStream> serial_stream_;
    PeripheralTree tree_;
    SerialLine serial_;
    QString error_;
    bool loaded_ = false;
    bool watching_serial_ = false;
    unsigned generation_ = 0;
    unsigned serial_generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_PERIPHERALS_HPP
