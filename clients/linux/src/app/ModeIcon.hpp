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

#ifndef BEEBIUM_LINUX_APP_MODE_ICON_HPP
#define BEEBIUM_LINUX_APP_MODE_ICON_HPP

#include <QColor>
#include <QIcon>
#include <QIconEngine>
#include <QString>

namespace beebium::linux_client {

// A sidebar mode's icon (#272): the client's own drawing, one ink, on two
// grids, 24 for the sidebar's mode strip and 16 for menus, each drawn for its
// size rather than scaled from the other. Painted in the palette's ink for the
// icon's mode (the text colour, its disabled colour, the highlighted text
// colour when selected), so one drawing serves light and dark themes.
class ModeIconEngine : public QIconEngine {
public:
    // `name` names the drawing: ":/mode-icons/<grid>/<name>.svg".
    explicit ModeIconEngine(QString name);

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override;
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override;
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override;
    QList<QSize> availableSizes(QIcon::Mode mode, QIcon::State state) override;
    QIconEngine* clone() const override;
    QString key() const override;
    bool isNull() override;

    // The grid drawn for a size: 16 below 20 pixels, 24 from there.
    static int grid_for(int pixels);
    // The ink an icon in `mode` is painted in.
    static QColor ink(QIcon::Mode mode);

private:
    QString name_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_MODE_ICON_HPP
