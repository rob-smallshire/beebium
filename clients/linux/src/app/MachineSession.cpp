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

#include "app/MachineSession.hpp"

#include "app/ApplicationInfo.hpp"
#include "app/FilesystemSharing.hpp"

#include "beebium/host_identity/HostFingerprint.hpp"

#include "keyboard.grpc.pb.h"

#include <QMetaObject>

#include <cstdio>

namespace beebium::linux_client {

namespace {

constexpr std::chrono::milliseconds HANDSHAKE_TIMEOUT{5000};
constexpr std::chrono::milliseconds LIVENESS_TICK{250};

// The server's canonical character-to-key table, fetched once per
// connection (client specification 7.4). Mapping entries name BBC keys
// through it too, so without it the keyboard stays inert, and says why.
BbcKeyTable fetch_key_table(const std::shared_ptr<grpc::Channel>& channel) {
    auto keyboard = beebium::KeyboardService::NewStub(channel);
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + HANDSHAKE_TIMEOUT);
    beebium::AllKeyMappingsResponse response;
    const grpc::Status status =
        keyboard->GetAllKeyMappings(&context, beebium::GetAllKeyMappingsRequest{}, &response);
    if (!status.ok()) {
        std::fprintf(stderr, "beebium: no key table from the server: %s\n",
                     status.error_message().c_str());
        return {};
    }
    return BbcKeyTable::from_proto(response);
}

ConnectionFailure classify(const HandshakeResult& result) {
    if (result.reached) {
        return ConnectionFailure::incompatible;
    }
    switch (result.code) {
    case grpc::StatusCode::UNAVAILABLE:
    case grpc::StatusCode::DEADLINE_EXCEEDED:
        return ConnectionFailure::unreachable;
    default:
        return ConnectionFailure::error;
    }
}

}  // namespace

MachineSession::MachineSession(QObject* parent) : QObject(parent) {
    liveness_ticker_.setInterval(LIVENESS_TICK);
    connect(&liveness_ticker_, &QTimer::timeout, this,
            [this] { tick_liveness(LivenessTracker::Clock::now()); });
}

MachineSession::~MachineSession() {
    disconnect_from_server();
}

void MachineSession::connect_to(const QString& endpoint) {
    disconnect_from_server();
    endpoint_ = endpoint;
    liveness_.reset();
    channel_ = grpc::CreateChannel(endpoint.toStdString(), grpc::InsecureChannelCredentials());

    const unsigned generation = ++generation_;
    handshake_thread_ = std::thread([this, generation, channel = channel_] {
        const HandshakeResult result = perform_handshake(
            channel, client_protocol_fingerprint(),
            beebium::host_identity::host_fingerprint(), HANDSHAKE_TIMEOUT);
        BbcKeyTable table;
        if (result.reached && is_compatible(result.protocol)) {
            table = fetch_key_table(channel);
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, result, table = std::move(table)]() mutable {
                on_handshake(generation, result, std::move(table));
            },
            Qt::QueuedConnection);
    });
}

void MachineSession::reconnect() {
    if (!endpoint_.isEmpty()) {
        connect_to(endpoint_);
    }
}

