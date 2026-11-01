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

#ifndef BEEBIUM_LINUX_APP_MACHINE_SESSION_HPP
#define BEEBIUM_LINUX_APP_MACHINE_SESSION_HPP

#include "connection/FrameMailbox.hpp"
#include "connection/Handshake.hpp"
#include "connection/KeySender.hpp"
#include "keyboard/BbcKeyTable.hpp"
#include "connection/VideoStream.hpp"

#include <QObject>
#include <QString>

#include <memory>
#include <thread>

namespace beebium::linux_client {

// One window's connection to one server: the channel, the handshake, and the
// streams. Network work happens on threads of its own; results arrive as
// signals on the GUI thread, which never blocks on the network.
class MachineSession : public QObject {
    Q_OBJECT

public:
    explicit MachineSession(QObject* parent = nullptr);
    ~MachineSession() override;

    FrameMailbox& mailbox() { return mailbox_; }

    // Begin connecting to host:port. Reports handshake_finished, then, when
    // compatible, opens the frame stream.
    void connect_to(const QString& endpoint);

    // Cancel every stream and join every thread. Idempotent.
    void disconnect_from_server();

    VideoStream* video_stream() { return video_.get(); }

    // Valid from keyboard_ready until disconnect.
    KeySender* key_sender() { return keys_.get(); }
    const BbcKeyTable& key_table() const { return key_table_; }

signals:
    void handshake_finished(bool reached, bool compatible, const QString& message);
    // Reader thread to GUI thread, queued: a frame is waiting.
    void frame_ready();
    // The server's key table has arrived and key events can be sent.
    void keyboard_ready();
    void video_stream_ended(int code, const QString& message, bool cancelled_by_client);

private:
    void on_handshake(unsigned generation, const HandshakeResult& result, BbcKeyTable table);

    std::shared_ptr<grpc::Channel> channel_;
    FrameMailbox mailbox_;
    std::unique_ptr<VideoStream> video_;
    std::unique_ptr<KeySender> keys_;
    BbcKeyTable key_table_;
    std::thread handshake_thread_;
    // Bumped by every connect and disconnect, so a handshake answer queued
    // for a connection since abandoned is recognised and dropped.
    unsigned generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_MACHINE_SESSION_HPP
