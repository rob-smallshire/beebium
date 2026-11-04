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

// The emulation speed scale (client specification 5.6), as the macOS
// client's: a log2 axis centred on 1x, sized from a spike-robust estimate of
// the attainable speed, labelled at powers of two.

#include "app/SpeedScale.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace beebium::linux_client;
using Catch::Approx;

TEST_CASE("The attainable speed is the near-maximum of the recent samples", "[speed][scale]") {
    AttainableSpeed attainable;
    CHECK(attainable.value() == 1.0);  // nothing heard yet
    // The larger of achieved and estimated, kept within 1x to 512x.
    attainable.add(0.5, 0.0);
    CHECK(attainable.value() == 1.0);
    attainable.add(3.0, 12.0);
    CHECK(attainable.value() == 12.0);
    attainable.add(2000.0, 0.0);
    CHECK(attainable.value() == 512.0);
    attainable.add(std::numeric_limits<double>::infinity(), 0.0);
    CHECK(attainable.value() == 512.0);

    // With ten or more samples the top tenth is set aside as spikes.
    AttainableSpeed steady;
    for (int i = 0; i < 9; ++i) {
        steady.add(1.0, 10.0);
    }
    steady.add(1.0, 300.0);  // the tenth: one spike in ten
    CHECK(steady.value() == 10.0);
}

TEST_CASE("The attainable speed forgets samples older than its window", "[speed][scale]") {
    AttainableSpeed attainable;
    attainable.add(1.0, 100.0);
    for (std::size_t i = 0; i < AttainableSpeed::WINDOW; ++i) {
        attainable.add(1.0, 4.0);
    }
    CHECK(attainable.value() == 4.0);
}

TEST_CASE("The axis is the tightest seven- or nine-label family covering the attainable speed",
          "[speed][scale]") {
    const auto check = [](double attainable, double max, int labels) {
        const SpeedAxis axis = speed_axis_for(attainable);
        CHECK(axis.max() == max);
        CHECK(axis.min() == 1.0 / max);
        CHECK(axis.label_multipliers().size() == static_cast<std::size_t>(labels));
    };
    check(1.0, 8.0, 7);
    check(7.0, 8.0, 7);
    check(9.0, 16.0, 9);
    check(20.0, 64.0, 7);
    check(100.0, 256.0, 9);
    check(512.0, 512.0, 7);
    CHECK(speed_axis_for(20.0).label_multipliers() ==
          std::vector<double>{1.0 / 64, 1.0 / 16, 1.0 / 4, 1.0, 4.0, 16.0, 64.0});
}

TEST_CASE("Positions on the axis are logarithmic, 1x at the centre, clamped to the ends",
          "[speed][scale]") {
    const SpeedAxis axis = speed_axis_for(7.0);  // 1/8 to 8
    CHECK(axis.fraction_of(1.0) == Approx(0.5));
    CHECK(axis.fraction_of(8.0) == Approx(1.0));
    CHECK(axis.fraction_of(1.0 / 8) == Approx(0.0));
    CHECK(axis.fraction_of(2.0) == Approx(4.0 / 6));
    CHECK(axis.fraction_of(1000.0) == Approx(1.0));
    CHECK(axis.fraction_of(0.0) == Approx(0.0));
    CHECK(axis.multiplier_at(0.5) == Approx(1.0));
    CHECK(axis.multiplier_at(1.0) == Approx(8.0));
    CHECK(axis.multiplier_at(-0.5) == Approx(1.0 / 8));
    CHECK(axis.multiplier_at(4.0 / 6) == Approx(2.0));
}

TEST_CASE("Speeds are labelled as the macOS client labels them", "[speed][scale]") {
    CHECK(speed_label(1.0) == QStringLiteral("1×"));
    CHECK(speed_label(8.0) == QStringLiteral("8×"));
    CHECK(speed_label(1.5) == QStringLiteral("1.5×"));
    CHECK(speed_label(0.5) == QStringLiteral("1/2"));
    CHECK(speed_label(1.0 / 16) == QStringLiteral("1/16"));
    CHECK(speed_label(0.3) == QStringLiteral("0.3×"));
}

TEST_CASE("The speed control adopts the server's speed once, then owns it", "[speed][control]") {
    SpeedControl control;
    CHECK(control.configured() == 1.0);
    CHECK_FALSE(control.unlimited());
    CHECK(control.adopt(3.0));
    CHECK(control.configured() == 3.0);
    // Only the first sample after connecting is adopted.
    CHECK_FALSE(control.adopt(6.0));
    CHECK(control.configured() == 3.0);

    SpeedControl flat_out;
    CHECK(flat_out.adopt(0.0));
    CHECK(flat_out.unlimited());
    CHECK(flat_out.configured() == 1.0);
    CHECK(flat_out.set_unlimited(false) == 1.0);  // back to 1x
}

TEST_CASE("Unlimited sends 0.0 and remembers the speed it leaves", "[speed][control]") {
    SpeedControl control;
    CHECK(control.apply(4.0) == 4.0);
    CHECK(control.set_unlimited(true) == 0.0);
    CHECK(control.unlimited());
    CHECK(control.configured() == 4.0);  // the thumb stays there, dimmed
    CHECK(control.set_unlimited(false) == 4.0);
    // A finite speed chosen while unlimited leaves unlimited.
    control.set_unlimited(true);
    CHECK(control.apply(0.5) == 0.5);
    CHECK_FALSE(control.unlimited());
}
