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

// The machine's LEDs (client specification 10): discovered, in the
// server's order, as brightness 0 to 255.

#include "app/IndicatorStrip.hpp"
#include "app/LedColour.hpp"
#include "app/SessionIndicators.hpp"

#include "FakeBeebiumServer.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QLayout>
#include <QMouseEvent>
#include <QStringList>

#include <catch2/catch_approx.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

std::shared_ptr<grpc::Channel> channel_to(const FakeServer& server) {
    return grpc::CreateChannel(server.endpoint().toStdString(), grpc::InsecureChannelCredentials());
}

}  // namespace

TEST_CASE("The indicators are the server's, in its order, with their metadata",
          "[indicators]") {
    FakeServer server;
    SessionIndicators indicators;
    indicators.start(channel_to(server));
    REQUIRE(wait_until([&] { return indicators.indicators().size() == 4; }));
    const auto& listed = indicators.indicators();
    CHECK(listed[0].name == "caps-lock-led");
    CHECK(listed[0].label == "CAPS LOCK");
    CHECK(listed[0].color == "625nm");
    CHECK(listed[0].related_key == "Caps Lock");
    CHECK(listed[2].name == "drive-0-led");
    CHECK(listed[2].related_key.empty());
    // Sampled at most every 50 ms.
    REQUIRE(wait_until([&] { return server.indicators.subscriptions == 1; }));
    CHECK(server.indicators.min_interval_ms == 50);
    indicators.stop();
}

TEST_CASE("Brightness arrives as 0 to 255, and only changes are told", "[indicators]") {
    FakeServer server;
    SessionIndicators indicators;
    QStringList changes;
    QObject::connect(&indicators, &SessionIndicators::value_changed,
                     [&](const QString& name, int value) {
                         changes << QStringLiteral("%1=%2").arg(name).arg(value);
                     });
    indicators.start(channel_to(server));
    REQUIRE(wait_until([&] { return server.indicators.subscriptions == 1; }));
    server.indicators.push(std::vector<std::pair<std::string, std::uint32_t>>{
        {"drive-0-led", 128}, {"caps-lock-led", 0}});
    server.indicators.push(std::vector<std::pair<std::string, std::uint32_t>>{
        {"drive-0-led", 128}});  // unchanged: not told
    server.indicators.push(std::vector<std::pair<std::string, std::uint32_t>>{
        {"drive-0-led", 255}});
    REQUIRE(wait_until([&] { return indicators.value(QStringLiteral("drive-0-led")) == 255; }));
    changes.sort();
    CHECK(changes == QStringList{QStringLiteral("caps-lock-led=0"),
                                 QStringLiteral("drive-0-led=128"),
                                 QStringLiteral("drive-0-led=255")});
    indicators.stop();
}

TEST_CASE("A lock LED's change is told apart, for the keyboard's latch", "[indicators]") {
    FakeServer server;
    SessionIndicators indicators;
    QStringList locks;
    QObject::connect(&indicators, &SessionIndicators::lock_led_changed,
                     [&](const QString& name) { locks << name; });
    indicators.start(channel_to(server));
    REQUIRE(wait_until([&] { return server.indicators.subscriptions == 1; }));
    server.indicators.push(std::vector<std::pair<std::string, std::uint32_t>>{
        {"drive-0-led", 255}, {"shift-lock-led", 255}});
    REQUIRE(wait_until([&] { return !locks.isEmpty(); }));
    CHECK(locks == QStringList{QStringLiteral("shift-lock-led")});
    indicators.stop();
}

// --- drawing them --------------------------------------------------------------------

TEST_CASE("An LED's colour is read in every form the metadata uses", "[indicators][colour]") {
    CHECK(led_colour(QStringLiteral("#ff0000")) == QColor(255, 0, 0));
    CHECK(led_colour(QStringLiteral("#0a0")) == QColor(0, 170, 0));
    CHECK(led_colour(QStringLiteral("rgb(214 122 127)")) == QColor(214, 122, 127));
    CHECK(led_colour(QStringLiteral("rgb(214, 122, 127)")) == QColor(214, 122, 127));
    const QColor green = led_colour(QStringLiteral("hsl(120 100% 50%)"));
    CHECK(green.green() == 255);
    CHECK(green.red() == 0);
    CHECK(led_colour(QStringLiteral("nonsense")) == QColor(Qt::gray));
}

TEST_CASE("A wavelength is a hue, brought to an even brightness", "[indicators][colour]") {
    const QColor red = led_colour(QStringLiteral("625nm"));
    const QColor green = led_colour(QStringLiteral("568nm"));
    CHECK(red.red() > red.green());
    CHECK(red.red() > red.blue());
    CHECK(green.green() > green.blue());
    CHECK(green.green() >= green.red());
    // The outline is the same hue, darker.
    const QColor outline = led_outline(red);
    CHECK(outline.lightness() < red.lightness());
}

TEST_CASE("Brightness is a fill, not on or off", "[indicators]") {
    CHECK(LedIndicator::fill_fraction(0) == 0.0);
    CHECK(LedIndicator::fill_fraction(255) == 1.0);
    CHECK(LedIndicator::fill_fraction(128) == Catch::Approx(128.0 / 255.0));
    CHECK(LedIndicator::fill_fraction(400) == 1.0);
}

TEST_CASE("The strip shows the server's LEDs in its order, and only related keys click",
          "[indicators]") {
    FakeServer server;
    SessionIndicators indicators;
    IndicatorStrip strip(indicators);
    strip.show();
    QStringList tapped;
    QObject::connect(&strip, &IndicatorStrip::key_tapped,
                     [&](const QString& key) { tapped << key; });
    indicators.start(channel_to(server));
    REQUIRE(wait_until([&] { return strip.count() == 4; }));
    QStringList order;
    for (int i = 0; i < strip.layout()->count(); ++i) {
        order << strip.layout()->itemAt(i)->widget()->objectName();
    }
    CHECK(order == QStringList{QStringLiteral("caps-lock-led"), QStringLiteral("shift-lock-led"),
                               QStringLiteral("drive-0-led"), QStringLiteral("drive-1-led")});

    REQUIRE(wait_until([&] { return server.indicators.subscriptions == 1; }));
    server.indicators.push(std::vector<std::pair<std::string, std::uint32_t>>{{"drive-0-led", 200}});
    REQUIRE(wait_until([&] { return strip.led(QStringLiteral("drive-0-led"))->value() == 200; }));

    const auto click = [](QWidget* widget) {
        const QPointF centre(widget->width() / 2.0, widget->height() / 2.0);
        QMouseEvent release(QEvent::MouseButtonRelease, centre, widget->mapToGlobal(centre),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &release);
    };
    // Laid out, so each LED has its size (a top-level window does not grow
    // by itself as children arrive; in a machine window a layout sizes it).
    strip.adjustSize();
    REQUIRE(wait_until([&] { return strip.led(QStringLiteral("shift-lock-led"))->width() > 0; }));
    CHECK(strip.led(QStringLiteral("shift-lock-led"))->clickable());
    CHECK_FALSE(strip.led(QStringLiteral("drive-0-led"))->clickable());
    click(strip.led(QStringLiteral("drive-0-led")));
    click(strip.led(QStringLiteral("shift-lock-led")));
    CHECK(tapped == QStringList{QStringLiteral("Shift Lock")});
    indicators.stop();
}
