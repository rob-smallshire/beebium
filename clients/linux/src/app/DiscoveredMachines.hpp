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

#ifndef BEEBIUM_LINUX_APP_DISCOVERED_MACHINES_HPP
#define BEEBIUM_LINUX_APP_DISCOVERED_MACHINES_HPP

#include "connection/Endpoint.hpp"

#include <beebium/discovery/Browser.hpp>

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace beebium::linux_client {

// A machine advertising itself on _beebium._tcp (client specification 4.1).
struct DiscoveredMachine {
    QString instance_name;  // the machine's name as announced; stable key
    QString hostname;       // e.g. "host-a.local."
    QString address;        // IPv4, dotted; empty if only IPv6 was announced
    std::uint16_t port = 0;
    // From the TXT record.
    QString uuid;
    QString model;
    QString model_name;
    QString role;
    bool on_this_host = false;

    // Where to dial: the IPv4 address when known (no mDNS lookup needed by
    // the resolver), else the host name.
    Endpoint endpoint() const;
};

// What identifies this host, for telling local machines from others.
struct ThisHost {
    QString hostname;          // the short name, without .local
    QStringList addresses;     // this host's own addresses
};

ThisHost this_host();

DiscoveredMachine machine_from_service(const beebium::discovery::DiscoveredService& service,
                                       const ThisHost& host);

// This host's machines first, then the network's, each by name.
void sort_for_display(std::vector<DiscoveredMachine>& machines);

// The live list of discovered machines, browsed with the shared discovery
// library (Avahi on Linux). Changes arrive on the GUI thread.
class DiscoveredMachines : public QObject {
    Q_OBJECT

public:
    explicit DiscoveredMachines(QObject* parent = nullptr);
    ~DiscoveredMachines() override;

    // Begin browsing; false when mDNS is unavailable here.
    bool start();
    void stop();

    bool available() const { return available_; }
    const std::vector<DiscoveredMachine>& machines() const { return machines_; }

Q_SIGNALS:
    void changed();

private:
    void added(const beebium::discovery::DiscoveredService& service);
    void removed(const QString& instance_name);

    std::unique_ptr<beebium::discovery::Browser> browser_;
    ThisHost this_host_;
    std::vector<DiscoveredMachine> machines_;
    bool available_ = false;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_DISCOVERED_MACHINES_HPP
