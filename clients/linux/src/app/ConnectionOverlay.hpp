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

#ifndef BEEBIUM_LINUX_APP_CONNECTION_OVERLAY_HPP
#define BEEBIUM_LINUX_APP_CONNECTION_OVERLAY_HPP

#include "connection/ReconnectPolicy.hpp"

#include <QString>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;

namespace beebium::linux_client {

// What the overlay over the frozen picture says for a connection verdict
// (client specification 4.3, 4.4): nothing while live, a spinner while
// waiting or reconnecting, and a terminal statement with Close (and Retry
// when recovery gave up) otherwise. `machine` is the server's name for the
// machine, else its address.
struct OverlayContent {
    bool visible = false;
    QString title;
    QString detail;
    bool spinner = false;
    bool retry = false;
    bool close = false;
};

OverlayContent overlay_content(ConnectionVerdict verdict, const QString& machine, int attempt);

// The overlay itself: a panel centred over the display, the picture's last
// frame left visible around it.
class ConnectionOverlay : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionOverlay(QWidget* parent);

    void show_content(const OverlayContent& content);

Q_SIGNALS:
    void retry_requested();
    void close_requested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* title_;
    QLabel* detail_;
    QProgressBar* spinner_;
    QPushButton* retry_;
    QPushButton* close_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONNECTION_OVERLAY_HPP
