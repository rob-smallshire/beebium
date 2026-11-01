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

#ifndef BEEBIUM_LINUX_APP_CARD_GRID_HPP
#define BEEBIUM_LINUX_APP_CARD_GRID_HPP

#include <QListWidget>
#include <QPixmap>
#include <QString>

namespace beebium::linux_client {

// The home window's grid of cards (client specification 13.4, after the
// macOS welcome window): each card a picture, the screen thumbnail or a
// placeholder, over a name and one further line.
class CardGrid : public QListWidget {
    Q_OBJECT

public:
    static constexpr int PICTURE_WIDTH = 160;
    static constexpr int PICTURE_HEIGHT = 120;

    explicit CardGrid(QWidget* parent = nullptr);

    // A card; `thumbnail` null shows the placeholder.
    QListWidgetItem* add_card(const QString& name, const QString& detail,
                              const QPixmap& thumbnail = {}, bool running = false);
    // Replace a card's picture, as a thumbnail arrives.
    void set_picture(QListWidgetItem* item, const QPixmap& thumbnail, bool running = false);

    // The picture a card shows: the thumbnail fitted on black, or the
    // placeholder, and a "Running" badge when the machine is.
    static QPixmap picture(const QPixmap& thumbnail, bool running, const QWidget* style_from);
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CARD_GRID_HPP
