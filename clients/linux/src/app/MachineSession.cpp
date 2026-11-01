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

#include "beebium/host_identity/HostFingerprint.hpp"

#include "keyboard.grpc.pb.h"

#include <QMetaObject>

#include <cstdio>

namespace beebium::linux_client {

namespace {

constexpr std::chrono::milliseconds HANDSHAKE_TIMEOUT{5000};

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

}  // namespace

MachineSession::MachineSession(QObject* parent) : QObject(parent) {}

MachineSession::~MachineSession() {
    disconnect_from_server();
}

void MachineSession::connect_to(const QString& endpoint) {
    disconnect_from_server();
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

void MachineSession::on_handshake(unsigned generation, const HandshakeResult& result,
                                  BbcKeyTable table) {
    if (generation != generation_) {
        return;
    }
    if (handshake_thread_.joinable()) {
        handshake_thread_.join();
    }
    if (!result.reached) {
        emit handshake_finished(false, false, QString::fromStdString(result.error));
        return;
    }
    if (!is_compatible(result.protocol)) {
        // Refused rather than warned about: a client may choose either, but
        // must not ignore it (client specification 4.2).
        emit handshake_finished(true, false,
                                QString::fromStdString(describe_protocol_mismatch(
                                    result, client_protocol_fingerprint())));
        return;
    }
    emit handshake_finished(true, true,
                            QStringLiteral("%1 x %2 at %3 Hz")
                                .arg(result.video_width)
                                .arg(result.video_height)
                                .arg(result.video_framerate_hz));

    video_ = std::make_unique<VideoStream>(
        channel_, mailbox_,
        VideoStream::Callbacks{
            [this] { QMetaObject::invokeMethod(this, &MachineSession::frame_ready,
                                               Qt::QueuedConnection); },
            [this](const StreamEnd& end) {
                QMetaObject::invokeMethod(
                    this,
                    [this, end] {
                        emit video_stream_ended(static_cast<int>(end.code),
                                                QString::fromStdString(end.message),
                                                end.cancelled_by_client);
                    },
                    Qt::QueuedConnection);
            },
        });
    video_->start();

    if (!table.empty()) {
        key_table_ = std::move(table);
        keys_ = std::make_unique<KeySender>(channel_);
        emit keyboard_ready();

        guest_ready_watcher_ = std::make_unique<GuestReadyWatcher>(channel_, [this] {
            QMetaObject::invokeMethod(this, &MachineSession::guest_ready, Qt::QueuedConnection);
        });
        guest_ready_watcher_->start();
    }
}

void MachineSession::disconnect_from_server() {
    ++generation_;
    if (guest_ready_watcher_) {
        guest_ready_watcher_->stop();
        guest_ready_watcher_.reset();
    }
    if (keys_) {
        // Drains what is queued first, so a release-all made just before
        // disconnecting still reaches the machine.
        keys_->stop();
        keys_.reset();
    }
    key_table_ = BbcKeyTable();
    if (video_) {
        video_->stop();
        video_.reset();
    }
    if (handshake_thread_.joinable()) {
        // Bounded by the handshake's deadlines.
        handshake_thread_.join();
    }
    channel_.reset();
}

}  // namespace beebium::linux_client
