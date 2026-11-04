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

#ifndef BEEBIUM_LINUX_APP_PERIPHERALS_SIDEBAR_HPP
#define BEEBIUM_LINUX_APP_PERIPHERALS_SIDEBAR_HPP

#include "app/ExtensionViewWidget.hpp"
#include "connection/PeripheralTree.hpp"

#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace beebium::linux_client {

class SessionExtensions;
class SessionPeripherals;

// The Peripherals mode (client specification 12.3, 12.4, 13.1), as the
// macOS client's: the machine's peripherals under the points they attach
// to, in the server's order, each nested under the provider the server
// chose, a device on several points shown once with the others as "also:"
// badges. A node with a UI embeds its panel, subscribed only while the
// mode shows. The serial port's section carries the on-board serial
// interface's line. No extension is known by name.
class PeripheralsSidebar : public QWidget {
    Q_OBJECT

public:
    PeripheralsSidebar(SessionPeripherals& peripherals, SessionExtensions& extensions,
                       ExtensionViewWidget::Shares shares, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void rebuild();
    void add_node(const PeripheralNode& node, int depth, QVBoxLayout* into);
    void show_serial();

    SessionPeripherals& peripherals_;
    SessionExtensions& extensions_;
    ExtensionViewWidget::Shares shares_;
    QWidget* content_;
    QVBoxLayout* layout_;
    QLabel* serial_line_ = nullptr;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_PERIPHERALS_SIDEBAR_HPP