void MachineSession::on_handshake(unsigned generation, const HandshakeResult& result,
                                  BbcKeyTable table) {
    if (generation != generation_) {
        return;
    }
    if (handshake_thread_.joinable()) {
        handshake_thread_.join();
    }
    if (!result.reached) {
        emit connection_failed(classify(result), QString::fromStdString(result.error));
        return;
    }
    if (!is_compatible(result.protocol)) {
        // Refused rather than warned about: a client may choose either, but
        // must not ignore it (client specification 4.2).
        emit connection_failed(ConnectionFailure::incompatible,
                               QString::fromStdString(describe_protocol_mismatch(
                                   result, client_protocol_fingerprint())));
        return;
    }

    connected_ = true;
    machine_name_ = QString::fromStdString(result.machine_name);
    machine_uuid_ = QString::fromStdString(result.machine_uuid);
    machine_description_ = QString::fromStdString(result.machine_description);
    server_shares_filesystem_ =
        shares_filesystem(result.server_is_local, current_sandbox());
    guest_was_ready_.reset();

    // The status stream first: it is the connection's liveness (4.3).
    status_ = std::make_unique<ServerStatusStream>(
        channel_,
        ServerStatusStream::Callbacks{
            [this, generation](const beebium::ServerStatusEvent& event) {
                QMetaObject::invokeMethod(
                    this, [this, generation, event] { on_status_event(generation, event); },
                    Qt::QueuedConnection);
            },
            [this, generation](const StreamEnd& end) {
                QMetaObject::invokeMethod(
                    this, [this, generation, end] { on_status_end(generation, end); },
                    Qt::QueuedConnection);
            },
        });
    status_->start();
    liveness_ticker_.start();

    video_ = std::make_unique<VideoStream>(
        channel_, mailbox_,
        VideoStream::Callbacks{
            [this] {
                QMetaObject::invokeMethod(this, &MachineSession::frame_ready,
                                          Qt::QueuedConnection);
            },
            // A video stream's end is not a liveness signal; the status
            // stream decides (4.3).
            {},
        });
    video_->start();

    emit connected_to_server();
    emit identity_changed(machine_name_, machine_description_);

    if (!table.empty()) {
        key_table_ = std::move(table);
        keys_ = std::make_unique<KeySender>(channel_);
        emit keyboard_ready();
    }
}

void MachineSession::on_status_event(unsigned generation,
                                     const beebium::ServerStatusEvent& event) {
    if (generation != generation_) {
        return;
    }
    const Liveness before = liveness_.state();
    liveness_.on_event(LivenessTracker::Clock::now(),
                       event.status() == beebium::SERVER_STATUS_SHUTTING_DOWN);
    set_liveness(before);

    template_media_written_ = event.template_media_written();

    if (event.status() == beebium::SERVER_STATUS_IDENTITY_CHANGED && event.has_identity()) {
        machine_name_ = QString::fromStdString(event.identity().name());
        machine_uuid_ = QString::fromStdString(event.identity().uuid());
        machine_description_ = QString::fromStdString(event.identity().description());
        emit identity_changed(machine_name_, machine_description_);
    }
    if (event.status() == beebium::SERVER_STATUS_MACHINE_RESET) {
        emit machine_reset();
    }
    // Every event carries the guest's readiness; its rising edge, or a guest
    // already ready when the stream begins, is the sync point (7.3).
    const bool ready = event.guest_ready();
    if (ready && guest_was_ready_ != true) {
        emit guest_ready();
    }
    guest_was_ready_ = ready;
}

void MachineSession::on_status_end(unsigned generation, const StreamEnd& end) {
    if (generation != generation_) {
        return;
    }
    const Liveness before = liveness_.state();
    liveness_.on_stream_end(end.cancelled_by_client);
    liveness_ticker_.stop();
    set_liveness(before);
}

void MachineSession::tick_liveness(LivenessTracker::Clock::time_point now) {
    const Liveness before = liveness_.state();
    liveness_.on_tick(now);
    set_liveness(before);
}

void MachineSession::set_liveness(Liveness before) {
    if (liveness_.state() != before) {
        emit liveness_changed(liveness_.state());
    }
}

void MachineSession::stop_streams() {
    if (keys_) {
        // Drains what is queued first, so a release-all made just before
        // disconnecting still reaches the machine.
        keys_->stop();
        keys_.reset();
    }
    key_table_ = BbcKeyTable();
    if (status_) {
        status_->stop();
        status_.reset();
    }
    if (video_) {
        video_->stop();
        video_.reset();
    }
}

void MachineSession::disconnect_from_server() {
    ++generation_;
    liveness_ticker_.stop();
    stop_streams();
    if (handshake_thread_.joinable()) {
        // Bounded by the handshake's deadlines.
        handshake_thread_.join();
    }
    channel_.reset();
    connected_ = false;
    server_shares_filesystem_ = false;
    template_media_written_ = false;
}

}  // namespace beebium::linux_client
