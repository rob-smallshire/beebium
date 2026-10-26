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

#ifndef BEEBIUM_MUSIC5000_OUTPUT_FILTER_HPP
#define BEEBIUM_MUSIC5000_OUTPUT_FILTER_HPP

#include <array>
#include <complex>

namespace beebium {

// The Music 5000's analogue output filter (one side), as a digital filter at
// the synthesiser's own sample rate.
//
// The network on page 3 of the schematic (docs/music5000/hardware-model.md 7.2
// and its appendix) is fourth order from DAC current to output voltage, with
// no zeros:
//
//   - a real pole at the I-to-V node: 1K0 in parallel with the 15K input
//     resistor (which sees a virtual earth), with 22nF: 7717 Hz;
//   - a real pole in stage 1's feedback: 15K with 3n3: 3215 Hz;
//   - stage 2, a unity-R unity-C Sallen-Key with gain K = 1 + 15K/30K = 1.5:
//     a pole pair at f0 = 1/(2 pi 15K 3n3) = 3215 Hz, Q = 1/(3 - K) = 0.667.
//
// The DAC drives each channel as a pulse about 1 us long once per 21.3 us
// sample, not as a held staircase, so there is no zero-order-hold droop to
// model, and the images around 46875 Hz fall below -80 dB after the network.
// The right digital target is therefore the analogue magnitude response up to
// Nyquist.
//
// Design, all computed at construction from the component values:
//
//   - Poles by the matched z-transform, z = exp(p / fs): exact pole
//     frequencies and damping.
//   - A two-zero numerator 1 + b1 z^-1 + b2 z^-2 fitted to the analogue
//     magnitude. Its squared magnitude c0 + c1 cos w + c2 cos 2w is linear in
//     (c0, c1, c2), so the fit is one 3x3 least-squares solve of
//     |N|^2 |D|^-2 against |H_analogue|^2 on 64 log-spaced points from 20 Hz
//     to 20 kHz, weighted for relative (equal-in-dB) error. The audio band,
//     not Nyquist, is the grid's top: fitting to Nyquist pulls the error at
//     20 kHz to 0.6 dB for no audible gain.
//   - The numerator is factored through x = z + 1/z (a quadratic), keeping
//     the root of each reciprocal pair inside the unit circle: minimum phase,
//     with the fitted magnitude.
//   - The gain is set for unity at DC.
//
// Against the analogue response from the same components (fs = 46875 Hz):
// within 0.06 dB from 20 Hz to 20 kHz; -3 dB at 2118 Hz (analogue 2115) and
// -6 dB at 2937 Hz (analogue 2933); monotonic, no peaking.
//
// Free of Beebium dependencies, so it can be tested and reused on its own.
class Music5000OutputFilter {
public:
    struct Components {
        double iv_resistance = 1.0e3;        // R13/R15 (left), R25/R27 (right)
        double iv_capacitance = 22.0e-9;     // C13/C14, C19/C20
        double input_resistance = 15.0e3;    // R14/R16, R26/R28
        double stage1_resistance = 15.0e3;   // R18/R17, R30/R29
        double stage1_capacitance = 3.3e-9;  // C16/C15, C22/C21
        double stage2_resistance = 15.0e3;   // R19, R20 / R31, R32
        double stage2_capacitance = 3.3e-9;  // C17, C18 / C23, C24
        double stage2_gain = 1.5;            // 1 + R22 / (R23 + R24)
    };

    explicit Music5000OutputFilter(double sample_rate_hz = 46875.0);
    Music5000OutputFilter(double sample_rate_hz, const Components& components);

    // Filter one sample.
    double process(double x) {
        // Section 1: the fitted numerator over the Sallen-Key pole pair
        // (transposed direct form II).
        double y = numerator_[0] * x + s1_[0];
        s1_[0] = numerator_[1] * x - pair_a1_ * y + s1_[1];
        s1_[1] = numerator_[2] * x - pair_a2_ * y;
        // Sections 2 and 3: the two real poles.
        y = y + real_pole_[0] * s2_;
        s2_ = y;
        y = y + real_pole_[1] * s3_;
        s3_ = y;
        return gain_ * y;
    }

    void reset() {
        s1_ = {0.0, 0.0};
        s2_ = 0.0;
        s3_ = 0.0;
    }

    // Design inspection, for tests.
    double sample_rate() const { return sample_rate_; }
    const std::array<double, 3>& numerator() const { return numerator_; }
    std::array<std::complex<double>, 2> zeros() const;
    // The digital filter's frequency response at f Hz.
    std::complex<double> response(double frequency_hz) const;
    // The analogue network's response at f Hz, normalised to unity at DC.
    static std::complex<double> analogue_response(double frequency_hz,
                                                  const Components& components);

private:
    double sample_rate_;
    // Denominator: (1 + pair_a1 z^-1 + pair_a2 z^-2) (1 - p0 z^-1) (1 - p1 z^-1).
    double pair_a1_ = 0.0;
    double pair_a2_ = 0.0;
    std::array<double, 2> real_pole_{};
    std::array<double, 3> numerator_{1.0, 0.0, 0.0};
    double gain_ = 1.0;

    std::array<double, 2> s1_{};
    double s2_ = 0.0;
    double s3_ = 0.0;
};

}  // namespace beebium

#endif  // BEEBIUM_MUSIC5000_OUTPUT_FILTER_HPP
