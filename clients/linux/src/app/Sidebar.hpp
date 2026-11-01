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

#ifndef BEEBIUM_LINUX_APP_SIDEBAR_HPP
#define BEEBIUM_LINUX_APP_SIDEBAR_HPP

#include "app/SidebarMode.hpp"

#include <QHash>
#include <QWidget>

class QButtonGroup;
class QLabel;
class QStackedWidget;
class QToolButton;

namespace beebium::linux_client {

// A machine window's sidebar (client specification 13.1), after the macOS
// client's: a row of mode buttons, then the mode's header, then its content,
// one mode at a time. While the server is not live a mode that shows its
// state carries a banner saying why, over its content dimmed and disabled.
class Sidebar : public QWidget {
    Q_OBJECT

public:
    static constexpr int WIDTH = 280;

    explicit Sidebar(QWidget* parent = nullptr);

    // A mode's content, in the order of 13.1 whatever order they are added.
    void add_mode(SidebarMode mode, QWidget* content);
    bool has_mode(SidebarMode mode) const { return pages_.contains(static_cast<int>(mode)); }

    void set_mode(SidebarMode mode);
    SidebarMode mode() const { return mode_; }

    void set_verdict(ConnectionVerdict verdict);
    bool showing_banner() const;
    bool content_enabled() const;

Q_SIGNALS:
    void mode_changed(beebium::linux_client::SidebarMode mode);

private:
    void present();

    QWidget* buttons_row_;
    QButtonGroup* buttons_;
    QLabel* header_icon_;
    QLabel* header_label_;
    QWidget* banner_;
    QLabel* banner_title_;
    QLabel* banner_detail_;
    QStackedWidget* stack_;
    QHash<int, QWidget*> pages_;
    SidebarMode mode_ = SidebarMode::sound;
    ConnectionVerdict verdict_ = ConnectionVerdict::live;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SIDEBAR_HPP
