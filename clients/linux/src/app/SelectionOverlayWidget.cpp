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

#include <QPainter>
#include <QPalette>

namespace beebium::linux_client {

namespace {

QRect whole_pixels(const ViewRect& rect) {
    return QRectF(rect.x, rect.y, rect.width, rect.height).toAlignedRect();
}

QColor with_alpha(QColor colour, double alpha) {
    colour.setAlphaF(static_cast<float>(alpha));
    return colour;
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
    paint_selection(painter, coordinator_.overlay(), palette().color(QPalette::Highlight));
}

}  // namespace beebium::linux_client
