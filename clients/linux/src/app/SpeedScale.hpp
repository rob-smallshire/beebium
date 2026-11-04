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

#ifndef BEEBIUM_LINUX_APP_SPEED_SCALE_HPP
#define BEEBIUM_LINUX_APP_SPEED_SCALE_HPP

#include <QString>

#include <cstddef>
#include <deque>
#include <vector>

namespace beebium::linux_client {

// The emulation speed control's arithmetic (client specification 5.6), the
// macOS client's exactly: speeds are multiples of real time.

// A spike-robust estimate of the fastest the host can run the machine: of
// the last WINDOW samples, the highest once the top tenth are set aside as
// spikes, and 1x until anything is heard.
class AttainableSpeed {
public:
    static constexpr std::size_t WINDOW = 120;  // 30 s of 250 ms samples
    static constexpr double CEILING = 512.0;

    // One pacing sample: the larger of the achieved and estimated maximum
    // speeds, kept within 1x and the ceiling; one not finite is ignored.
    void add(double achieved, double estimated_max);
    double value() const;

private:
    std::deque<double> samples_;
};

// A symmetric log2 axis, 1x at its centre: half_span labels either side of
// 1x, step powers of two apart.
struct SpeedAxis {
    int half_span = 3;
    int step = 1;

    int exponent() const { return half_span * step; }
    double max() const;
    double min() const { return 1.0 / max(); }
    // 2^(step * i) for i from -half_span to half_span.
    std::vector<double> label_multipliers() const;
    // Where a speed sits, 0 at the slow end and 1 at the fast, clamped.
    double fraction_of(double multiplier) const;
    // The speed at a place on the axis, clamped to its ends.
    double multiplier_at(double fraction) const;
};

// The tightest axis covering an attainable speed: of seven labels (3 steps
// a side) or nine (4), whichever reaches it with the smaller span, nine on
// a tie: 8x, 16x, 64x, 256x or 512x.
SpeedAxis speed_axis_for(double attainable);

// "1×", "8×", "1.5×" at or above real time; "1/2", "1/16" below it.
QString speed_label(double multiplier);

// The configured speed and the Unlimited switch. Each change returns the
// multiplier to send the server: 0.0 for unlimited.
class SpeedControl {
public:
    double configured() const { return configured_; }
    bool unlimited() const { return unlimited_; }

    // The server's configured speed from the first pacing sample after
    // connecting, adopted once (0.0 is unlimited); after that the control
    // owns it. Returns whether this one was adopted.
    bool adopt(double server_multiplier);
    // A finite speed chosen (a drag let go, a label clicked): leaves
    // unlimited.
    double apply(double multiplier);
    // Unlimited on sends 0.0; off sends the speed it left.
    double set_unlimited(bool unlimited);

private:
    double configured_ = 1.0;
    bool unlimited_ = false;
    bool adopted_ = false;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SPEED_SCALE_HPP
