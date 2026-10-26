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

#include "Music5000Resampler.hpp"

#include <cmath>
#include <numbers>

namespace beebium {

namespace {

// The zeroth-order modified Bessel function of the first kind, by its power
// series (which converges quickly for the arguments a Kaiser window needs).
double bessel_i0(double x) {
    double sum = 1.0;
    double term = 1.0;
    const double quarter_x_squared = x * x / 4.0;
    for (int k = 1; k < 64; ++k) {
        term *= quarter_x_squared / (static_cast<double>(k) * k);
        sum += term;
        if (term < sum * 1e-17) {
            break;
        }
    }
    return sum;
}

}  // namespace

Music5000Resampler::Music5000Resampler() {
    const std::size_t length = static_cast<std::size_t>(kUp) * kTapsPerPhase;
    const double upsampled_rate = kInputRateHz * kUp;
    const double cutoff = (kPassbandEdgeHz + kStopbandEdgeHz) / 2.0 / upsampled_rate;  // cycles/sample
    const double centre = (static_cast<double>(length) - 1.0) / 2.0;
    const double window_norm = bessel_i0(kKaiserBeta);

    prototype_.resize(length);
    double sum = 0.0;
    for (std::size_t j = 0; j < length; ++j) {
        const double t = static_cast<double>(j) - centre;
        const double arg = 2.0 * std::numbers::pi * cutoff * t;
        const double sinc = (t == 0.0) ? 1.0 : std::sin(arg) / arg;
        const double r = t / centre;
        const double window = bessel_i0(kKaiserBeta * std::sqrt(1.0 - r * r)) / window_norm;
        prototype_[j] = 2.0 * cutoff * sinc * window;
        sum += prototype_[j];
    }
    // Unity gain at DC through the interpolator: the taps of each phase sum
    // to one on average, so all of them to kUp.
    for (double& h : prototype_) {
        h *= static_cast<double>(kUp) / sum;
    }

    polyphase_.resize(length);
    for (std::size_t phase = 0; phase < kUp; ++phase) {
        for (std::size_t k = 0; k < kTapsPerPhase; ++k) {
            polyphase_[phase * kTapsPerPhase + k] = prototype_[phase + k * kUp];
        }
    }
}

void Music5000Resampler::reset() {
    history_.fill(0.0);
    head_ = 0;
    offset_ = 0;
}

}  // namespace beebium
