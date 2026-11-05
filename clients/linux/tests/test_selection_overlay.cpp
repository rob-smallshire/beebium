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
// highlight colour and on whole pixels.

#include "app/SelectionOverlayWidget.hpp"

#include <QImage>
#include <QPainter>

#include <catch2/catch_test_macros.hpp>

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
