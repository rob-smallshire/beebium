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

#include <QMetaObject>

namespace beebium::linux_client {

namespace {

constexpr std::chrono::milliseconds HANDSHAKE_TIMEOUT{5000};

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
        QMetaObject::invokeMethod(
            this, [this, generation, result] { on_handshake(generation, result); },
            Qt::QueuedConnection);
    });
}

void MachineSession::on_handshake(unsigned generation, const HandshakeResult& result) {
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
}

void MachineSession::disconnect_from_server() {
    ++generation_;
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
