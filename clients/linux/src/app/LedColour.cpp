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

#include "app/LedColour.hpp"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>

namespace beebium::linux_client {

namespace {

struct Lab {
    double l, a, b;
};

// sRGB and CIE LAB (D65), the standard formulae.
double to_linear(double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double from_linear(double c) {
    return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

constexpr double XN = 0.95047, YN = 1.0, ZN = 1.08883;

double lab_f(double t) {
    return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0;
}

double lab_f_inverse(double f) {
    const double cubed = f * f * f;
    return cubed > 216.0 / 24389.0 ? cubed : (116.0 * f - 16.0) * 27.0 / 24389.0;
}

Lab to_lab(const QColor& colour) {
    const double r = to_linear(colour.redF());
    const double g = to_linear(colour.greenF());
    const double b = to_linear(colour.blueF());
    const double x = 0.4124 * r + 0.3576 * g + 0.1805 * b;
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const double z = 0.0193 * r + 0.1192 * g + 0.9505 * b;
    const double fx = lab_f(x / XN), fy = lab_f(y / YN), fz = lab_f(z / ZN);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

QColor from_lab(const Lab& lab) {
    const double fy = (lab.l + 16.0) / 116.0;
    const double fx = fy + lab.a / 500.0;
    const double fz = fy - lab.b / 200.0;
    const double x = XN * lab_f_inverse(fx), y = YN * lab_f_inverse(fy), z = ZN * lab_f_inverse(fz);
    const double r = 3.2406 * x - 1.5372 * y - 0.4986 * z;
    const double g = -0.9689 * x + 1.8758 * y + 0.0415 * z;
    const double b = 0.0557 * x - 0.2040 * y + 1.0570 * z;
    const auto clamp = [](double c) { return std::clamp(from_linear(std::max(c, 0.0)), 0.0, 1.0); };
    return QColor::fromRgbF(static_cast<float>(clamp(r)), static_cast<float>(clamp(g)),
                            static_cast<float>(clamp(b)));
}

// The visible spectrum to RGB, the approximation the macOS client uses.
QColor wavelength_rgb(double lambda) {
    double r = 0, g = 0, b = 0;
    if (lambda >= 380 && lambda < 440) {
        r = -(lambda - 440) / (440 - 380);
        b = 1;
    } else if (lambda >= 440 && lambda < 490) {
        g = (lambda - 440) / (490 - 440);
        b = 1;
    } else if (lambda >= 490 && lambda < 510) {
        g = 1;
        b = -(lambda - 510) / (510 - 490);
    } else if (lambda >= 510 && lambda < 580) {
        r = (lambda - 510) / (580 - 510);
        g = 1;
    } else if (lambda >= 580 && lambda < 645) {
        r = 1;
        g = -(lambda - 645) / (645 - 580);
    } else if (lambda >= 645 && lambda <= 780) {
        r = 1;
    }
    return QColor::fromRgbF(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
}

// The macOS client's lightness per wavelength, tuned by eye so LEDs of
// different hues look equally bright.
double target_lightness(double lambda) {
    static constexpr std::array<std::pair<double, double>, 7> points{{
        {380, 60}, {480, 65}, {520, 75}, {568, 75}, {590, 80}, {625, 65}, {780, 60},
    }};
    if (lambda <= points.front().first) {
        return points.front().second;
    }
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        if (lambda <= points[i + 1].first) {
            const double t = (lambda - points[i].first) / (points[i + 1].first - points[i].first);
            return points[i].second + t * (points[i + 1].second - points[i].second);
        }
    }
    return points.back().second;
}

QStringList arguments(const QString& text) {
    const int open = text.indexOf(QLatin1Char('('));
    const int close = text.lastIndexOf(QLatin1Char(')'));
    if (open < 0 || close <= open) {
        return {};
    }
    QString inside = text.mid(open + 1, close - open - 1);
    inside.replace(QLatin1Char(','), QLatin1Char(' ')).remove(QLatin1Char('%'));
    return inside.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

}  // namespace

QColor led_colour(const QString& metadata) {
    const QString text = metadata.trimmed().toLower();
    if (text.startsWith(QLatin1Char('#'))) {
        const QColor colour(text);
        return colour.isValid() ? colour : QColor(Qt::gray);
    }
    if (text.startsWith(QLatin1String("rgb("))) {
        const QStringList parts = arguments(text);
        if (parts.size() >= 3) {
            return QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
        }
        return QColor(Qt::gray);
    }
    if (text.startsWith(QLatin1String("hsl("))) {
        const QStringList parts = arguments(text);
        if (parts.size() >= 3) {
            return QColor::fromHslF(static_cast<float>(std::fmod(parts[0].toDouble(), 360.0) / 360.0),
                                    static_cast<float>(parts[1].toDouble() / 100.0),
                                    static_cast<float>(parts[2].toDouble() / 100.0));
        }
        return QColor(Qt::gray);
    }
    if (text.endsWith(QLatin1String("nm"))) {
        bool ok = false;
        const double lambda = text.chopped(2).trimmed().toDouble(&ok);
        if (!ok) {
            return QColor(Qt::gray);
        }
        Lab lab = to_lab(wavelength_rgb(lambda));
        lab.l = target_lightness(lambda);
        return from_lab(lab);
    }
    return QColor(Qt::gray);
}

QColor led_outline(const QColor& colour) {
    Lab lab = to_lab(colour);
    lab.l = std::max(0.0, lab.l - 20.0);
    return from_lab(lab);
}

}  // namespace beebium::linux_client
