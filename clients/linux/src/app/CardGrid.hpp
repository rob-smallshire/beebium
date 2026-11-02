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

#include <QFont>
#include <QListWidget>
#include <QPixmap>
#include <QString>
#include <QStringList>

namespace beebium::linux_client {

// `text` broken into at most `max_lines` lines no wider than `width`,
// the last elided if the text runs on.
QStringList card_lines(const QString& text, const QFont& font, int width, int max_lines);

// The home window's grid of cards (client specification 13.4), laid out as
// the macOS welcome window lays its presets: fixed cells of even spacing,
// each card a picture (the screen thumbnail or a placeholder) over its name
// and a description of up to two lines apiece, in an area with no frame of
// its own whose scroll bar shows only when the cards overflow it.
class CardGrid : public QListWidget {
    Q_OBJECT

public:
    static constexpr int PICTURE_WIDTH = 160;
    static constexpr int PICTURE_HEIGHT = 120;
    static constexpr int CARD_WIDTH = PICTURE_WIDTH;
    // The name and the description, two lines apiece, with their padding.
    static constexpr int LABEL_HEIGHT = 76;
    static constexpr int CARD_HEIGHT = PICTURE_HEIGHT + LABEL_HEIGHT;
    static constexpr int SPACING = 16;
    static constexpr int CELL_WIDTH = CARD_WIDTH + SPACING;
    // Between the cards and the area's sides.
    static constexpr int MARGIN = 24;
    // The card's description; its text is its name.
    static constexpr int DETAIL_ROLE = Qt::UserRole + 32;

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
