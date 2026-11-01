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
#include "connection/Liveness.hpp"
#include "connection/ServerStatusStream.hpp"
#include "connection/VideoStream.hpp"
#include "keyboard/BbcKeyTable.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <optional>
#include <thread>

namespace beebium::linux_client {

// Why a connection attempt did not produce a usable connection, so the
// client can choose a sentence to frame the server's own words (client
// specification 3.4).
enum class ConnectionFailure {
    unreachable,     // nothing answered at the address in time
    incompatible,    // the server speaks a different protocol (4.2)
    error,           // the server answered with an error
};

// One window's connection to one server: the channel, the handshake, and the
// streams. Network work happens on threads of its own; results arrive as
// signals on the GUI thread, which never blocks on the network.
//
// Liveness comes from the status stream alone (4.3). The teardown order is
// keys, status, video, and the channel last (4.5); every disconnect is
// idempotent.
class MachineSession : public QObject {
    Q_OBJECT

public:
    explicit MachineSession(QObject* parent = nullptr);
    ~MachineSession() override;

    FrameMailbox& mailbox() { return mailbox_; }

    // Begin connecting to host:port: the handshake, then the streams.
    void connect_to(const QString& endpoint);

    // Connect again to the same endpoint, as recovery does (4.4).
    void reconnect();

    // Cancel every stream and join every thread. Idempotent.
    void disconnect_from_server();

    QString endpoint() const { return endpoint_; }
    bool connected() const { return connected_; }
    Liveness liveness() const { return liveness_.state(); }

    // The machine's name and description (5.7), from the handshake and then
    // from identity-changed events.
    QString machine_name() const { return machine_name_; }
    QString machine_description() const { return machine_description_; }

    // Whether the server can open paths this client exchanges with it (3.2):
    // the fingerprints matched for this connection; false when disconnected.
    bool server_shares_filesystem() const { return server_shares_filesystem_; }

    // Whether a machine not yet saved has written to its copied media, from
    // every status event (4.5: ask whether to save on close).
    bool template_media_written() const { return template_media_written_; }

    VideoStream* video_stream() { return video_.get(); }

    // Valid from keyboard_ready until disconnect.
    KeySender* key_sender() { return keys_.get(); }
    const BbcKeyTable& key_table() const { return key_table_; }

    // For tests: drive the liveness watchdog from a clock of their own.
    void tick_liveness(LivenessTracker::Clock::time_point now);

signals:
    void connected_to_server();
    void connection_failed(beebium::linux_client::ConnectionFailure failure,
                           const QString& server_message);
    void liveness_changed(beebium::linux_client::Liveness liveness);
    void identity_changed(const QString& name, const QString& description);
    // Reader thread to GUI thread, queued: a frame is waiting.
    void frame_ready();
    // The server's key table has arrived and key events can be sent.
    void keyboard_ready();
    // The guest has asked for input since its last reset, or already had
    // when the connection began: its lock latches are settled (7.3).
    void guest_ready();
    void machine_reset();

private:
    void on_handshake(unsigned generation, const HandshakeResult& result, BbcKeyTable table);
    void on_status_event(unsigned generation, const beebium::ServerStatusEvent& event);
    void on_status_end(unsigned generation, const StreamEnd& end);
    void set_liveness(Liveness before);
    void stop_streams();

    QString endpoint_;
    std::shared_ptr<grpc::Channel> channel_;
    FrameMailbox mailbox_;
    std::unique_ptr<ServerStatusStream> status_;
    std::unique_ptr<VideoStream> video_;
    std::unique_ptr<KeySender> keys_;
    BbcKeyTable key_table_;
    std::thread handshake_thread_;
    // Bumped by every connect and disconnect, so an answer or event queued
    // for a connection since abandoned is recognised and dropped.
    unsigned generation_ = 0;

    bool connected_ = false;
    LivenessTracker liveness_;
    QTimer liveness_ticker_;
    QString machine_name_;
    QString machine_description_;
    bool server_shares_filesystem_ = false;
    bool template_media_written_ = false;
    std::optional<bool> guest_was_ready_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_MACHINE_SESSION_HPP
