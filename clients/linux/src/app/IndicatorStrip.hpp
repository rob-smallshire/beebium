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

#ifndef BEEBIUM_LINUX_APP_INDICATOR_STRIP_HPP
#define BEEBIUM_LINUX_APP_INDICATOR_STRIP_HPP

#include "connection/IndicatorStream.hpp"

#include <QColor>
#include <QHash>
#include <QWidget>

#include <algorithm>

namespace beebium::linux_client {

class SessionIndicators;

// One LED, as the macOS client draws it: a ring whose thickness is the
// brightness, from a thin outline at 0 to a filled disc at 255 (never just
// on or off), and its label beside it. One with a related key is clickable.
class LedIndicator : public QWidget {
    Q_OBJECT

public:
    static constexpr int DIAMETER = 12;

    LedIndicator(const IndicatorInfo& info, QWidget* parent = nullptr);

    void set_value(int value);
    int value() const { return value_; }
    // The fill, 0 to 1, that a brightness draws.
    static double fill_fraction(int value) { return std::clamp(value, 0, 255) / 255.0; }
    QColor colour() const { return colour_; }
    bool clickable() const { return !related_key_.isEmpty(); }

    QSize sizeHint() const override;

Q_SIGNALS:
    void clicked(const QString& related_key);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QString label_;
    QString related_key_;
    QColor colour_;
    QColor outline_;
    int value_ = 0;
};

// The machine's LEDs in a row (client specification 10): whatever the server
// registers, in its order, rebuilt whenever it lists them. Nothing is
// hard-coded.
class IndicatorStrip : public QWidget {
    Q_OBJECT

public:
    explicit IndicatorStrip(SessionIndicators& indicators, QWidget* parent = nullptr);

    LedIndicator* led(const QString& name) const { return leds_.value(name); }
    int count() const { return static_cast<int>(leds_.size()); }

Q_SIGNALS:
    // An LED with a related key was clicked: tap that BBC key, by name.
    void key_tapped(const QString& related_key);

private:
    void rebuild();

    SessionIndicators& indicators_;
    QHash<QString, LedIndicator*> leds_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_INDICATOR_STRIP_HPP
