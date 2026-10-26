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

// The Music 5000's 46875 Hz to 48000 Hz resampler.

#include "Music5000Resampler.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace beebium;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kIn = Music5000Resampler::kInputRateHz;
constexpr double kOut = Music5000Resampler::kOutputRateHz;

std::vector<double> resample_sine(double frequency_hz, std::size_t input_count) {
    Music5000Resampler resampler;
    std::vector<double> out;
    for (std::size_t i = 0; i < input_count; ++i) {
        const double x = std::sin(2.0 * std::numbers::pi * frequency_hz * static_cast<double>(i) / kIn);
        resampler.push(x, [&](double y) { out.push_back(y); });
    }
    return out;
}

// DFT bins 0..N/2 of N samples starting at `start`.
std::vector<std::complex<double>> dft(const std::vector<double>& x, std::size_t start, std::size_t n) {
    std::vector<std::complex<double>> bins(n / 2 + 1);
    for (std::size_t k = 0; k < bins.size(); ++k) {
        std::complex<double> sum = 0.0;
        const double w = 2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n);
        for (std::size_t m = 0; m < n; ++m) {
            sum += x[start + m] * std::polar(1.0, -w * static_cast<double>(m));
        }
        bins[k] = sum;
    }
    return bins;
}

}  // namespace

TEST_CASE("Music5000Resampler carries a 1 kHz tone cleanly to 48 kHz",
          "[music5000][resampler]") {
    // 4800 output samples are 0.1 s: 10 Hz bins, 1 kHz in bin 100, a whole
    // number of cycles so a rectangular window has no leakage.
    constexpr std::size_t kN = 4800;
    constexpr std::size_t kStart = 480;
    const std::vector<double> y = resample_sine(1000.0, 6000);
    REQUIRE(y.size() >= kStart + kN);
    const auto bins = dft(y, kStart, kN);

    std::size_t peak = 0;
    for (std::size_t k = 1; k < bins.size(); ++k) {
        if (std::abs(bins[k]) > std::abs(bins[peak])) {
            peak = k;
        }
    }
    REQUIRE(peak == 100);

    const double level_db = 20.0 * std::log10(std::abs(bins[peak]) / (kN / 2.0));
    CHECK_THAT(level_db, WithinAbs(0.0, 0.1));

    double worst_db = -400.0;
    for (std::size_t k = 0; k < bins.size(); ++k) {
        if (k == peak) {
            continue;
        }
        const double rel = 20.0 * std::log10(std::abs(bins[k]) / std::abs(bins[peak]) + 1e-300);
        worst_db = std::max(worst_db, rel);
    }
    INFO("worst other bin " << worst_db << " dB");
    CHECK(worst_db < -80.0);
}

TEST_CASE("Music5000Resampler delays by its stated group delay", "[music5000][resampler]") {
    constexpr std::size_t kN = 4800;
    constexpr std::size_t kStart = 480;
    const std::vector<double> y = resample_sine(1000.0, 6000);
    const auto bins = dft(y, kStart, kN);

    // y[n] = sin(2 pi f (n / fs_out - delay)), so bin 100's phase is
    // 2 pi f (start / fs_out - delay) - pi / 2.
    const double f = 1000.0;
    const double phase = std::arg(bins[100]);
    double delay = static_cast<double>(kStart) / kOut - (phase + std::numbers::pi / 2.0) / (2.0 * std::numbers::pi * f);
    const double period = 1.0 / f;
    delay -= period * std::floor(delay / period);
    CHECK_THAT(delay, WithinAbs(Music5000Resampler::group_delay_seconds(), 1e-6));
    CHECK_THAT(Music5000Resampler::group_delay_seconds() * kIn, WithinAbs(19.996, 0.001));
}

TEST_CASE("Music5000Resampler emits exactly 128 outputs per 125 inputs, without drift",
          "[music5000][resampler]") {
    Music5000Resampler resampler;
    uint64_t outputs = 0;
    const uint64_t inputs = 1'000'003;
    for (uint64_t i = 1; i <= inputs; ++i) {
        resampler.push(0.0, [&](double) { ++outputs; });
        // After i inputs: ceil(i x 128 / 125) outputs.
        const uint64_t expected = (i * 128 + 124) / 125;
        if (outputs != expected) {
            FAIL("after " << i << " inputs: " << outputs << " outputs, expected " << expected);
        }
    }
    REQUIRE(outputs == 1'024'004);  // 1000003 x 1.024 = 1024003.07
}

TEST_CASE("Music5000Resampler has unity gain at DC", "[music5000][resampler]") {
    Music5000Resampler resampler;
    double last = 0.0;
    double worst = 0.0;
    std::size_t count = 0;
    for (int i = 0; i < 2000; ++i) {
        resampler.push(1.0, [&](double y) {
            if (++count > 100) {
                worst = std::max(worst, std::abs(y - 1.0));
            }
            last = y;
        });
    }
    CHECK_THAT(last, WithinAbs(1.0, 1e-4));
    CHECK(worst < 1e-4);
}

TEST_CASE("Music5000Resampler prototype is symmetric and sums to the upsampling factor",
          "[music5000][resampler]") {
    Music5000Resampler resampler;
    const auto& h = resampler.prototype();
    REQUIRE(h.size() == Music5000Resampler::kUp * Music5000Resampler::kTapsPerPhase);
    double sum = 0.0;
    for (std::size_t j = 0; j < h.size(); ++j) {
        REQUIRE_THAT(h[j], WithinAbs(h[h.size() - 1 - j], 1e-15));
        sum += h[j];
    }
    REQUIRE_THAT(sum, WithinAbs(static_cast<double>(Music5000Resampler::kUp), 1e-9));
}
