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

#ifndef BEEBIUM_LINUX_APP_SYSTEM_POWER_MONITOR_HPP
#define BEEBIUM_LINUX_APP_SYSTEM_POWER_MONITOR_HPP

#include <QObject>

#include <memory>

namespace beebium::linux_client {

// Full system sleep and wake, never display sleep (client specification 4.4;
// frontend-sleep-wake.md). Recovery depends on this interface, not on an OS
// API, so it can be driven by tests.
class SystemPowerMonitor : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // logind's PrepareForSleep on the system bus; a monitor that never fires
    // when there is no system bus or no logind.
    static std::unique_ptr<SystemPowerMonitor> create_for_this_host(QObject* parent = nullptr);

Q_SIGNALS:
    // A full sleep is imminent.
    void will_sleep();
    // Woke from full sleep: a live connection is very likely dead.
    void did_wake();
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SYSTEM_POWER_MONITOR_HPP
