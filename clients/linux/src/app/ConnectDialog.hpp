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

#ifndef BEEBIUM_LINUX_APP_CONNECT_DIALOG_HPP
#define BEEBIUM_LINUX_APP_CONNECT_DIALOG_HPP

#include "connection/Endpoint.hpp"

#include <QDialog>

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

// Connect a window to a machine (client specification 4.1): pick one of the
// discovered machines, this host's apart from the network's, or type an
// address (default localhost:48875, recent connections offered).
class ConnectDialog : public QDialog {
    Q_OBJECT

public:
    // Whether a machine is already open in a window, to mark it so.
    using IsOpen = std::function<bool(const DiscoveredMachine&)>;

    ConnectDialog(DiscoveredMachines& machines, const std::vector<Endpoint>& recents,
                  IsOpen is_open, QWidget* parent = nullptr);

    // What was chosen, once accepted.
    std::optional<Endpoint> endpoint() const;
    // The chosen machine's uuid, when it was picked from the list.
    QString machine_uuid() const;

    // The typed address, for tests.
    QComboBox* address_box() const { return address_; }
    QPushButton* connect_button() const { return connect_; }

private:
    void rebuild();
    void update_connect();
    void choose(QTreeWidgetItem* item);

    DiscoveredMachines& machines_;
    IsOpen is_open_;
    QTreeWidget* list_;
    QLabel* list_note_;
    QComboBox* address_;
    QPushButton* connect_;
    QString picked_uuid_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONNECT_DIALOG_HPP
