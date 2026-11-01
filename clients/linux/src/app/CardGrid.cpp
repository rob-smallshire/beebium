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

#include "app/CardGrid.hpp"

#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QStyle>

namespace beebium::linux_client {

namespace {

const QSize PICTURE_SIZE(CardGrid::PICTURE_WIDTH, CardGrid::PICTURE_HEIGHT);

}  // namespace

CardGrid::CardGrid(QWidget* parent) : QListWidget(parent) {
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setWordWrap(true);
    setSpacing(8);
    setIconSize(PICTURE_SIZE);
    setSelectionMode(QAbstractItemView::SingleSelection);
    // Room for the picture and three lines of text: a two-line name or
    // detail wraps rather than being cut.
    const int line = fontMetrics().lineSpacing();
    setGridSize(QSize(PICTURE_WIDTH + 24, PICTURE_HEIGHT + 3 * line + 16));
}

QListWidgetItem* CardGrid::add_card(const QString& name, const QString& detail,
                                    const QPixmap& thumbnail, bool running) {
    auto* item = new QListWidgetItem(this);
    item->setText(detail.isEmpty() ? name : name + QLatin1Char('\n') + detail);
    item->setToolTip(detail.isEmpty() ? name : name + QLatin1Char('\n') + detail);
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    item->setSizeHint(gridSize() - QSize(4, 4));
    set_picture(item, thumbnail, running);
    return item;
}

void CardGrid::set_picture(QListWidgetItem* item, const QPixmap& thumbnail, bool running) {
    item->setIcon(QIcon(picture(thumbnail, running, this)));
}

QPixmap CardGrid::picture(const QPixmap& thumbnail, bool running, const QWidget* style_from) {
    QPixmap canvas(PICTURE_SIZE);
    canvas.fill(Qt::black);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!thumbnail.isNull()) {
        const QPixmap fitted =
            thumbnail.scaled(PICTURE_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawPixmap((PICTURE_WIDTH - fitted.width()) / 2,
                           (PICTURE_HEIGHT - fitted.height()) / 2, fitted);
    } else {
        const QPixmap placeholder =
            style_from->style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(48, 48);
        painter.drawPixmap((PICTURE_WIDTH - placeholder.width()) / 2,
                           (PICTURE_HEIGHT - placeholder.height()) / 2, placeholder);
    }
    if (running) {
        QFont font = style_from->font();
        font.setBold(true);
        font.setPointSizeF(font.pointSizeF() * 0.85);
        painter.setFont(font);
        const QString text = tr("Running");
        const QFontMetrics metrics(font);
        const QRect badge(PICTURE_WIDTH - metrics.horizontalAdvance(text) - 18, 6,
                          metrics.horizontalAdvance(text) + 12, metrics.height() + 4);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x2e, 0x9e, 0x44));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.drawRoundedRect(badge, 4, 4);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, text);
    }
    return canvas;
}

}  // namespace beebium::linux_client
