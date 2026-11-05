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

#include "app/SelectionOverlayWidget.hpp"

#include <QFontMetricsF>
#include <QPainter>
#include <QPalette>

#include <algorithm>
#include <array>
#include <cmath>

namespace beebium::linux_client {

namespace {

QRect whole_pixels(const ViewRect& rect) {
    return QRectF(rect.x, rect.y, rect.width, rect.height).toAlignedRect();
}

QColor with_alpha(QColor colour, double alpha) {
    colour.setAlphaF(static_cast<float>(alpha));
    return colour;
}

constexpr double LABEL_GAP = 4;
constexpr double LABEL_PADDING_X = 6;
constexpr double LABEL_PADDING_Y = 2;
constexpr double LABEL_ALPHA = 0.85;
constexpr double LABEL_FONT_SCALE = 0.85;

QRectF to_rectf(const ViewRect& rect) {
    return QRectF(rect.x, rect.y, rect.width, rect.height);
}

// Everything the selection covers, the drag's corners among it.
QRectF selection_bounds(const SelectionOverlay& overlay) {
    const QPointF anchor(overlay.anchor.x, overlay.anchor.y);
    const QPointF focus(overlay.focus.x, overlay.focus.y);
    QRectF bounds = QRectF(anchor, focus).normalized();
    for (const ViewRect& rect : overlay.range) {
        bounds = bounds.united(to_rectf(rect));
    }
    for (const ViewRect& rect : overlay.text) {
        bounds = bounds.united(to_rectf(rect));
    }
    if (overlay.marquee) {
        bounds = bounds.united(to_rectf(*overlay.marquee));
    }
    return bounds;
}

double clamped(double value, double low, double high) {
    return std::max(low, std::min(value, high));
}


}  // namespace

void paint_selection(QPainter& painter, const SelectionOverlay& overlay, const QColor& tint) {
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(with_alpha(tint, RANGE_ALPHA));
    for (const ViewRect& rect : overlay.range) {
        painter.drawRect(whole_pixels(rect));
    }
    painter.setBrush(with_alpha(tint, TEXT_ALPHA));
    for (const ViewRect& rect : overlay.text) {
        painter.drawRect(whole_pixels(rect));
    }
    if (overlay.marquee) {
        // A one-pixel line inside the rect.
        QPen pen(with_alpha(tint, MARQUEE_ALPHA));
        pen.setWidth(1);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QRectF outline = QRectF(whole_pixels(*overlay.marquee)).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.drawRect(outline);
    }
    painter.restore();
}

QString interpretation_label(Interpretation interpretation) {
    switch (interpretation) {
    case Interpretation::rows:
        break;
    case Interpretation::columns:
        return QObject::tr("Columns");
    case Interpretation::anywhere:
        return QObject::tr("Anywhere");
    }
    return QObject::tr("Rows");
}

QRectF interpretation_label_rect(const SelectionOverlay& overlay, QSizeF label, QSizeF widget) {
    const QRectF bounds = selection_bounds(overlay);
    const double width = label.width();
    const double height = label.height();
    const double max_x = std::max(0.0, widget.width() - width);
    const double max_y = std::max(0.0, widget.height() - height);
    const bool rightward = overlay.focus.x >= overlay.anchor.x;
    const bool downward = overlay.focus.y >= overlay.anchor.y;
    // Along the selection from the anchor, held inside the widget.
    const double along_x =
        clamped(rightward ? overlay.anchor.x : overlay.anchor.x - width, 0, max_x);
    const double along_y =
        clamped(downward ? overlay.anchor.y : overlay.anchor.y - height, 0, max_y);
    const double above = bounds.top() - LABEL_GAP - height;
    const double below = bounds.bottom() + LABEL_GAP;
    const double left = bounds.left() - LABEL_GAP - width;
    const double right = bounds.right() + LABEL_GAP;
    // Behind the anchor first, then the selection's far side, then beside it.
    const std::array<QPointF, 4> candidates{
        QPointF(along_x, downward ? above : below),
        QPointF(along_x, downward ? below : above),
        QPointF(rightward ? left : right, along_y),
        QPointF(rightward ? right : left, along_y),
    };
    for (const QPointF& place : candidates) {
        if (place.x() >= 0 && place.x() <= max_x && place.y() >= 0 && place.y() <= max_y) {
            return QRectF(place, label);
        }
    }
    // No room anywhere around it: inside the widget, over the selection.
    const QPointF first = candidates.front();
    return QRectF(QPointF(clamped(first.x(), 0, max_x), clamped(first.y(), 0, max_y)), label);
}

void paint_interpretation_label(QPainter& painter, const SelectionOverlay& overlay,
                                QSizeF widget, const QColor& tint, const QColor& ink,
                                const QFont& font) {
    const QString text = interpretation_label(overlay.interpretation);
    QFont small = font;
    small.setPointSizeF(font.pointSizeF() * LABEL_FONT_SCALE);
    const QFontMetricsF metrics(small);
    const QSizeF size(std::ceil(metrics.horizontalAdvance(text) + 2 * LABEL_PADDING_X),
                      std::ceil(metrics.height() + 2 * LABEL_PADDING_Y));
    const QRectF place = interpretation_label_rect(overlay, size, widget);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(with_alpha(tint, LABEL_ALPHA));
    painter.drawRoundedRect(place, 3, 3);
    painter.setPen(ink);
    painter.setFont(small);
    painter.drawText(place, Qt::AlignCenter, text);
    painter.restore();
}

SelectionOverlayWidget::SelectionOverlayWidget(const SelectionCoordinator& coordinator,
                                               QWidget* display)
    : QWidget(display), coordinator_(coordinator) {
    setObjectName(QStringLiteral("selectionOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void SelectionOverlayWidget::refresh() {
    setVisible(coordinator_.selecting());
    if (isVisible()) {
        raise();
    }
    update();
}

void SelectionOverlayWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    const SelectionOverlay overlay = coordinator_.overlay();
    paint_selection(painter, overlay, palette().color(QPalette::Highlight));
    painter.setRenderHint(QPainter::Antialiasing, true);
    paint_interpretation_label(painter, overlay, size(), palette().color(QPalette::Highlight),
                               palette().color(QPalette::HighlightedText), font());
}

}  // namespace beebium::linux_client
