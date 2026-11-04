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

#include "app/SessionEconet.hpp"

#include "connection/EconetStatusStream.hpp"
#include "connection/RpcWorker.hpp"

#include "econet_transport.grpc.pb.h"

#include <QMetaObject>
#include <QPointer>

#include <chrono>

namespace beebium::linux_client {

namespace {

constexpr auto CALL_DEADLINE = std::chrono::seconds(5);

}  // namespace

SessionEconet::SessionEconet(QObject* parent) : QObject(parent) {}

SessionEconet::~SessionEconet() {
    stop();
}

void SessionEconet::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    channel_ = std::move(channel);
    rpc_ = std::make_unique<RpcWorker>();
    inputs_.server_live = true;
    inputs_.stream_lost = false;
    read_status();
    list_transports();
    if (watching_) {
        open_stream();
    }
    Q_EMIT changed();
}

void SessionEconet::stop() {
    ++generation_;
    close_stream();
    if (rpc_) {
        rpc_->stop();
        rpc_.reset();
    }
    if (channel_) {
        channel_.reset();
        // The last status stays, no longer the server's present state.
        inputs_.server_live = false;
        Q_EMIT changed();
    }
}

void SessionEconet::read_status() {
    const unsigned generation = generation_;
    QPointer<SessionEconet> guard(this);
    rpc_->post([channel = channel_, guard, generation](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        auto stub = beebium::EconetService::NewStub(channel);
        beebium::GetEconetStatusResponse status;
        if (!stub->GetEconetStatus(&context, {}, &status).ok() || !guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, status] {
                // The stream's snapshot, when there is one, is the newer.
                if (guard && generation == guard->generation_ &&
                    guard->streamed_in_ != generation) {
                    guard->inputs_.status = status;
                    Q_EMIT guard->changed();
                }
            },
            Qt::QueuedConnection);
    });
}

void SessionEconet::list_transports() {
    const unsigned generation = generation_;
    QPointer<SessionEconet> guard(this);
    rpc_->post([channel = channel_, guard, generation](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        auto stub = beebium::EconetTransportService::NewStub(channel);
        beebium::ListTransportsResponse response;
        const bool ok = stub->ListTransports(&context, {}, &response).ok();
        if (!guard) {
            return;
        }
        std::vector<beebium::EconetTransport> transports(response.transports().begin(),
                                                         response.transports().end());
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, ok, transports = std::move(transports)]() mutable {
                if (!guard || generation != guard->generation_) {
                    return;
                }
                guard->transports_ = std::move(transports);
                guard->transports_loaded_ = ok;
                guard->transports_failed_ = !ok;
                Q_EMIT guard->changed();
            },
            Qt::QueuedConnection);
    });
}

void SessionEconet::set_station(std::uint32_t station) {
    if (!rpc_) {
        return;
    }
    const unsigned generation = generation_;
    QPointer<SessionEconet> guard(this);
    rpc_->post([channel = channel_, guard, generation, station](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        auto stub = beebium::EconetService::NewStub(channel);
        beebium::SetStationIdRequest request;
        request.set_station_id(station);
        beebium::SetStationIdResponse response;
        const grpc::Status status = stub->SetStationId(&context, request, &response);
        if (status.ok() && response.success()) {
            return;  // the stream shows the new configuration
        }
        const QString error = QString::fromStdString(status.ok() ? response.error()
                                                                 : status.error_message());
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, error] {
                if (guard && generation == guard->generation_) {
                    Q_EMIT guard->station_refused(error);
                }
            },
            Qt::QueuedConnection);
    });
}

void SessionEconet::set_watching(bool watching) {
    if (watching == watching_) {
        return;
    }
    watching_ = watching;
    if (watching_) {
        open_stream();
    } else {
        close_stream();
    }
}

void SessionEconet::open_stream() {
    if (!channel_ || stream_) {
        return;
    }
    const unsigned generation = ++stream_generation_;
    stream_ = std::make_unique<EconetStatusStream>(
        channel_,
        EconetStatusStream::Callbacks{
            [this, generation](const beebium::GetEconetStatusResponse& status) {
                QMetaObject::invokeMethod(
                    this,
                    [this, generation, status] {
                        if (generation != stream_generation_) {
                            return;
                        }
                        inputs_.status = status;
                        inputs_.stream_lost = false;
                        streamed_in_ = generation_;
                        Q_EMIT changed();
                    },
                    Qt::QueuedConnection);
            },
            [this, generation](const StreamEnd& end) {
                if (end.cancelled_by_client) {
                    return;
                }
                QMetaObject::invokeMethod(
                    this,
                    [this, generation] {
                        if (generation != stream_generation_) {
                            return;
                        }
                        // Lost with the server still there: the last status
                        // stays, not current, and the gauges go.
                        inputs_.stream_lost = true;
                        stream_.reset();
                        Q_EMIT changed();
                    },
                    Qt::QueuedConnection);
            },
        });
    stream_->start();
}

void SessionEconet::close_stream() {
    ++stream_generation_;
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
}

}  // namespace beebium::linux_client
