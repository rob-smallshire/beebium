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

#ifndef BEEBIUM_LINUX_APP_CONNECTION_RECOVERY_HPP
#define BEEBIUM_LINUX_APP_CONNECTION_RECOVERY_HPP

#include "connection/ReconnectPolicy.hpp"

#include <QObject>
#include <QTimer>

namespace beebium::linux_client {

class MachineSession;
class SystemPowerMonitor;

// Drives the ReconnectPolicy for one machine window: the timer, the session's
// reconnect, and the window's one connection verdict (client specification
// 4.3, 4.4). Construct it before anything else connects to the session's
// signals, so a failed attempt is counted before others see it.
class ConnectionRecovery : public QObject {
    Q_OBJECT

public:
    ConnectionRecovery(MachineSession& session, SystemPowerMonitor* power, QObject* parent = nullptr);

    ConnectionVerdict verdict() const;
    ReconnectPolicy::Phase phase() const { return policy_.phase(); }
    bool recovering() const { return policy_.phase() != ReconnectPolicy::Phase::idle; }
    int attempt() const { return policy_.attempt(); }

    // The user asked to try again after recovery gave up.
    void retry();

    // The window is closing: cancel recovery before teardown (4.4).
    void window_closing();

signals:
    void verdict_changed(beebium::linux_client::ConnectionVerdict verdict, int attempt);

private:
    void apply(ReconnectPolicy::Step step);
    void publish();

    MachineSession& session_;
    ReconnectPolicy policy_;
    QTimer timer_;
    ConnectionVerdict published_ = ConnectionVerdict::live;
    int published_attempt_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONNECTION_RECOVERY_HPP
