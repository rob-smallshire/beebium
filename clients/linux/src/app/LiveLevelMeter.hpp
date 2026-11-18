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

#ifndef BEEBIUM_LINUX_APP_LIVE_LEVEL_METER_HPP
#define BEEBIUM_LINUX_APP_LIVE_LEVEL_METER_HPP

#include "app/LiveLevel.hpp"

#include <QElapsedTimer>
#include <QRect>
#include <QWidget>

namespace beebium::linux_client {

// A live input's level, as the client captures it (client specification
// 9.0, Live input): a bar in decibels of full scale, linear in dB from
// METER_FLOOR_DB at the left to 0 at the right, a tick beneath it at
// METER_TARGET_DB, a thin line at the held peak, and at the right a red
// mark while the input clips, absent otherwise.
class LiveLevelMeter : public QWidget {
public:
    static constexpr int HEIGHT = 12;
    // Redrawn some fifteen times a second at most, not for every chunk: on
    // a Pi each repaint beside the picture copies the window.
    static constexpr qint64 REDRAW_MS = 66;

    explicit LiveLevelMeter(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {120, HEIGHT}; }
    QSize minimumSizeHint() const override { return {40, HEIGHT}; }

    void take(const ChunkLevel& level);
    void reset();
    bool clip_lit() const { return state_.clip_lit(clock_.elapsed()); }

    // Where things are drawn, for tests: a level's column, the bar's band,
    // the tick's band beneath it, and the clip mark's square.
    int x_of(double db) const;
    QRect bar_rect() const;
    QRect tick_rect() const;
    QRect clip_rect() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int GAP = 4;
    // What a repaint would draw now, as pixels, to redraw only on a change.
    struct Drawn {
        int bar = -1;
        int hold = -1;
        bool clip = false;
        bool operator==(const Drawn&) const = default;
    };
    Drawn now_drawn(qint64 now) const;

    LevelMeterState state_;
    QElapsedTimer clock_;
    qint64 painted_ms_ = 0;
    Drawn drawn_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_LIVE_LEVEL_METER_HPP
