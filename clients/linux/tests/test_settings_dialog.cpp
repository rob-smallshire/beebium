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

// Configure Beebium (client specification 13.3): the paths page, and the
// Video page's defaults for new windows (6.6), as the macOS client's
// Settings > Video.

#include "app/ClientSettings.hpp"
#include "app/DisplayWidget.hpp"
#include "app/MainWindow.hpp"
#include "app/SettingsDialog.hpp"

#include <QAbstractButton>
#include <QComboBox>
#include <QLabel>
#include <QTabWidget>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

QComboBox* combo(QWidget& dialog, const char* name) {
    return dialog.findChild<QComboBox*>(QString::fromLatin1(name));
}

}  // namespace

TEST_CASE("Configure Beebium has the paths page and a Video page", "[settings][dialog]") {
    QTemporaryDir directory;
    ClientSettings settings(directory.filePath(QStringLiteral("Beebium.conf")));
    SettingsDialog dialog(settings);
    auto* pages = dialog.findChild<QTabWidget*>();
    REQUIRE(pages);
    REQUIRE(pages->count() == 2);
    CHECK(pages->tabText(0).remove(QLatin1Char('&')) == QStringLiteral("Paths"));
    CHECK(pages->tabText(1).remove(QLatin1Char('&')) == QStringLiteral("Video"));
}

TEST_CASE("The Video page sets the three defaults new windows start with", "[settings][dialog]") {
    QTemporaryDir directory;
    ClientSettings settings(directory.filePath(QStringLiteral("Beebium.conf")));
    SettingsDialog dialog(settings);
    dialog.set_colour_chooser([](const QColor&, const QString&) { return QColor(1, 2, 3); });
    QComboBox* style = combo(dialog, "defaultStyle");
    QComboBox* pixels = combo(dialog, "defaultPixels");
    REQUIRE(style);
    REQUIRE(pixels);
    CHECK(style->count() == 2);
    CHECK(style->itemText(0) == QStringLiteral("Standard"));
    CHECK(style->itemText(1) == QStringLiteral("Debug"));
    CHECK(pixels->itemText(0) == QStringLiteral("Authentic"));
    CHECK(pixels->itemText(1) == QStringLiteral("Crisp"));
    CHECK(style->currentIndex() == 0);
    CHECK(pixels->currentIndex() == 0);
    bool footer = false;
    for (QLabel* label : dialog.findChildren<QLabel*>()) {
        footer = footer || label->text() == QStringLiteral(
                                                "Defaults apply to new emulator windows. Existing "
                                                "windows keep their current settings.");
    }
    CHECK(footer);
    auto* boxing = dialog.findChild<QAbstractButton*>(QStringLiteral("defaultBoxingColour"));
    REQUIRE(boxing);

    style->setCurrentIndex(1);
    pixels->setCurrentIndex(1);
    boxing->click();
    // Nothing is stored until OK.
    CHECK(settings.video_defaults() == VideoSettings{});
    dialog.accept();
    const VideoSettings defaults = settings.video_defaults();
    CHECK(defaults.style == DisplayStyle::debug);
    CHECK(defaults.pixels == PixelShape::crisp);
    CHECK(defaults.boxing == Rgb{1, 2, 3});
}

TEST_CASE("Cancelling the dialog keeps the video defaults", "[settings][dialog]") {
    QTemporaryDir directory;
    ClientSettings settings(directory.filePath(QStringLiteral("Beebium.conf")));
    SettingsDialog dialog(settings);
    combo(dialog, "defaultStyle")->setCurrentIndex(1);
    dialog.reject();
    CHECK(settings.video_defaults() == VideoSettings{});
}

TEST_CASE("New windows start from the defaults; open windows keep their settings",
          "[settings][video][window]") {
    QTemporaryDir directory;
    ClientSettings settings(directory.filePath(QStringLiteral("Beebium.conf")));
    MainWindow before(QRhiWidget::Api::OpenGL, nullptr, &settings);
    VideoSettings chosen;
    chosen.pixels = PixelShape::crisp;
    chosen.boxing = Rgb{40, 50, 60};
    settings.set_video_defaults(chosen);
    MainWindow after(QRhiWidget::Api::OpenGL, nullptr, &settings);
    CHECK(after.display().video_settings().pixels == PixelShape::crisp);
    CHECK(after.display().video_settings().boxing == Rgb{40, 50, 60});
    CHECK(before.display().video_settings() == VideoSettings{});
    before.set_quitting(true);
    after.set_quitting(true);
}
