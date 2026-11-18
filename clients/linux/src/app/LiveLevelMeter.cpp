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

#include "app/LiveLevelMeter.hpp"

#include <QPainter>

#include <cmath>

namespace beebium::linux_client {

namespace {

// The colour of clipping, as of recording.
const QColor CLIP_COLOUR(0xd7, 0x26, 0x1e);

}  // namespace

LiveLevelMeter::LiveLevelMeter(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(HEIGHT);
    clock_.start();
}

void LiveLevelMeter::take(const ChunkLevel& level) {
    const qint64 now = clock_.elapsed();
    state_.take(level, now);
    if (now_drawn(now) != drawn_ && now - painted_ms_ >= REDRAW_MS) {
        painted_ms_ = now;
        update();
    }
}

void LiveLevelMeter::reset() {
    state_.reset();
    clock_.restart();
    painted_ms_ = 0;
    update();
}

int LiveLevelMeter::x_of(double db) const {
    const QRect bar = bar_rect();
    return bar.left() + static_cast<int>(std::lround(meter_fraction(db) * (bar.width() - 1)));
}

QRect LiveLevelMeter::bar_rect() const {
    return {0, 2, std::max(1, width() - HEIGHT - GAP), 6};
}

QRect LiveLevelMeter::tick_rect() const {
    const QRect bar = bar_rect();
    return {x_of(METER_TARGET_DB), bar.bottom() + 1, 1, HEIGHT - bar.bottom() - 1};
}

QRect LiveLevelMeter::clip_rect() const {
    return {width() - HEIGHT, 0, HEIGHT, HEIGHT};
}

LiveLevelMeter::Drawn LiveLevelMeter::now_drawn(qint64 now) const {
    Drawn drawn;
    const double bar = state_.bar_db(now);
    const double hold = state_.hold_db(now);
    drawn.bar = bar > METER_FLOOR_DB ? x_of(bar) : -1;
    drawn.hold = hold > METER_FLOOR_DB ? x_of(hold) : -1;
    drawn.clip = state_.clip_lit(now);
    return drawn;
}

void LiveLevelMeter::paintEvent(QPaintEvent*) {
    drawn_ = now_drawn(clock_.elapsed());
    QPainter painter(this);
    const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
    const QRect bar = bar_rect();
    painter.fillRect(bar, palette().color(group, QPalette::Mid));
    if (drawn_.bar >= 0) {
        painter.fillRect(QRect(bar.left(), bar.top(), drawn_.bar - bar.left() + 1, bar.height()),
                         palette().color(group, QPalette::Highlight));
    }
    const QColor ink = palette().color(group, QPalette::WindowText);
    painter.fillRect(tick_rect(), ink);
    // The held peak, the line the source's volume is set by, standing
    // above and below the bar.
    if (drawn_.hold >= 0) {
        painter.fillRect(QRect(drawn_.hold, 0, 1, bar.bottom() + 2), ink);
    }
    if (drawn_.clip) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(CLIP_COLOUR);
        painter.drawEllipse(QRectF(clip_rect()).adjusted(1.0, 1.0, -1.0, -1.0));
    }
}

}  // namespace beebium::linux_client
