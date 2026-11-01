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

#ifndef BEEBIUM_LINUX_APP_RUNNING_MACHINES_PAGE_HPP
#define BEEBIUM_LINUX_APP_RUNNING_MACHINES_PAGE_HPP

#include "connection/Endpoint.hpp"

#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace beebium::linux_client {

class DiscoveredMachines;
struct DiscoveredMachine;

// The home window's running machines (client specification 4.1, 13.4):
// those discovered on this host apart from the network's, or an address to
// type (default localhost:48875, recent connections offered), and Connect.
class RunningMachinesPage : public QWidget {
    Q_OBJECT

public:
    // Whether a machine is already open in a window, to mark it so.
    using IsOpen = std::function<bool(const DiscoveredMachine&)>;

    RunningMachinesPage(DiscoveredMachines& machines, IsOpen is_open, QWidget* parent = nullptr);

    // The recent connections the address box offers, most recent first.
    void set_recents(const std::vector<Endpoint>& recents);
    // Mark again which machines are open.
    void refresh();

    // What Connect would connect to.
    std::optional<Endpoint> endpoint() const;
    // The chosen machine's uuid, when it was picked from the list.
    QString machine_uuid() const;

    QComboBox* address_box() const { return address_; }
    QPushButton* connect_button() const { return connect_; }
    QTreeWidget* list() const { return list_; }

Q_SIGNALS:
    void connect_requested(const beebium::linux_client::Endpoint& endpoint,
                           const QString& machine_uuid);

private:
    void rebuild();
    void update_connect();
    void choose(QTreeWidgetItem* item);
    void request_connect();

    DiscoveredMachines& machines_;
    IsOpen is_open_;
    QTreeWidget* list_;
    QLabel* list_note_;
    QComboBox* address_;
    QPushButton* connect_;
    QString picked_uuid_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_RUNNING_MACHINES_PAGE_HPP
