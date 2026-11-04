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

// The Processor mode (client specification 5.6, 13.1): the emulation speed
// control, as the macOS client's, watching the server's pacing statistics
// only while it shows.

#include "app/ProcessorSidebar.hpp"
#include "app/SessionSpeed.hpp"
#include "app/SpeedScale.hpp"

#include "FakeBeebiumServer.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QMouseEvent>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <grpcpp/grpcpp.h>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;
using Catch::Approx;

namespace {

struct Speed {
    FakeServer server;
    SessionSpeed speed;
    Speed() {
        speed.start(grpc::CreateChannel(server.endpoint().toStdString(),
                                        grpc::InsecureChannelCredentials()));
    }
    std::vector<double> requests() {
        std::lock_guard lock(server.system.calls_mutex);
        return server.system.speed_requests;
    }
    void push(double configured, double achieved, double estimated_max) {
        beebium::PacingStats stats;
        stats.set_speed_multiplier(configured);
        stats.set_achieved_speed_multiplier(achieved);
        stats.set_estimated_max_speed_multiplier(estimated_max);
        server.system.push_pacing(stats);
    }
};

void mouse(QWidget* widget, QEvent::Type type, QPoint at) {
    const Qt::MouseButtons held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, QPointF(at), widget->mapToGlobal(QPointF(at)), Qt::LeftButton, held,
                      Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

}  // namespace

TEST_CASE("The Processor mode watches the pacing statistics only while it shows",
          "[processor]") {
    Speed fixture;
    ProcessorSidebar processor(fixture.speed);
    wait_until([] { return false; }, std::chrono::milliseconds(100));
    CHECK(fixture.server.system.pacing_watchers == 0);
    processor.show();
    REQUIRE(wait_until([&] { return fixture.server.system.pacing_watchers == 1; }));
    processor.hide();
    REQUIRE(wait_until([&] { return fixture.server.system.pacing_watchers == 0; }));
}

TEST_CASE("The Processor mode has a headline, the scale and the Unlimited switch",
          "[processor]") {
    Speed fixture;
    ProcessorSidebar processor(fixture.speed);
    bool headline = false;
    for (QLabel* label : processor.findChildren<QLabel*>()) {
        headline = headline || label->text() == QStringLiteral("Emulation Speed");
    }
    CHECK(headline);
    REQUIRE(processor.scale());
    CHECK(processor.scale()->height() == SpeedScaleWidget::HEIGHT);
    REQUIRE(processor.unlimited());
    CHECK(processor.unlimited()->text() == QStringLiteral("Unlimited"));
    CHECK(processor.unlimited()->toolTip() ==
          QStringLiteral("Run the emulator as fast as the host allows"));
}

TEST_CASE("The scale adopts the server's speed once and follows the achieved speed",
          "[processor]") {
    Speed fixture;
    ProcessorSidebar processor(fixture.speed);
    processor.resize(280, 200);
    processor.show();
    SpeedScaleWidget* scale = processor.scale();
    fixture.push(2.0, 1.5, 20.0);
    REQUIRE(wait_until([&] { return scale->achieved() == 1.5; }));
    CHECK(scale->configured() == 2.0);
    CHECK_FALSE(scale->thumb_dimmed());
    CHECK(scale->axis().max() == 64.0);
    CHECK_FALSE(processor.unlimited()->isChecked());
    // The window owns the speed after the first sample.
    fixture.push(6.0, 5.5, 20.0);
    REQUIRE(wait_until([&] { return scale->achieved() == 5.5; }));
    CHECK(scale->configured() == 2.0);
    CHECK(fixture.requests().empty());
}

TEST_CASE("A machine running flat out is adopted as Unlimited", "[processor]") {
    Speed fixture;
    ProcessorSidebar processor(fixture.speed);
    processor.show();
    fixture.push(0.0, 30.0, 30.0);
    REQUIRE(wait_until([&] { return processor.unlimited()->isChecked(); }));
    CHECK(processor.scale()->thumb_dimmed());
    CHECK(processor.scale()->configured() == 1.0);
}

TEST_CASE("Clicking a label sends that power of two; 1x is the reset", "[processor]") {
    Speed fixture;
    ProcessorSidebar processor(fixture.speed);
    processor.resize(280, 200);
    processor.show();
    SpeedScaleWidget* scale = processor.scale();
    const auto labels = scale->axis().label_multipliers();
    REQUIRE(labels.size() == 7);  // 1/8 to 8 before any sample
    const QPoint four = scale->label_rect(5).center();
    mouse(scale, QEvent::MouseButtonPress, four);
    mouse(scale, QEvent::MouseButtonRelease, four);
    REQUIRE(wait_until([&] { return fixture.requests().size() == 1; }));
    CHECK(fixture.requests().back() == 4.0);
    CHECK(scale->configured() == 4.0);
    const QPoint one = scale->label_rect(3).center();
    mouse(scale, QEvent::MouseButtonPress, one);
    mouse(scale, QEvent::MouseButtonRelease, one);
    REQUIRE(wait_until([&] { return fixture.requests().size() == 2; }));
    CHECK(fixture.requests().back() == 1.0);
}

TEST_CASE("Dragging the thumb sends the speed only when let go", "[processor]") {
    Speed fixture;
    ProcessorSidebar processor(fixture.speed);
    processor.resize(280, 200);
    processor.show();
    SpeedScaleWidget* scale = processor.scale();
    const int y = scale->axis_y();
    mouse(scale, QEvent::MouseButtonPress, QPoint(scale->x_for(1.0), y));
    mouse(scale, QEvent::MouseMove, QPoint(scale->x_for(2.0), y));
    CHECK(scale->configured() == Approx(2.0).epsilon(0.05));
    wait_until([] { return false; }, std::chrono::milliseconds(100));
    CHECK(fixture.requests().empty());
    mouse(scale, QEvent::MouseButtonRelease, QPoint(scale->x_for(2.0), y));
    REQUIRE(wait_until([&] { return fixture.requests().size() == 1; }));
    CHECK(fixture.requests().back() == Approx(2.0).epsilon(0.05));
}

TEST_CASE("Unlimited sends 0.0, dims the thumb, and turning it off restores the speed",
          "[processor]") {
    Speed fixture;
    ProcessorSidebar processor(fixture.speed);
    processor.resize(280, 200);
    processor.show();
    SpeedScaleWidget* scale = processor.scale();
    const QPoint two = scale->label_rect(4).center();
    mouse(scale, QEvent::MouseButtonPress, two);
    mouse(scale, QEvent::MouseButtonRelease, two);
    processor.unlimited()->click();
    REQUIRE(wait_until([&] { return fixture.requests().size() == 2; }));
    CHECK(fixture.requests() == std::vector<double>{2.0, 0.0});
    CHECK(scale->thumb_dimmed());
    CHECK(scale->configured() == 2.0);
    processor.unlimited()->click();
    REQUIRE(wait_until([&] { return fixture.requests().size() == 3; }));
    CHECK(fixture.requests().back() == 2.0);
    CHECK_FALSE(scale->thumb_dimmed());
    // A speed chosen while unlimited leaves unlimited.
    processor.unlimited()->click();
    const QPoint half = scale->label_rect(2).center();
    mouse(scale, QEvent::MouseButtonPress, half);
    mouse(scale, QEvent::MouseButtonRelease, half);
    REQUIRE(wait_until([&] { return fixture.requests().size() == 5; }));
    CHECK(fixture.requests().back() == 0.5);
    CHECK_FALSE(processor.unlimited()->isChecked());
}
