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

#ifndef BEEBIUM_LINUX_APP_NETWORK_SIDEBAR_HPP
#define BEEBIUM_LINUX_APP_NETWORK_SIDEBAR_HPP

#include "app/ExtensionViewWidget.hpp"

#include <QHash>
#include <QWidget>

#include <functional>

class QFrame;
class QLabel;
class QLineEdit;
class QToolButton;
class QVBoxLayout;

namespace beebium::linux_client {

class ExtensionPanel;
class SessionEconet;
class SessionExtensions;

// The Network mode (client specification 12.4, 13.1), as the macOS
// client's: Econet's link and station, a station set but not yet adopted
// shown pending with Break to adopt it, an editor bounded by the server,
// the collisions in effect, a banner while a real-time transport is paused
// by the speed, and each transport's own panel, by its opaque id. Nothing
// here knows a transport by name.
class NetworkSidebar : public QWidget {
    Q_OBJECT

public:
    using Shares = ExtensionViewWidget::Shares;
    // Press Break, as the macOS Break button does; and the host key that
    // presses it, for the button and the editor's note.
    using TapBreak = std::function<void()>;
    using BreakKey = std::function<QString()>;

    NetworkSidebar(SessionEconet& econet, SessionExtensions& extensions, Shares shares,
                   TapBreak tap_break, BreakKey break_key, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void present();
    void present_transports();
    void open_editor();
    void save_station();

    SessionEconet& econet_;
    SessionExtensions& extensions_;
    Shares shares_;
    TapBreak tap_break_;
    BreakKey break_key_;

    QLabel* state_title_;
    QLabel* state_detail_;
    QWidget* status_box_;
    QFrame* banner_;
    QLabel* link_dot_;
    QLabel* link_;
    QLabel* in_force_;
    QLabel* pending_;
    QToolButton* break_;
    QToolButton* edit_;
    QFrame* editor_;
    QLineEdit* field_;
    QLabel* error_;
    QLabel* info_;
    QLabel* collision_;
    QLabel* stale_;
    QVBoxLayout* transports_layout_;
    QLabel* transports_unavailable_;
    QHash<QString, ExtensionPanel*> panels_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_NETWORK_SIDEBAR_HPP
