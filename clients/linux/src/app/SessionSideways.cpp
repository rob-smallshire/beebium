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

#include "app/SessionSideways.hpp"

#include "connection/RpcWorker.hpp"
#include "connection/SidewaysEventStream.hpp"

#include <QMetaObject>
#include <QPointer>

#include <chrono>

namespace beebium::linux_client {

namespace {

constexpr auto CALL_DEADLINE = std::chrono::seconds(5);

}  // namespace

SessionSideways::SessionSideways(QObject* parent) : QObject(parent) {}

SessionSideways::~SessionSideways() {
    stop();
}

void SessionSideways::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    channel_ = std::move(channel);
    rpc_ = std::make_unique<RpcWorker>();
    if (watching_) {
        read_status();
        open_stream();
    }
}

void SessionSideways::stop() {
    close_stream();
    ++connection_generation_;
    if (rpc_) {
        rpc_->stop();
        rpc_.reset();
    }
    channel_.reset();
    if (loaded_) {
        loaded_ = false;
        topology_ = {};
        Q_EMIT changed();
    }
}

void SessionSideways::set_watching(bool watching) {
    if (watching == watching_) {
        return;
    }
    watching_ = watching;
    if (watching_) {
        // Read afresh: nothing was kept live while no one watched.
        read_status();
        open_stream();
    } else {
        close_stream();
    }
}

void SessionSideways::read_status() {
    if (!rpc_ || !channel_) {
        return;
    }
    const unsigned generation = connection_generation_;
    QPointer<SessionSideways> guard(this);
    rpc_->post([channel = channel_, guard, generation](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        auto stub = beebium::SidewaysService::NewStub(channel);
        beebium::GetSlotStatusResponse response;
        if (!stub->GetSlotStatus(&context, {}, &response).ok() || !guard) {
            return;
        }
        SidewaysTopology topology = sideways_topology(response);
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, topology = std::move(topology)]() mutable {
                if (guard && generation == guard->connection_generation_) {
                    guard->topology_ = std::move(topology);
                    guard->loaded_ = true;
                    Q_EMIT guard->changed();
                }
            },
            Qt::QueuedConnection);
    });
}

void SessionSideways::set_protection(const std::string& group_id, SidewaysProtection kind,
                                     bool engaged) {
    if (!rpc_ || !channel_) {
        return;
    }
    const unsigned generation = connection_generation_;
    QPointer<SessionSideways> guard(this);
    rpc_->post([channel = channel_, guard, generation, group_id, kind,
                engaged](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        auto stub = beebium::SidewaysService::NewStub(channel);
        beebium::SetSlotProtectionRequest request;
        request.set_group_id(group_id);
        request.set_kind(kind == SidewaysProtection::hide
                             ? beebium::SIDEWAYS_PROTECTION_KIND_HIDE
                             : beebium::SIDEWAYS_PROTECTION_KIND_WRITE_PROTECT);
        request.set_engaged(engaged);
        beebium::SetSlotProtectionResponse response;
        const grpc::Status status = stub->SetSlotProtection(&context, request, &response);
        QString refusal;
        if (!status.ok()) {
            refusal = QString::fromStdString(status.error_message());
        } else if (!response.success()) {
            refusal = QString::fromStdString(response.error());
        }
        const bool refused = !status.ok() || !response.success();
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, refused, refusal, group_id] {
                if (!guard || generation != guard->connection_generation_) {
                    return;
                }
                if (refused) {
                    Q_EMIT guard->protection_refused(
                        refusal.isEmpty()
                            ? tr("Could not change protection for %1.")
                                  .arg(QString::fromStdString(group_id))
                            : refusal);
                    return;
                }
                // No event says what the switch did: read it back.
                guard->read_status();
            },
            Qt::QueuedConnection);
    });
}

void SessionSideways::open_stream() {
    if (!channel_ || stream_) {
        return;
    }
    const unsigned generation = ++stream_generation_;
    stream_ = std::make_unique<SidewaysEventStream>(
        channel_, SidewaysEventStream::Callbacks{
                      [this, generation](const beebium::SidewaysEvent& event) {
                          QMetaObject::invokeMethod(
                              this, [this, generation, event] { on_event(generation, event); },
                              Qt::QueuedConnection);
                      },
                      {},
                  });
    stream_->start();
}

void SessionSideways::close_stream() {
    ++stream_generation_;
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
}

void SessionSideways::on_event(unsigned generation, const beebium::SidewaysEvent& event) {
    if (generation != stream_generation_) {
        return;
    }
    if (event.has_slot_header_changed()) {
        const auto& changed_header = event.slot_header_changed();
        if (topology_.apply_header_change(changed_header.slot(), changed_header.rom_header())) {
            Q_EMIT changed();
        }
    } else if (event.has_slot_configured()) {
        read_status();
    }
}

}  // namespace beebium::linux_client
