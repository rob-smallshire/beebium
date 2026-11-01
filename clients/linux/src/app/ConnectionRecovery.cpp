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

#include "app/ConnectionRecovery.hpp"

#include "app/MachineSession.hpp"
#include "app/SystemPowerMonitor.hpp"

namespace beebium::linux_client {

ConnectionRecovery::ConnectionRecovery(MachineSession& session, SystemPowerMonitor* power,
                                       QObject* parent)
    : QObject(parent), session_(session) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] { apply(policy_.on_timer()); });

    connect(&session_, &MachineSession::liveness_changed, this, [this](Liveness liveness) {
        if (liveness == Liveness::died) {
            apply(policy_.on_connection_lost());
        } else if (liveness == Liveness::stopped) {
            timer_.stop();
            policy_.on_graceful_stop();
        }
        publish();
    });
    connect(&session_, &MachineSession::connected_to_server, this, [this] {
        policy_.on_connected();
        publish();
    });
    connect(&session_, &MachineSession::connection_failed, this,
            [this](ConnectionFailure, const QString&) { apply(policy_.on_attempt_failed()); });

    if (power) {
        connect(power, &SystemPowerMonitor::will_sleep, this, [this] {
            timer_.stop();
            policy_.on_will_sleep();
            publish();
        });
        connect(power, &SystemPowerMonitor::did_wake, this, [this] {
            const bool connected =
                session_.connected() && session_.liveness() != Liveness::stopped;
            apply(policy_.on_wake(connected || recovering()));
        });
    }
}

ConnectionVerdict ConnectionRecovery::verdict() const {
    return connection_verdict(session_.liveness(), policy_.phase());
}

void ConnectionRecovery::retry() {
    apply(policy_.on_retry());
}

void ConnectionRecovery::window_closing() {
    timer_.stop();
    policy_.on_window_closing();
}

void ConnectionRecovery::apply(ReconnectPolicy::Step step) {
    using Kind = ReconnectPolicy::Step::Kind;
    switch (step.kind) {
    case Kind::none:
        break;
    case Kind::attempt_after:
        timer_.start(static_cast<int>(step.delay.count()));
        break;
    case Kind::attempt_now:
        timer_.stop();
        // One reconnect rebuilds the whole connection (4.4).
        session_.reconnect();
        break;
    }
    publish();
}

void ConnectionRecovery::publish() {
    const ConnectionVerdict now = verdict();
    if (now != published_ || policy_.attempt() != published_attempt_) {
        published_ = now;
        published_attempt_ = policy_.attempt();
        Q_EMIT verdict_changed(now, published_attempt_);
    }
}

}  // namespace beebium::linux_client
