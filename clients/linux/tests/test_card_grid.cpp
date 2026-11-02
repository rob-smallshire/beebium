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

// The home window's cards, laid out as the macOS welcome window lays its
// presets: fixed cells of even spacing, each card a picture over its name
// and description of up to two lines apiece, in an area with no frame of
// its own (client specification 5.3, 13.4).

#include "app/CardGrid.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QFont>
#include <QFontMetrics>

using namespace beebium::linux_client;

TEST_CASE("A card's text is its name, its description held apart", "[home][cards]") {
    CardGrid grid;
    QListWidgetItem* card = grid.add_card(QStringLiteral("Model B"),
                                          QStringLiteral("BBC Model B with Acorn DFS"));
    CHECK(card->text() == QStringLiteral("Model B"));
    CHECK(card->data(CardGrid::DETAIL_ROLE).toString() ==
          QStringLiteral("BBC Model B with Acorn DFS"));
}

TEST_CASE("Cards sit in fixed cells of even spacing in an area with no frame", "[home][cards]") {
    CardGrid grid;
    CHECK(grid.gridSize() ==
          QSize(CardGrid::CELL_WIDTH, CardGrid::card_height(grid.font()) + CardGrid::SPACING));
    CHECK(CardGrid::CELL_WIDTH == CardGrid::CARD_WIDTH + CardGrid::SPACING);
    CHECK(grid.frameShape() == QFrame::NoFrame);
    CHECK_FALSE(grid.viewport()->autoFillBackground());
    CHECK(grid.verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded);
    CHECK(grid.horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff);
}

TEST_CASE("A card has room for two lines of name and two of description in the font in use",
          "[home][cards]") {
    CardGrid grid;
    for (const qreal points : {9.0, 12.0, 16.0}) {
        QFont font = grid.font();
        font.setPointSizeF(points);
        grid.setFont(font);
        QFont name_font = font;
        name_font.setWeight(QFont::Medium);
        const int needed = CardGrid::PICTURE_HEIGHT +
                           2 * QFontMetrics(name_font).lineSpacing() +
                           2 * QFontMetrics(CardGrid::detail_font(font)).lineSpacing();
        CHECK(CardGrid::card_height(font) >= needed);
        CHECK(grid.gridSize().height() == CardGrid::card_height(font) + CardGrid::SPACING);
    }
}

TEST_CASE("A card's text stops at two lines, the last elided", "[home][cards]") {
    const QFont font;
    const int width = CardGrid::CARD_WIDTH - 16;
    CHECK(card_lines(QStringLiteral("Model B"), font, width, 2) == QStringList{QStringLiteral("Model B")});
    const QString long_text = QStringLiteral(
        "A BBC Model B with the Acorn 6502 Second Processor, Econet over AUN and a Music 5000");
    const QStringList lines = card_lines(long_text, font, width, 2);
    REQUIRE(lines.size() == 2);
    const QFontMetrics metrics(font);
    for (const QString& line : lines) {
        CHECK(metrics.horizontalAdvance(line) <= width);
    }
    CHECK(lines.back().endsWith(QChar(0x2026)));
}
