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

#include "Music5000OutputFilter.hpp"

#include <cmath>
#include <numbers>

namespace beebium {

namespace {

using Complex = std::complex<double>;

constexpr int kFitPointCount = 64;
constexpr double kFitLowHz = 20.0;
constexpr double kFitHighHz = 20000.0;

struct AnaloguePoles {
    double iv_node;          // rad/s
    double stage1;           // rad/s
    double stage2_natural;   // rad/s
    double stage2_q;
};

AnaloguePoles analogue_poles(const Music5000OutputFilter::Components& c) {
    // The I-to-V node sees its 1K0 in parallel with the 15K input resistor,
    // whose far end is at the op-amp's virtual earth.
    const double node_resistance =
        1.0 / (1.0 / c.iv_resistance + 1.0 / c.input_resistance);
    return {
        1.0 / (node_resistance * c.iv_capacitance),
        1.0 / (c.stage1_resistance * c.stage1_capacitance),
        1.0 / (c.stage2_resistance * c.stage2_capacitance),
        1.0 / (3.0 - c.stage2_gain),
    };
}

// Determinant of a 3x3 matrix given by rows.
double det3(const double m[3][3]) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
         - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

// The root of z^2 - x z + 1 = 0 inside (or on) the unit circle.
Complex inner_root(Complex x) {
    const Complex root = std::sqrt(x * x - 4.0);
    const Complex a = (x + root) / 2.0;
    const Complex b = (x - root) / 2.0;
    return std::abs(a) <= std::abs(b) ? a : b;
}

}  // namespace

Music5000OutputFilter::Music5000OutputFilter(double sample_rate_hz)
    : Music5000OutputFilter(sample_rate_hz, Components{}) {}

Music5000OutputFilter::Music5000OutputFilter(double sample_rate_hz, const Components& components)
    : sample_rate_(sample_rate_hz) {
    const AnaloguePoles poles = analogue_poles(components);
    const double t = 1.0 / sample_rate_hz;

    // Matched z-transform of the poles.
    real_pole_[0] = std::exp(-poles.iv_node * t);
    real_pole_[1] = std::exp(-poles.stage1 * t);
    const double zeta = 1.0 / (2.0 * poles.stage2_q);
    const Complex pair_pole = std::exp(
        Complex(-zeta, std::sqrt(1.0 - zeta * zeta)) * poles.stage2_natural * t);
    pair_a1_ = -2.0 * pair_pole.real();
    pair_a2_ = std::norm(pair_pole);

    // Fit c0 + c1 cos w + c2 cos 2w = |N|^2 to |H_analogue|^2 |D|^2 by least
    // squares in relative error: the normal equations of rows
    // [1, cos w, cos 2w] / target against 1.
    double normal[3][3] = {};
    double rhs[3] = {};
    for (int i = 0; i < kFitPointCount; ++i) {
        const double f = kFitLowHz * std::pow(kFitHighHz / kFitLowHz,
                                              static_cast<double>(i) / (kFitPointCount - 1));
        const double w = 2.0 * std::numbers::pi * f / sample_rate_hz;
        const Complex z_inv = std::polar(1.0, -w);
        const Complex denominator = (1.0 + pair_a1_ * z_inv + pair_a2_ * z_inv * z_inv)
                                  * (1.0 - real_pole_[0] * z_inv)
                                  * (1.0 - real_pole_[1] * z_inv);
        const double target = std::norm(analogue_response(f, components)) * std::norm(denominator);
        const double row[3] = {1.0 / target, std::cos(w) / target, std::cos(2.0 * w) / target};
        for (int r = 0; r < 3; ++r) {
            rhs[r] += row[r];
            for (int col = 0; col < 3; ++col) {
                normal[r][col] += row[r] * row[col];
            }
        }
    }
    // Cramer's rule.
    const double det = det3(normal);
    double c[3];
    for (int k = 0; k < 3; ++k) {
        double m[3][3];
        for (int r = 0; r < 3; ++r) {
            for (int col = 0; col < 3; ++col) {
                m[r][col] = (col == k) ? rhs[r] : normal[r][col];
            }
        }
        c[k] = det3(m) / det;
    }

    // Factor c0 + c1 cos w + c2 cos 2w. With x = z + 1/z, cos w = x/2 and
    // cos 2w = x^2/2 - 1, so it is (c2/2) x^2 + (c1/2) x + (c0 - c2): a
    // quadratic whose roots give the reciprocal pairs of zeros.
    const Complex qa = c[2] / 2.0;
    const Complex qb = c[1] / 2.0;
    const Complex qc = c[0] - c[2];
    const Complex disc = std::sqrt(qb * qb - 4.0 * qa * qc);
    const Complex z0 = inner_root((-qb + disc) / (2.0 * qa));
    const Complex z1 = inner_root((-qb - disc) / (2.0 * qa));
    numerator_ = {1.0, -(z0 + z1).real(), (z0 * z1).real()};

    // Unity gain at DC.
    const double dc_numerator = numerator_[0] + numerator_[1] + numerator_[2];
    const double dc_denominator = (1.0 + pair_a1_ + pair_a2_)
                                * (1.0 - real_pole_[0]) * (1.0 - real_pole_[1]);
    gain_ = dc_denominator / dc_numerator;
}

std::array<std::complex<double>, 2> Music5000OutputFilter::zeros() const {
    const Complex disc = std::sqrt(Complex(numerator_[1] * numerator_[1]
                                           - 4.0 * numerator_[0] * numerator_[2]));
    return {(-numerator_[1] + disc) / (2.0 * numerator_[0]),
            (-numerator_[1] - disc) / (2.0 * numerator_[0])};
}

std::complex<double> Music5000OutputFilter::response(double frequency_hz) const {
    const Complex z_inv = std::polar(1.0, -2.0 * std::numbers::pi * frequency_hz / sample_rate_);
    const Complex numerator = numerator_[0] + numerator_[1] * z_inv + numerator_[2] * z_inv * z_inv;
    const Complex denominator = (1.0 + pair_a1_ * z_inv + pair_a2_ * z_inv * z_inv)
                              * (1.0 - real_pole_[0] * z_inv)
                              * (1.0 - real_pole_[1] * z_inv);
    return gain_ * numerator / denominator;
}

std::complex<double> Music5000OutputFilter::analogue_response(double frequency_hz,
                                                              const Components& components) {
    const AnaloguePoles poles = analogue_poles(components);
    const Complex s(0.0, 2.0 * std::numbers::pi * frequency_hz);
    const double w0 = poles.stage2_natural;
    return (poles.iv_node / (s + poles.iv_node))
         * (poles.stage1 / (s + poles.stage1))
         * (w0 * w0 / (s * s + s * w0 / poles.stage2_q + w0 * w0));
}

}  // namespace beebium
