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

// A selection's overlay (client specification 6.5): the range light, the
// cells that copy stronger, the anywhere marquee as an outline, all in the
// highlight colour and on whole pixels; and the label naming how the
// selection reads, beside it and never over it.

#include "app/SelectionOverlayWidget.hpp"

#include <QImage>
#include <QRectF>
#include <QPainter>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace beebium::linux_client;

namespace {

QImage painted(const SelectionOverlay& overlay) {
    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    paint_selection(painter, overlay, QColor(0, 0, 255));
    return image;
}

int alpha_at(const QImage& image, int x, int y) {
    return qAlpha(image.pixel(x, y));
}

}  // namespace

TEST_CASE("The range is lighter than the text drawn over it", "[selection][overlay]") {
    SelectionOverlay overlay;
    overlay.range = {{10, 10, 50, 20}};
    overlay.text = {{20, 10, 10, 20}};
    const QImage image = painted(overlay);
    CHECK(alpha_at(image, 5, 5) == 0);
    CHECK(alpha_at(image, 15, 15) == 64);  // a quarter
    // Text over range: a half over a quarter.
    CHECK(alpha_at(image, 25, 15) > 128);
    CHECK(qBlue(image.pixel(25, 15)) == 255);
}

TEST_CASE("Rects are widened to whole pixels", "[selection][overlay]") {
    SelectionOverlay overlay;
    overlay.text = {{10.4, 10.4, 9.2, 9.2}};
    const QImage image = painted(overlay);
    CHECK(alpha_at(image, 10, 10) > 0);
    CHECK(alpha_at(image, 19, 19) > 0);
    CHECK(alpha_at(image, 20, 20) == 0);
}

TEST_CASE("The marquee is an outline, empty inside", "[selection][overlay]") {
    SelectionOverlay overlay;
    overlay.marquee = ViewRect{10, 10, 40, 40};
    const QImage image = painted(overlay);
    CHECK(alpha_at(image, 10, 30) > 0);
    CHECK(alpha_at(image, 49, 30) > 0);
    CHECK(alpha_at(image, 30, 30) == 0);
    CHECK(alpha_at(image, 9, 30) == 0);
}

namespace {

const QSizeF LABEL(60, 16);
const QSizeF WIDGET(1000, 800);

SelectionOverlay dragged(ViewPoint anchor, ViewPoint focus) {
    SelectionOverlay overlay;
    overlay.anchor = anchor;
    overlay.focus = focus;
    const double left = std::min(anchor.x, focus.x);
    const double top = std::min(anchor.y, focus.y);
    overlay.range = {{left, top, std::abs(focus.x - anchor.x), std::abs(focus.y - anchor.y)}};
    return overlay;
}

QRectF bounds_of(const SelectionOverlay& overlay) {
    const ViewRect& r = overlay.range.front();
    return QRectF(r.x, r.y, r.width, r.height);
}

bool inside(const QRectF& label) {
    return QRectF(QPointF(0, 0), WIDGET).contains(label);
}

}  // namespace

TEST_CASE("The label names the reading, nothing more", "[selection][overlay][label]") {
    CHECK(interpretation_label(Interpretation::rows) == QStringLiteral("Rows"));
    CHECK(interpretation_label(Interpretation::columns) == QStringLiteral("Columns"));
    CHECK(interpretation_label(Interpretation::anywhere) == QStringLiteral("Anywhere"));
}

TEST_CASE("The label sits behind the corner the drag began from", "[selection][overlay][label]") {
    // Dragged down and right: above, starting at the anchor.
    SelectionOverlay down_right = dragged({300, 300}, {500, 450});
    QRectF label = interpretation_label_rect(down_right, LABEL, WIDGET);
    CHECK(label.size() == LABEL);
    CHECK(label.bottom() <= 300);
    CHECK(label.left() == 300);
    CHECK_FALSE(label.intersects(bounds_of(down_right)));
    // Dragged up and left: below, ending at the anchor.
    SelectionOverlay up_left = dragged({500, 450}, {300, 300});
    label = interpretation_label_rect(up_left, LABEL, WIDGET);
    CHECK(label.top() >= 450);
    CHECK(label.right() == 500);
    CHECK_FALSE(label.intersects(bounds_of(up_left)));
}

TEST_CASE("At the widget's edge the label flips to the selection's other side",
          "[selection][overlay][label]") {
    // No room above a selection begun at the top.
    SelectionOverlay top = dragged({300, 4}, {500, 200});
    QRectF label = interpretation_label_rect(top, LABEL, WIDGET);
    CHECK(label.top() >= 200);
    CHECK(inside(label));
    // No room below one begun at the bottom and dragged up.
    SelectionOverlay bottom = dragged({300, 796}, {500, 600});
    label = interpretation_label_rect(bottom, LABEL, WIDGET);
    CHECK(label.bottom() <= 600);
    CHECK(inside(label));
    // Begun at the right edge and dragged right: kept inside.
    SelectionOverlay right = dragged({980, 300}, {999, 400});
    label = interpretation_label_rect(right, LABEL, WIDGET);
    CHECK(inside(label));
    CHECK_FALSE(label.intersects(bounds_of(right)));
}

TEST_CASE("A selection the full height of the widget has its label beside it",
          "[selection][overlay][label]") {
    SelectionOverlay tall = dragged({300, 0}, {500, 800});
    const QRectF label = interpretation_label_rect(tall, LABEL, WIDGET);
    CHECK(inside(label));
    CHECK_FALSE(label.intersects(bounds_of(tall)));
}

TEST_CASE("Wherever there is room, the label is inside and clear of the selection",
          "[selection][overlay][label]") {
    for (double ax = 0; ax <= 1000; ax += 125) {
        for (double ay = 0; ay <= 800; ay += 100) {
            for (double fx = 0; fx <= 1000; fx += 250) {
                for (double fy = 0; fy <= 800; fy += 200) {
                    const SelectionOverlay overlay = dragged({ax, ay}, {fx, fy});
                    const QRectF bounds = bounds_of(overlay);
                    const bool room = bounds.top() >= LABEL.height() + 4 ||
                                      WIDGET.height() - bounds.bottom() >= LABEL.height() + 4 ||
                                      bounds.left() >= LABEL.width() + 4 ||
                                      WIDGET.width() - bounds.right() >= LABEL.width() + 4;
                    const QRectF label = interpretation_label_rect(overlay, LABEL, WIDGET);
                    INFO(ax << "," << ay << " to " << fx << "," << fy);
                    CHECK(inside(label));
                    if (room) {
                        CHECK_FALSE(label.intersects(bounds));
                    }
                }
            }
        }
    }
}

TEST_CASE("The label is painted beside the selection, not over it",
          "[selection][overlay][label]") {
    QImage image(400, 300, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        paint_interpretation_label(painter, dragged({100, 100}, {300, 200}), QSizeF(400, 300),
                                   QColor(0, 0, 255), QColor(255, 255, 255), QFont());
    }
    // Above the anchor, nothing over the selection.
    bool above = false;
    for (int x = 100; x < 160; ++x) {
        for (int y = 70; y < 100; ++y) {
            above = above || qAlpha(image.pixel(x, y)) > 0;
        }
    }
    CHECK(above);
    CHECK(qAlpha(image.pixel(200, 150)) == 0);
}
