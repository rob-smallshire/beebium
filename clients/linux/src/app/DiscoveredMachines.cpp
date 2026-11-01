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

#include "app/DiscoveredMachines.hpp"

#include <QHostAddress>
#include <QMetaObject>
#include <QNetworkInterface>
#include <QPointer>
#include <QSysInfo>
#include <QtEndian>

#include <algorithm>

namespace beebium::linux_client {

namespace {

QString txt(const beebium::discovery::DiscoveredService& service, const char* key) {
    const auto found = service.txt_records.find(key);
    return found == service.txt_records.end() ? QString()
                                              : QString::fromStdString(found->second);
}

QString short_hostname(QString name) {
    if (name.endsWith(QLatin1Char('.'))) {
        name.chop(1);
    }
    if (name.endsWith(QLatin1String(".local"), Qt::CaseInsensitive)) {
        name.chop(6);
    }
    return name.toLower();
}

}  // namespace

Endpoint DiscoveredMachine::endpoint() const {
    QString host = address;
    if (host.isEmpty()) {
        host = hostname.endsWith(QLatin1Char('.')) ? hostname.chopped(1) : hostname;
    }
    return Endpoint{host.toStdString(), port};
}

ThisHost this_host() {
    ThisHost host;
    host.hostname = short_hostname(QSysInfo::machineHostName());
    for (const QHostAddress& address : QNetworkInterface::allAddresses()) {
        host.addresses << address.toString();
    }
    return host;
}

DiscoveredMachine machine_from_service(const beebium::discovery::DiscoveredService& service,
                                       const ThisHost& host) {
    DiscoveredMachine machine;
    machine.instance_name = QString::fromStdString(service.instance_name);
    machine.hostname = QString::fromStdString(service.hostname);
    if (service.ipv4_addr_net_byte_order != 0) {
        machine.address = QHostAddress(qFromBigEndian(service.ipv4_addr_net_byte_order)).toString();
    }
    machine.port = service.port;
    machine.uuid = txt(service, "uuid");
    machine.model = txt(service, "model");
    machine.model_name = txt(service, "model_name");
    machine.role = txt(service, "role");
    machine.on_this_host = (!host.hostname.isEmpty() &&
                            short_hostname(machine.hostname) == host.hostname) ||
                           (!machine.address.isEmpty() && host.addresses.contains(machine.address));
    return machine;
}

void sort_for_display(std::vector<DiscoveredMachine>& machines) {
    std::stable_sort(machines.begin(), machines.end(),
                     [](const DiscoveredMachine& a, const DiscoveredMachine& b) {
                         if (a.on_this_host != b.on_this_host) {
                             return a.on_this_host;
                         }
                         return a.instance_name.localeAwareCompare(b.instance_name) < 0;
                     });
}

DiscoveredMachines::DiscoveredMachines(QObject* parent)
    : QObject(parent), browser_(beebium::discovery::create_browser()), this_host_(this_host()) {}

DiscoveredMachines::~DiscoveredMachines() {
    stop();
}

bool DiscoveredMachines::start() {
    // Callbacks come from the browser's own thread: hop to ours. A queued
    // call to an object since destroyed is dropped by Qt.
    QPointer<DiscoveredMachines> self(this);
    beebium::discovery::BrowserCallbacks callbacks;
    callbacks.on_added = [self](const beebium::discovery::DiscoveredService& service) {
        if (self) {
            QMetaObject::invokeMethod(self, [self, service] { if (self) self->added(service); },
                                      Qt::QueuedConnection);
        }
    };
    callbacks.on_removed = [self](const std::string& instance_name) {
        if (self) {
            const QString name = QString::fromStdString(instance_name);
            QMetaObject::invokeMethod(self, [self, name] { if (self) self->removed(name); },
                                      Qt::QueuedConnection);
        }
    };
    available_ = browser_->start("_beebium._tcp", std::move(callbacks));
    return available_;
}

void DiscoveredMachines::stop() {
    if (browser_) {
        browser_->stop();
    }
}

void DiscoveredMachines::added(const beebium::discovery::DiscoveredService& service) {
    DiscoveredMachine machine = machine_from_service(service, this_host_);
    auto existing = std::find_if(machines_.begin(), machines_.end(), [&](const auto& m) {
        return m.instance_name == machine.instance_name;
    });
    if (existing != machines_.end()) {
        *existing = std::move(machine);
    } else {
        machines_.push_back(std::move(machine));
    }
    sort_for_display(machines_);
    emit changed();
}

void DiscoveredMachines::removed(const QString& instance_name) {
    const auto before = machines_.size();
    machines_.erase(std::remove_if(machines_.begin(), machines_.end(),
                                   [&](const auto& m) { return m.instance_name == instance_name; }),
                    machines_.end());
    if (machines_.size() != before) {
        emit changed();
    }
}

}  // namespace beebium::linux_client
