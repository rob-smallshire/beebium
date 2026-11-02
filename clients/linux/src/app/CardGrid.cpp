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
#include <QPainterPath>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTextLayout>

namespace beebium::linux_client {

namespace {

const QSize PICTURE_SIZE(CardGrid::PICTURE_WIDTH, CardGrid::PICTURE_HEIGHT);
constexpr int CORNER_RADIUS = 8;
constexpr int LABEL_PADDING_X = 8;
constexpr int LABEL_PADDING_Y = 6;

// A card: a rounded panel in the view's base colour with a hairline edge,
// the picture across its top, then the name in medium weight and the
// description smaller in the secondary colour; the chosen card's edge is
// drawn in the highlight colour.
class CardDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return {CardGrid::CELL_WIDTH, CardGrid::CARD_HEIGHT + CardGrid::SPACING};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const QPalette& palette = option.palette;
        const QRectF card(option.rect.x() + (option.rect.width() - CardGrid::CARD_WIDTH) / 2.0,
                          option.rect.y() + CardGrid::SPACING / 2.0, CardGrid::CARD_WIDTH,
                          CardGrid::CARD_HEIGHT);
        const bool chosen = option.state & QStyle::State_Selected;
        const bool focused = option.state & QStyle::State_HasFocus;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPainterPath outline;
        outline.addRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), CORNER_RADIUS, CORNER_RADIUS);
        painter->fillPath(outline, palette.base());

        painter->save();
        painter->setClipPath(outline);
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        painter->drawPixmap(card.topLeft(), icon.pixmap(PICTURE_SIZE));
        painter->restore();

        QFont name_font = option.font;
        name_font.setWeight(QFont::Medium);
        QFont detail_font = option.font;
        detail_font.setPointSizeF(detail_font.pointSizeF() * 0.85);
        const int text_width = CardGrid::CARD_WIDTH - 2 * LABEL_PADDING_X;
        qreal y = card.top() + CardGrid::PICTURE_HEIGHT + LABEL_PADDING_Y;
        const auto draw_lines = [&](const QString& text, const QFont& font, const QColor& colour) {
            painter->setFont(font);
            painter->setPen(colour);
            const QFontMetrics metrics(font);
            for (const QString& line : card_lines(text, font, text_width, 2)) {
                painter->drawText(QRectF(card.left() + LABEL_PADDING_X, y, text_width,
                                         metrics.lineSpacing()),
                                  Qt::AlignHCenter | Qt::AlignTop, line);
                y += metrics.lineSpacing();
            }
        };
        draw_lines(index.data(Qt::DisplayRole).toString(), name_font, palette.color(QPalette::Text));
        draw_lines(index.data(CardGrid::DETAIL_ROLE).toString(), detail_font,
                   palette.color(QPalette::PlaceholderText));

        painter->setBrush(Qt::NoBrush);
        if (chosen) {
            painter->setPen(QPen(palette.color(focused ? QPalette::Highlight : QPalette::Mid), 2));
            QPainterPath ring;
            ring.addRoundedRect(card.adjusted(1, 1, -1, -1), CORNER_RADIUS, CORNER_RADIUS);
            painter->drawPath(ring);
        } else {
            painter->setPen(QPen(palette.color(QPalette::Mid), 1));
            painter->drawPath(outline);
        }
        painter->restore();
    }
};

}  // namespace

QStringList card_lines(const QString& text, const QFont& font, int width, int max_lines) {
    QStringList lines;
    if (text.isEmpty() || max_lines <= 0) {
        return lines;
    }
    QTextLayout layout(text, font);
    layout.beginLayout();
    while (lines.size() < max_lines) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(width);
        const QString rest = text.mid(line.textStart());
        if (lines.size() == max_lines - 1) {
            // The last line takes all that is left, elided if it runs on.
            lines << QFontMetrics(font).elidedText(rest.simplified(), Qt::ElideRight, width);
            break;
        }
        lines << text.mid(line.textStart(), line.textLength()).trimmed();
    }
    layout.endLayout();
    return lines;
}

CardGrid::CardGrid(QWidget* parent) : QListWidget(parent) {
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setItemDelegate(new CardDelegate(this));
    setGridSize(QSize(CELL_WIDTH, CARD_HEIGHT + SPACING));
    // No frame or fill of its own: the cards sit on the window, and the
    // scroll bar shows only when they overflow it.
    setFrameShape(QFrame::NoFrame);
    viewport()->setAutoFillBackground(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setViewportMargins(MARGIN - SPACING / 2, 0, MARGIN - SPACING / 2, 0);
}

QListWidgetItem* CardGrid::add_card(const QString& name, const QString& detail,
                                    const QPixmap& thumbnail, bool running) {
    auto* item = new QListWidgetItem(this);
    item->setText(name);
    item->setData(DETAIL_ROLE, detail);
    item->setToolTip(detail.isEmpty() ? name : name + QLatin1Char('\n') + detail);
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
