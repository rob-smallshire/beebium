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

#pragma once

#include <cmath>
#include <numbers>
#include <optional>

namespace beebium {

// A model of the machine's cassette input: the LM324 stages that filter the
// signal from the recorder and square it for the Serial ULA (findings 1.5 and
// 7.1). Run once a sample, at the source's rate:
//
//   1. a one-pole high-pass at 20 Hz, blocking DC;
//   2. the second-order Sallen-Key high-pass at 1.29 kHz and low-pass at
//      4.13 kHz, as biquads designed by the bilinear transform at an internal
//      rate of at least MIN_FILTER_RATE: each source sample is stepped
//      OVERSAMPLE times, the input interpolated linearly between samples (a
//      symmetric kernel, so no phase), because the transform's warping at a
//      low source rate would move the 1200 Hz tone by several clocks. Each stage's Q is the circuit's own: equal components and a
//      gain K give Q = 1 / (3 - K), with K about 1.27 and 1.26. So the low
//      tone's crossings move as on the machine, about +65 degrees at 1200 Hz
//      and -5 at 2400;
//   3. a comparator with hysteresis: the output goes high when the filtered
//      signal rises past +h and low when it falls past -h, h = 0.02 of full
//      scale. No normalisation and no automatic gain.
//
// Each flip of the comparator is an edge, placed between the two samples that
// straddle the threshold by linear interpolation, so edge times are not
// quantised to the sample period. The constants are parameters of the model,
// a Decision (unverified) as a group: they are to be checked against the
// corpus and the DCD-test recordings.
class FrontEnd {
public:
    // --- The model's constants, in one place ---
    static constexpr double DC_BLOCK_HZ = 20.0;
    static constexpr double HIGH_PASS_HZ = 1290.0;
    static constexpr double HIGH_PASS_GAIN = 1.27;  // K of the Sallen-Key stage
    static constexpr double LOW_PASS_HZ = 4130.0;
    static constexpr double LOW_PASS_GAIN = 1.26;
    static constexpr double HYSTERESIS = 0.02;      // of full scale, on the filtered signal

    // The filters run at no less than this, whatever the source's rate.
    static constexpr double MIN_FILTER_RATE = 88200.0;

    static constexpr double q_of(double gain) { return 1.0 / (3.0 - gain); }

    explicit FrontEnd(double rate)
        : rate_(rate), oversample_(rate >= MIN_FILTER_RATE ? 1 : static_cast<unsigned>(std::ceil(MIN_FILTER_RATE / rate))),
          filter_rate_(rate * oversample_) {
        const double pi = std::numbers::pi;
        dc_r_ = std::exp(-2.0 * pi * DC_BLOCK_HZ / filter_rate_);
        high_pass_ = design(HIGH_PASS_HZ, q_of(HIGH_PASS_GAIN), true);
        low_pass_ = design(LOW_PASS_HZ, q_of(LOW_PASS_GAIN), false);
    }

    // Forget the signal, as after a seek: the filters at rest, the output low.
    void reset() {
        dc_x_ = dc_y_ = 0.0;
        high_pass_.clear();
        low_pass_.clear();
        last_ = 0.0;
        previous_x_ = 0.0;
        high_ = false;
    }

    // The filtered signal for one source sample, before the comparator (its
    // value at the sample, after the internal steps between).
    double filter(double x) {
        double y = 0.0;
        for (unsigned k = 1; k <= oversample_; ++k) {
            y = filter_step(previous_x_ + (x - previous_x_) * k / oversample_);
        }
        previous_x_ = x;
        return y;
    }

    // One source sample. Returns where an edge falls, as a fraction in (0, 1]
    // of the way from the previous sample to this one, or nothing. At most one
    // edge falls between two samples: the comparator does not flip back
    // within one, which at 22.05 kHz bounds the tone at 11 kHz.
    std::optional<double> step(double x) {
        std::optional<double> edge;
        for (unsigned k = 1; k <= oversample_; ++k) {
            const double y = filter_step(previous_x_ + (x - previous_x_) * k / oversample_);
            if (!edge) {
                if (!high_ && y > HYSTERESIS) {
                    edge = (k - 1 + crossing(HYSTERESIS, y)) / oversample_;
                    high_ = true;
                } else if (high_ && y < -HYSTERESIS) {
                    edge = (k - 1 + crossing(-HYSTERESIS, y)) / oversample_;
                    high_ = false;
                }
            }
            last_ = y;
        }
        previous_x_ = x;
        return edge;
    }

    // The comparator's output: true high.
    bool high() const { return high_; }
    double rate() const { return rate_; }
    unsigned oversample() const { return oversample_; }

private:
    double filter_step(double x) {
        const double dc = x - dc_x_ + dc_r_ * dc_y_;
        dc_x_ = x;
        dc_y_ = dc;
        return low_pass_.step(high_pass_.step(dc));
    }

    struct Biquad {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        double step(double x) {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
        void clear() { x1 = x2 = y1 = y2 = 0.0; }
    };

    // A second-order high-pass or low-pass at `hz` and `q` (the bilinear
    // transform, prewarped at the corner).
    Biquad design(double hz, double q, bool high_pass) const {
        const double w0 = 2.0 * std::numbers::pi * hz / filter_rate_;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double c = std::cos(w0);
        const double a0 = 1.0 + alpha;
        Biquad f;
        if (high_pass) {
            f.b0 = (1.0 + c) / 2.0 / a0;
            f.b1 = -(1.0 + c) / a0;
        } else {
            f.b0 = (1.0 - c) / 2.0 / a0;
            f.b1 = (1.0 - c) / a0;
        }
        f.b2 = f.b0;
        f.a1 = -2.0 * c / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    // Where between the previous filtered step and `y` the signal crossed `threshold`.
    double crossing(double threshold, double y) const {
        const double d = y - last_;
        if (d == 0.0) return 1.0;
        const double f = (threshold - last_) / d;
        return f <= 0.0 ? 1e-9 : (f > 1.0 ? 1.0 : f);
    }

    double rate_;
    unsigned oversample_;
    double filter_rate_;
    double previous_x_ = 0.0;
    double dc_r_ = 0.0;
    double dc_x_ = 0.0;
    double dc_y_ = 0.0;
    Biquad high_pass_;
    Biquad low_pass_;
    double last_ = 0.0;
    bool high_ = false;
};

}  // namespace beebium
