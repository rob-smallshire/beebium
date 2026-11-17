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

// The model of the machine's cassette input: its filters, its comparator and
// the interpolated edge times (#208, cassette round 7).

#include <beebium/cassette/FrontEnd.hpp>
#include <beebium/cassette/TapeSignal.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

using namespace beebium;

namespace {

constexpr double PI = std::numbers::pi;

// A signal as a function of time in seconds.
using Signal = std::function<double(double)>;

Signal sine(double hz, double amplitude) {
    return [=](double t) { return amplitude * std::sin(2.0 * PI * hz * t); };
}

Signal square(double hz, double amplitude) {
    return [=](double t) { return std::sin(2.0 * PI * hz * t) >= 0.0 ? amplitude : -amplitude; };
}

// Deterministic uniform noise of peak `amplitude`, one value a sample.
struct Noise {
    uint32_t seed = 2026;
    double amplitude;
    explicit Noise(double a) : amplitude(a) {}
    double operator()() {
        seed = seed * 1664525u + 1013904223u;
        return (static_cast<double>(seed >> 8) / 8388608.0 - 1.0) * amplitude;
    }
};

// Edge times in ULA clocks for `seconds` of `signal` at `rate`, with noise
// added if given.
std::vector<double> edges_of(const Signal& signal, double rate, double seconds, Noise* noise = nullptr) {
    FrontEnd front_end(rate);
    std::vector<double> edges;
    const auto samples = static_cast<uint64_t>(seconds * rate);
    for (uint64_t n = 0; n < samples; ++n) {
        double x = signal(static_cast<double>(n) / rate);
        if (noise) x += (*noise)();
        if (const auto f = front_end.step(x)) {
            edges.push_back((static_cast<double>(n) - 1.0 + *f) * ULA_CLOCK_HZ / rate);
        }
    }
    return edges;
}

// The filtered signal's gain and phase (degrees, positive a lead) for a sine
// at `hz`, measured over the last half of a second at `rate`.
std::pair<double, double> response(double hz, double rate) {
    FrontEnd front_end(rate);
    const auto samples = static_cast<uint64_t>(rate);
    double s = 0.0, c = 0.0, power = 0.0;
    for (uint64_t n = 0; n < samples; ++n) {
        const double w = 2.0 * PI * hz * static_cast<double>(n) / rate;
        const double y = front_end.filter(std::sin(w));
        if (n >= samples / 2) {
            s += y * std::sin(w);
            c += y * std::cos(w);
            power += std::sin(w) * std::sin(w);
        }
    }
    return {std::hypot(s, c) / power, std::atan2(c, s) * 180.0 / PI};
}

size_t edges_in_last_second(const std::vector<double>& edges, double seconds) {
    size_t n = 0;
    for (double e : edges) n += e >= (seconds - 1.0) * ULA_CLOCK_HZ ? 1 : 0;
    return n;
}

}  // namespace

// =============================================================================
// 1. Filters
// =============================================================================

TEST_CASE("Front end constants are the circuit's", "[cassette][front_end]") {
    CHECK(FrontEnd::DC_BLOCK_HZ == 20.0);
    CHECK(FrontEnd::HIGH_PASS_HZ == 1290.0);
    CHECK(FrontEnd::LOW_PASS_HZ == 4130.0);
    CHECK(FrontEnd::q_of(FrontEnd::HIGH_PASS_GAIN) == Catch::Approx(0.578).margin(0.001));
    CHECK(FrontEnd::q_of(FrontEnd::LOW_PASS_GAIN) == Catch::Approx(0.575).margin(0.001));
    CHECK(FrontEnd::HYSTERESIS == 0.02);
}

TEST_CASE("Front end moves 1200 Hz by about +65 degrees and 2400 Hz by about -5", "[cassette][front_end]") {
    const auto [gain_1200, phase_1200] = response(1200.0, 48000.0);
    const auto [gain_2400, phase_2400] = response(2400.0, 48000.0);
    CAPTURE(gain_1200, phase_1200, gain_2400, phase_2400);
    CHECK(std::fabs(phase_1200 - 65.0) <= 5.0);
    CHECK(std::fabs(phase_2400 - -5.0) <= 5.0);
}

TEST_CASE("Front end rejects 50 Hz hum and attenuates 8 kHz", "[cassette][front_end]") {
    CHECK(edges_of(sine(50.0, 1.0), 48000.0, 2.0).empty());
    const auto [gain_8k, phase_8k] = response(8000.0, 48000.0);
    CHECK(20.0 * std::log10(gain_8k) <= -13.0);
    CHECK(edges_of(sine(8000.0, 0.05), 48000.0, 1.0).empty());
}

// =============================================================================
// 2. The comparator
// =============================================================================

TEST_CASE("Front end comparator thresholds", "[cassette][front_end]") {
    // 1200 Hz at 0.08: two edges a cycle.
    const auto low_tone = edges_of(sine(1200.0, 0.08), 48000.0, 2.0);
    CHECK(edges_in_last_second(low_tone, 2.0) == 2400);
    // At 0.01: nothing.
    CHECK(edges_of(sine(1200.0, 0.01), 48000.0, 2.0).empty());
    // 2400 Hz square at full scale.
    CHECK(edges_in_last_second(edges_of(square(2400.0, 1.0), 48000.0, 2.0), 2.0) == 4800);
    // Noise at 0.015 alone: nothing.
    Noise quiet(0.015);
    CHECK(edges_of([](double) { return 0.0; }, 48000.0, 2.0, &quiet).empty());
    // The same noise under a 0.2 sine: exactly the sine's edges.
    Noise under(0.015);
    const auto clean = edges_of(sine(1200.0, 0.2), 48000.0, 2.0);
    const auto noisy = edges_of(sine(1200.0, 0.2), 48000.0, 2.0, &under);
    REQUIRE(noisy.size() == clean.size());
    for (size_t i = 0; i < clean.size(); ++i) CHECK(std::fabs(noisy[i] - clean[i]) < 20.0);
}

// =============================================================================
// 3. Edge timing
// =============================================================================

TEST_CASE("Front end interpolates edges between samples", "[cassette][front_end]") {
    const auto edges = edges_of(sine(2400.0, 0.5), 48000.0, 1.0);
    REQUIRE(edges.size() > 1000);
    // After the filters settle, each half-cycle is 256.4 clocks, not 10 or 11
    // samples (256 or 282 clocks).
    for (size_t i = edges.size() / 2; i + 1 < edges.size(); ++i) {
        CHECK(std::fabs(edges[i + 1] - edges[i] - 256.0) <= 2.0);
    }
}

// =============================================================================
// 4. Rates
// =============================================================================

TEST_CASE("Front end gives the same edges at 22050, 44100, 48000 and 96000 Hz", "[cassette][front_end]") {
    // Carrier, then the low tone: the edges' times after the filters settle.
    const Signal signal = [](double t) {
        return t < 0.5 ? 0.5 * std::sin(2.0 * PI * 2400.0 * t) : 0.5 * std::sin(2.0 * PI * 1200.0 * t);
    };
    const auto reference = edges_of(signal, 48000.0, 1.0);
    for (const double rate : {22050.0, 44100.0, 96000.0}) {
        CAPTURE(rate);
        const auto edges = edges_of(signal, rate, 1.0);
        REQUIRE(edges.size() == reference.size());
        for (size_t i = reference.size() / 4; i < reference.size(); ++i) {
            CHECK(std::fabs(edges[i] - reference[i]) <= 2.0);
        }
    }
}
