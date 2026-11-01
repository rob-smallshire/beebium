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

#include "app/SystemPowerMonitor.hpp"

#include <QDBusConnection>

#include <cstdio>

namespace beebium::linux_client {

namespace {

// org.freedesktop.login1.Manager.PrepareForSleep(bool start): true before a
// suspend or hibernate, false after resuming. Display blanking does not emit
// it, which is the distinction the specification requires.
class LogindPowerMonitor final : public SystemPowerMonitor {
    Q_OBJECT

public:
    explicit LogindPowerMonitor(QObject* parent) : SystemPowerMonitor(parent) {
        QDBusConnection bus = QDBusConnection::systemBus();
        if (!bus.isConnected() ||
            !bus.connect(QStringLiteral("org.freedesktop.login1"),
                         QStringLiteral("/org/freedesktop/login1"),
                         QStringLiteral("org.freedesktop.login1.Manager"),
                         QStringLiteral("PrepareForSleep"), this,
                         SLOT(on_prepare_for_sleep(bool)))) {
            // Recovery still works on the next failure; only waking early is
            // lost. Said once, since it is a property of the host.
            std::fprintf(stderr,
                         "beebium: no logind on the system bus; waking from sleep will not "
                         "hasten reconnection\n");
        }
    }

private Q_SLOTS:
    void on_prepare_for_sleep(bool start) {
        if (start) {
            Q_EMIT will_sleep();
        } else {
            Q_EMIT did_wake();
        }
    }
};

}  // namespace

std::unique_ptr<SystemPowerMonitor> SystemPowerMonitor::create_for_this_host(QObject* parent) {
    return std::make_unique<LogindPowerMonitor>(parent);
}

}  // namespace beebium::linux_client

#include "SystemPowerMonitor.moc"
