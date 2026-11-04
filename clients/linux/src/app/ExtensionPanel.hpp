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

#ifndef BEEBIUM_LINUX_APP_EXTENSION_PANEL_HPP
#define BEEBIUM_LINUX_APP_EXTENSION_PANEL_HPP

#include "app/ExtensionViewWidget.hpp"

#include <QWidget>

#include <string>

namespace beebium::linux_client {

class ExtensionViewFeed;
class SessionExtensions;

// One extension's panel (client specification 12.1, 13.1): its view,
// subscribed while the panel shows and released when it hides, so leaving
// a mode releases its subscriptions. A gone extension's panel collapses; a
// lost server's stays, stale.
class ExtensionPanel : public QWidget {
    Q_OBJECT

public:
    ExtensionPanel(SessionExtensions& extensions, const std::string& extension_id,
                   ExtensionViewWidget::Shares shares, QWidget* parent = nullptr);
    ~ExtensionPanel() override;

    ExtensionViewWidget* renderer() const { return renderer_; }
    ExtensionViewFeed* feed() const { return feed_; }

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void present();

    ExtensionViewFeed* feed_;
    ExtensionViewWidget* renderer_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_EXTENSION_PANEL_HPP
