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

#include "app/SpeedScale.hpp"

#include <algorithm>
#include <cmath>

namespace beebium::linux_client {

void AttainableSpeed::add(double achieved, double estimated_max) {
    const double candidate = std::max(estimated_max, achieved);
    if (!std::isfinite(candidate)) {
        return;
    }
    samples_.push_back(std::clamp(candidate, 1.0, CEILING));
    while (samples_.size() > WINDOW) {
        samples_.pop_front();
    }
}

double AttainableSpeed::value() const {
    if (samples_.empty()) {
        return 1.0;
    }
    std::vector<double> sorted(samples_.begin(), samples_.end());
    std::sort(sorted.begin(), sorted.end());
    const auto spikes = static_cast<std::size_t>(static_cast<double>(sorted.size()) * 0.1);
    return std::max(1.0, sorted[sorted.size() - 1 - spikes]);
}

double SpeedAxis::max() const {
    return std::exp2(exponent());
}

std::vector<double> SpeedAxis::label_multipliers() const {
    std::vector<double> multipliers;
    for (int i = -half_span; i <= half_span; ++i) {
        multipliers.push_back(std::exp2(step * i));
    }
    return multipliers;
}

double SpeedAxis::fraction_of(double multiplier) const {
    if (!(multiplier > 0.0)) {
        return 0.0;
    }
    const double span = 2.0 * exponent();
    return std::clamp((std::log2(multiplier) + exponent()) / span, 0.0, 1.0);
}

double SpeedAxis::multiplier_at(double fraction) const {
    const double span = 2.0 * exponent();
    return std::exp2(std::clamp(fraction, 0.0, 1.0) * span - exponent());
}

SpeedAxis speed_axis_for(double attainable) {
    const double l = std::log2(std::max(attainable, 1.0));
    const int k7 = std::max(1, static_cast<int>(std::ceil(l / 3.0)));
    const int k9 = std::max(1, static_cast<int>(std::ceil(l / 4.0)));
    return 4 * k9 <= 3 * k7 ? SpeedAxis{4, k9} : SpeedAxis{3, k7};
}

QString speed_label(double multiplier) {
    static const QChar TIMES(0x00d7);
    if (multiplier >= 1.0) {
        if (multiplier == std::floor(multiplier)) {
            return QString::number(static_cast<long long>(multiplier)) + TIMES;
        }
        return QString::number(multiplier, 'g') + TIMES;
    }
    if (multiplier > 0.0) {
        const double inverse = 1.0 / multiplier;
        if (inverse == std::floor(inverse)) {
            return QStringLiteral("1/") + QString::number(static_cast<long long>(inverse));
        }
    }
    return QString::number(multiplier, 'g') + TIMES;
}

bool SpeedControl::adopt(double server_multiplier) {
    if (adopted_) {
        return false;
    }
    adopted_ = true;
    if (server_multiplier == 0.0) {
        unlimited_ = true;
    } else {
        configured_ = server_multiplier;
    }
    return true;
}

double SpeedControl::apply(double multiplier) {
    configured_ = multiplier;
    unlimited_ = false;
    return configured_;
}

double SpeedControl::set_unlimited(bool unlimited) {
    unlimited_ = unlimited;
    return unlimited_ ? 0.0 : configured_;
}

}  // namespace beebium::linux_client
