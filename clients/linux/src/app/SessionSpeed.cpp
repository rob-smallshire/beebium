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

#include "app/SessionSpeed.hpp"

#include "connection/PacingStatsStream.hpp"
#include "connection/RpcWorker.hpp"

#include <QMetaObject>

#include <chrono>

namespace beebium::linux_client {

namespace {

constexpr auto CALL_DEADLINE = std::chrono::seconds(5);

}  // namespace

SessionSpeed::SessionSpeed(QObject* parent) : QObject(parent) {}

SessionSpeed::~SessionSpeed() {
    stop();
}

void SessionSpeed::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    channel_ = std::move(channel);
    rpc_ = std::make_unique<RpcWorker>();
    if (watching_) {
        open_stream();
    }
}

void SessionSpeed::stop() {
    close_stream();
    if (rpc_) {
        rpc_->stop();
        rpc_.reset();
    }
    channel_.reset();
}

void SessionSpeed::set_watching(bool watching) {
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

void SessionSpeed::set_multiplier(double multiplier) {
    if (!rpc_ || !channel_) {
        return;
    }
    rpc_->post([channel = channel_, multiplier](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        auto stub = beebium::SystemService::NewStub(channel);
        beebium::SetSpeedMultiplierRequest request;
        request.set_speed_multiplier(multiplier);
        beebium::SetSpeedMultiplierResponse response;
        // The pacing statistics show what the server made of it.
        stub->SetSpeedMultiplier(&context, request, &response);
    });
}

void SessionSpeed::open_stream() {
    if (!channel_ || stream_) {
        return;
    }
    const unsigned generation = ++generation_;
    stream_ = std::make_unique<PacingStatsStream>(
        channel_, INTERVAL_MS,
        PacingStatsStream::Callbacks{
            [this, generation](const beebium::PacingStats& stats) {
                const PacingSample sample{stats.speed_multiplier(),
                                          stats.achieved_speed_multiplier(),
                                          stats.estimated_max_speed_multiplier()};
                QMetaObject::invokeMethod(
                    this, [this, generation, sample] { on_stats(generation, sample); },
                    Qt::QueuedConnection);
            },
            {},
        });
    stream_->start();
}

void SessionSpeed::close_stream() {
    ++generation_;
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
}

void SessionSpeed::on_stats(unsigned generation, const PacingSample& sample) {
    if (generation != generation_) {
        return;
    }
    Q_EMIT sampled(sample);
}

}  // namespace beebium::linux_client
