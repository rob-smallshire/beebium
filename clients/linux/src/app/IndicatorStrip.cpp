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

#include "app/IndicatorStrip.hpp"

#include "app/LedColour.hpp"
#include "app/SessionIndicators.hpp"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace beebium::linux_client {

LedIndicator::LedIndicator(const IndicatorInfo& info, QWidget* parent)
    : QWidget(parent),
      label_(QString::fromStdString(info.label)),
      related_key_(QString::fromStdString(info.related_key)),
      colour_(led_colour(QString::fromStdString(info.color))),
      outline_(led_outline(colour_)) {
    setObjectName(QString::fromStdString(info.name));
    setToolTip(clickable() ? tr("Click to toggle %1").arg(label_) : label_);
    if (clickable()) {
        setCursor(Qt::PointingHandCursor);
    }
}

void LedIndicator::set_value(int value) {
    if (value != value_) {
        value_ = value;
        update();
    }
}

QSize LedIndicator::sizeHint() const {
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    return {DIAMETER + 4 + QFontMetrics(small).horizontalAdvance(label_) + 2,
            std::max(DIAMETER + 2, QFontMetrics(small).height())};
}

void LedIndicator::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF circle(1, (height() - DIAMETER) / 2.0, DIAMETER, DIAMETER);
    // The annulus: thickness R * fill, so 0 is a hairline ring and 255 a disc.
    const double outer = DIAMETER / 2.0 - 0.5;
    const double inner = outer * (1.0 - fill_fraction(value_));
    QPainterPath ring;
    ring.addEllipse(circle.center(), outer, outer);
    if (inner > 0.5) {
        ring.addEllipse(circle.center(), inner, inner);
    }
    ring.setFillRule(Qt::OddEvenFill);
    painter.fillPath(ring, colour_);
    painter.setPen(QPen(outline_, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(circle.adjusted(0.5, 0.5, -0.5, -0.5));
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    painter.setFont(small);
    painter.setPen(palette().color(QPalette::Disabled, QPalette::WindowText));
    painter.drawText(QRectF(DIAMETER + 5, 0, width() - DIAMETER - 5, height()),
                     Qt::AlignLeft | Qt::AlignVCenter, label_);
}

void LedIndicator::mouseReleaseEvent(QMouseEvent* event) {
    if (clickable() && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        Q_EMIT clicked(related_key_);
    }
    QWidget::mouseReleaseEvent(event);
}

IndicatorStrip::IndicatorStrip(SessionIndicators& indicators, QWidget* parent)
    : QWidget(parent), indicators_(indicators) {
    setObjectName(QStringLiteral("indicators"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    connect(&indicators_, &SessionIndicators::listed, this, &IndicatorStrip::rebuild);
    connect(&indicators_, &SessionIndicators::value_changed, this,
            [this](const QString& name, int value) {
                if (LedIndicator* led = leds_.value(name)) {
                    led->set_value(value);
                }
            });
    rebuild();
}

void IndicatorStrip::rebuild() {
    for (LedIndicator* led : leds_) {
        delete led;
    }
    leds_.clear();
    for (const IndicatorInfo& info : indicators_.indicators()) {
        auto* led = new LedIndicator(info, this);
        led->set_value(indicators_.value(QString::fromStdString(info.name)));
        connect(led, &LedIndicator::clicked, this, &IndicatorStrip::key_tapped);
        layout()->addWidget(led);
        leds_.insert(QString::fromStdString(info.name), led);
    }
}

}  // namespace beebium::linux_client
