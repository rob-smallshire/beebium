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

// The Video mode (client specification 6.3, 6.6, 13.1): this window's video
// settings, as the macOS client's Video mode lays them out.

#include "app/DisplayWidget.hpp"
#include "app/MainWindow.hpp"
#include "app/Sidebar.hpp"
#include "app/VideoSidebar.hpp"

#include "FakeBeebiumServer.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QLabel>
#include <QSpinBox>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

QAbstractButton* button(QWidget& widget, const QString& text) {
    for (auto* found : widget.findChildren<QAbstractButton*>()) {
        if (found->text() == text) {
            return found;
        }
    }
    return nullptr;
}

QStringList visible_labels_top_to_bottom(QWidget& widget) {
    std::vector<QLabel*> labels;
    for (QLabel* label : widget.findChildren<QLabel*>()) {
        if (label->isVisibleTo(&widget) && !label->text().isEmpty()) {
            labels.push_back(label);
        }
    }
    std::stable_sort(labels.begin(), labels.end(), [&](QLabel* a, QLabel* b) {
        return a->mapTo(&widget, QPoint()).y() < b->mapTo(&widget, QPoint()).y();
    });
    QStringList texts;
    for (QLabel* label : labels) {
        texts << label->text();
    }
    return texts;
}

}  // namespace

TEST_CASE("The Video mode lays its controls out in the macOS client's order", "[video][mode]") {
    VideoSidebar video;
    video.resize(Sidebar::WIDTH, 600);
    video.show();
    CHECK(visible_labels_top_to_bottom(video) ==
          QStringList{QStringLiteral("Pixels"), QStringLiteral("Display Style"),
                      QStringLiteral("Edge margin"), QStringLiteral("Size"),
                      QStringLiteral("Colour"), QStringLiteral("Boxing Colour")});
    for (const char* choice : {"Authentic", "Crisp", "Standard", "Debug"}) {
        INFO(choice);
        REQUIRE(button(video, QString::fromLatin1(choice)));
    }
    CHECK(button(video, QStringLiteral("Authentic"))->isChecked());
    CHECK(button(video, QStringLiteral("Standard"))->isChecked());
    // Fits the sidebar.
    CHECK(video.minimumSizeHint().width() <= Sidebar::WIDTH);
}

TEST_CASE("Choosing the pixel shape and style changes this window's settings",
          "[video][mode]") {
    VideoSidebar video;
    video.resize(Sidebar::WIDTH, 600);
    video.show();
    std::optional<VideoSettings> heard;
    QObject::connect(&video, &VideoSidebar::changed,
                     [&](const VideoSettings& settings) { heard = settings; });
    button(video, QStringLiteral("Crisp"))->click();
    REQUIRE(heard);
    CHECK(heard->pixels == PixelShape::crisp);

    // Debug has no options, so the edge margin goes, divider and all.
    button(video, QStringLiteral("Debug"))->click();
    CHECK(heard->style == DisplayStyle::debug);
    CHECK(visible_labels_top_to_bottom(video) ==
          QStringList{QStringLiteral("Pixels"), QStringLiteral("Display Style"),
                      QStringLiteral("Boxing Colour")});
    button(video, QStringLiteral("Standard"))->click();
    CHECK(heard->style == DisplayStyle::standard);
}

TEST_CASE("The edge margin's size is a percent from 0 to 10, resettable to 2",
          "[video][mode]") {
    VideoSidebar video;
    video.show();
    auto* size = video.findChild<QSpinBox*>(QStringLiteral("edgeMarginSize"));
    REQUIRE(size);
    CHECK(size->minimum() == 0);
    CHECK(size->maximum() == VideoSettings::MAX_EDGE_MARGIN_PERCENT);
    CHECK(size->suffix() == QStringLiteral("%"));
    CHECK(size->value() == 2);
    auto* reset = video.findChild<QAbstractButton*>(QStringLiteral("edgeMarginSizeReset"));
    REQUIRE(reset);
    CHECK_FALSE(reset->isEnabled());  // already the default
    CHECK(reset->toolTip() == QStringLiteral("Reset to default"));
    size->setValue(7);
    CHECK(video.settings().edge_margin_percent == 7);
    CHECK(reset->isEnabled());
    reset->click();
    CHECK(video.settings().edge_margin_percent == 2);
    CHECK(size->value() == 2);
    CHECK_FALSE(reset->isEnabled());
}

TEST_CASE("Colours come from the platform's picker, opaque, and reset to their defaults",
          "[video][mode]") {
    VideoSidebar video;
    std::optional<QColor> offer;
    QString asked_title;
    video.set_colour_chooser([&](const QColor&, const QString& title) {
        asked_title = title;
        return offer;
    });
    video.show();
    auto* boxing = video.findChild<QAbstractButton*>(QStringLiteral("boxingColour"));
    auto* boxing_reset = video.findChild<QAbstractButton*>(QStringLiteral("boxingColourReset"));
    REQUIRE(boxing);
    REQUIRE(boxing_reset);
    CHECK_FALSE(boxing_reset->isEnabled());

    // Cancelled: nothing changes.
    boxing->click();
    CHECK(asked_title == QStringLiteral("Boxing Colour"));
    CHECK(video.settings().boxing == Rgb{15, 15, 15});

    offer = QColor(200, 10, 20, 64);  // any alpha is dropped
    boxing->click();
    CHECK(video.settings().boxing == Rgb{200, 10, 20});
    CHECK(boxing_reset->isEnabled());
    boxing_reset->click();
    CHECK(video.settings().boxing == Rgb{15, 15, 15});

    auto* margin = video.findChild<QAbstractButton*>(QStringLiteral("edgeMarginColour"));
    REQUIRE(margin);
    offer = QColor(0, 0, 255);
    margin->click();
    CHECK(video.settings().edge_margin_colour == Rgb{0, 0, 255});
    auto* margin_reset =
        video.findChild<QAbstractButton*>(QStringLiteral("edgeMarginColourReset"));
    REQUIRE(margin_reset);
    margin_reset->click();
    CHECK(video.settings().edge_margin_colour == Rgb{0, 0, 0});
}

TEST_CASE("A machine window's Video mode drives that window's display", "[video][window]") {
    FakeServer server;
    MainWindow window(QRhiWidget::Api::OpenGL);
    window.show();
    auto* sidebar = window.findChild<Sidebar*>();
    REQUIRE(sidebar);
    CHECK(sidebar->has_mode(SidebarMode::video));
    auto* video = window.findChild<VideoSidebar*>();
    REQUIRE(video);
    CHECK(window.display().video_settings() == VideoSettings{});
    button(*video, QStringLiteral("Crisp"))->click();
    CHECK(window.display().video_settings().pixels == PixelShape::crisp);
    window.set_quitting(true);
    window.close();
}
