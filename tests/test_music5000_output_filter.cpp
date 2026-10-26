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

// The Music 5000 output filter against the analogue network of
// docs/music5000/hardware-model.md section 7.2 and its appendix.

#include "Music5000OutputFilter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace beebium;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kSampleRate = 46875.0;

double db(double magnitude) { return 20.0 * std::log10(magnitude); }

std::vector<double> impulse_response(std::size_t length) {
    Music5000OutputFilter filter(kSampleRate);
    std::vector<double> h(length);
    for (std::size_t n = 0; n < length; ++n) {
        h[n] = filter.process(n == 0 ? 1.0 : 0.0);
    }
    return h;
}

// The magnitude of a signal's discrete-time Fourier transform at f Hz.
double dft_magnitude(const std::vector<double>& x, double frequency_hz) {
    std::complex<double> sum = 0.0;
    const double w = 2.0 * std::numbers::pi * frequency_hz / kSampleRate;
    for (std::size_t n = 0; n < x.size(); ++n) {
        sum += x[n] * std::polar(1.0, -w * static_cast<double>(n));
    }
    return std::abs(sum);
}

// The first frequency, scanning up in 1 Hz steps, at which the filter's
// response falls to `level_db`.
double frequency_at(const Music5000OutputFilter& filter, double level_db) {
    for (double f = 1.0; f < kSampleRate / 2; f += 1.0) {
        if (db(std::abs(filter.response(f))) <= level_db) {
            return f;
        }
    }
    return 0.0;
}

}  // namespace

TEST_CASE("Music5000OutputFilter matches the appendix table within 0.2 dB",
          "[music5000][filter]") {
    // hardware-model.md appendix: the page 3 network, current-driven.
    struct Point { double hz; double appendix_db; };
    const Point points[] = {
        {1000.0, -0.62}, {2000.0, -2.66}, {3215.0, -7.23},
        {5000.0, -15.58}, {10000.0, -34.43}, {20000.0, -56.65},
    };
    const std::vector<double> h = impulse_response(16384);
    for (const auto& p : points) {
        INFO(p.hz << " Hz");
        const double measured = db(dft_magnitude(h, p.hz));
        CHECK_THAT(measured, WithinAbs(p.appendix_db, 0.2));
        // And against the analogue transfer function from the same components.
        const double analogue = db(std::abs(Music5000OutputFilter::analogue_response(
            p.hz, Music5000OutputFilter::Components{})));
        CHECK_THAT(measured, WithinAbs(analogue, 0.2));
    }
}

TEST_CASE("Music5000OutputFilter follows the analogue response across the audio band",
          "[music5000][filter]") {
    Music5000OutputFilter filter(kSampleRate);
    for (double f = 20.0; f <= 20000.0; f *= 1.05) {
        INFO(f << " Hz");
        const double digital = db(std::abs(filter.response(f)));
        const double analogue = db(std::abs(Music5000OutputFilter::analogue_response(
            f, Music5000OutputFilter::Components{})));
        REQUIRE_THAT(digital, WithinAbs(analogue, 0.1));
    }
}

TEST_CASE("Music5000OutputFilter -3 dB and -6 dB points are within 2% of the network's",
          "[music5000][filter]") {
    Music5000OutputFilter filter(kSampleRate);
    const double minus3 = frequency_at(filter, -3.0);
    const double minus6 = frequency_at(filter, -6.0);
    CHECK_THAT(minus3, WithinAbs(2115.0, 2115.0 * 0.02));
    CHECK_THAT(minus6, WithinAbs(2933.0, 2933.0 * 0.02));
}

TEST_CASE("Music5000OutputFilter has unity DC gain and no peaking", "[music5000][filter]") {
    Music5000OutputFilter filter(kSampleRate);
    REQUIRE_THAT(std::abs(filter.response(0.0)), WithinAbs(1.0, 1e-12));

    // A step settles to 1.
    double y = 0.0;
    for (int n = 0; n < 4000; ++n) {
        y = filter.process(1.0);
    }
    REQUIRE_THAT(y, WithinAbs(1.0, 1e-9));

    // Monotonic below 1 kHz, and never above unity anywhere.
    double previous = 0.0;
    for (double f = 1.0; f <= 1000.0; f += 1.0) {
        const double level = db(std::abs(filter.response(f)));
        REQUIRE(level <= previous + 0.05);
        previous = level;
    }
    for (double f = 1.0; f < kSampleRate / 2; f += 10.0) {
        REQUIRE(std::abs(filter.response(f)) <= 1.0 + 1e-12);
    }
}

TEST_CASE("Music5000OutputFilter impulse response decays and sums to its DC gain",
          "[music5000][filter]") {
    const std::vector<double> h = impulse_response(16384);
    double sum = 0.0;
    for (double v : h) {
        REQUIRE(std::isfinite(v));
        sum += v;
    }
    REQUIRE_THAT(sum, WithinAbs(1.0, 1e-9));
    REQUIRE(std::abs(h.back()) < 1e-15);
}

TEST_CASE("Music5000OutputFilter fitted zeros are inside the unit circle",
          "[music5000][filter]") {
    Music5000OutputFilter filter(kSampleRate);
    for (const auto& zero : filter.zeros()) {
        INFO("zero " << zero);
        REQUIRE(std::abs(zero) < 1.0);
    }
}

TEST_CASE("Music5000OutputFilter design is deterministic", "[music5000][filter]") {
    Music5000OutputFilter a(kSampleRate);
    Music5000OutputFilter b(kSampleRate);
    REQUIRE(a.numerator() == b.numerator());
    for (double f : {100.0, 3215.0, 15000.0}) {
        REQUIRE(a.response(f) == b.response(f));
    }
}
