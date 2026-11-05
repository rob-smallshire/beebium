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

#include "app/ProcessorSidebar.hpp"

#include "app/SessionSpeed.hpp"

#include <QCheckBox>
#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <cmath>

namespace beebium::linux_client {

namespace {

constexpr int GLIDE_MS = 500;  // matched to the server's publish cadence, as on macOS
constexpr double DIMMED_OPACITY = 0.4;
constexpr int LABEL_OFFSET = 16;  // label centres below the track
constexpr int CARET_OFFSET = 16;  // caret centre above the track
constexpr int LABEL_PADDING = 3;

QFont label_font(const QFont& base) {
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * 0.75);
    return font;
}

}  // namespace

SpeedScaleWidget::SpeedScaleWidget(QWidget* parent)
    : QWidget(parent), glide_(new QVariantAnimation(this)) {
    setObjectName(QStringLiteral("speedScale"));
    setFixedHeight(HEIGHT);
    setMinimumWidth(160);
    glide_->setDuration(GLIDE_MS);
    glide_->setEasingCurve(QEasingCurve::Linear);
    connect(glide_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const int drawn_x = x_for(caret_);
        caret_ = value.toDouble();
        if (x_for(caret_) != drawn_x) {
            update();
        }
    });
}

void SpeedScaleWidget::set_axis(const SpeedAxis& axis) {
    if (axis.half_span == axis_.half_span && axis.step == axis_.step) {
        return;
    }
    axis_ = axis;
    update();
}

void SpeedScaleWidget::set_configured(double multiplier, bool dimmed) {
    if (multiplier == configured_ && dimmed == dimmed_) {
        return;
    }
    configured_ = multiplier;
    dimmed_ = dimmed;
    update();
}

void SpeedScaleWidget::set_achieved(double multiplier) {
    if (multiplier == achieved_) {
        return;  // an unchanged sample does not restart the glide
    }
    achieved_ = multiplier;
    glide_->stop();
    if (x_for(multiplier) == x_for(caret_)) {
        // Within a pixel of the caret: nothing to glide, nothing to draw.
        // At a steady speed every sample differs a little, and a glide
        // restarted for each would repaint the window without end.
        caret_ = multiplier;
        return;
    }
    glide_->setStartValue(caret_);
    glide_->setEndValue(multiplier);
    glide_->start();
}

int SpeedScaleWidget::axis_y() const {
    return static_cast<int>(std::lround(HEIGHT * 0.6));
}

int SpeedScaleWidget::x_for(double multiplier) const {
    const double usable = width() - 2.0 * THUMB_RADIUS;
    return static_cast<int>(std::lround(THUMB_RADIUS + axis_.fraction_of(multiplier) * usable));
}

double SpeedScaleWidget::multiplier_at_x(double x) const {
    const double usable = std::max(1.0, width() - 2.0 * THUMB_RADIUS);
    return axis_.multiplier_at((x - THUMB_RADIUS) / usable);
}

QRect SpeedScaleWidget::label_rect(int index) const {
    const auto multipliers = axis_.label_multipliers();
    const double multiplier = multipliers.at(static_cast<std::size_t>(index));
    const QFontMetrics metrics(label_font(font()));
    const QSize text = metrics.size(Qt::TextSingleLine, speed_label(multiplier));
    QRect rect(0, 0, text.width(), text.height() + 2 * LABEL_PADDING);
    rect.moveCenter(QPoint(x_for(multiplier), axis_y() + LABEL_OFFSET));
    return rect;
}

void SpeedScaleWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor accent = palette().color(QPalette::Highlight);
    const QColor ink = palette().color(QPalette::WindowText);
    const int y = axis_y();

    // The track, a capsule 4 px tall, with a tick at each label.
    QColor track = ink;
    track.setAlphaF(0.25);
    painter.setPen(Qt::NoPen);
    painter.setBrush(track);
    painter.drawRoundedRect(QRectF(THUMB_RADIUS, y - 2, width() - 2.0 * THUMB_RADIUS, 4), 2, 2);
    QColor tick = ink;
    tick.setAlphaF(0.5);
    painter.setBrush(tick);
    painter.setFont(label_font(font()));
    const auto multipliers = axis_.label_multipliers();
    for (std::size_t i = 0; i < multipliers.size(); ++i) {
        const int x = x_for(multipliers[i]);
        painter.setPen(Qt::NoPen);
        painter.drawRect(QRectF(x - 0.5, y - 4, 1, 8));
        painter.setPen(ink);
        painter.drawText(label_rect(static_cast<int>(i)), Qt::AlignCenter,
                         speed_label(multipliers[i]));
    }

    // The achieved speed: a caret above the track, pointing down at it.
    const double caret_x = x_for(caret_);
    const double caret_y = y - CARET_OFFSET;
    QPainterPath caret;
    caret.moveTo(caret_x - 5.5, caret_y - 4);
    caret.lineTo(caret_x + 5.5, caret_y - 4);
    caret.lineTo(caret_x, caret_y + 4);
    caret.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.fillPath(caret, accent);

    // The configured speed: the thumb, dimmed while unlimited.
    painter.setOpacity(dimmed_ ? DIMMED_OPACITY : 1.0);
    painter.setBrush(accent);
    painter.drawEllipse(QPointF(x_for(configured_), y), THUMB_RADIUS, THUMB_RADIUS);
}

void SpeedScaleWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    // The band above the labels drags the thumb; below it, the labels.
    if (event->position().y() < axis_y() + 6) {
        dragging_ = true;
        configured_ = multiplier_at_x(event->position().x());
        update();
    }
}

void SpeedScaleWidget::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        configured_ = multiplier_at_x(event->position().x());
        update();
    }
}

void SpeedScaleWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (dragging_) {
        dragging_ = false;
        configured_ = multiplier_at_x(event->position().x());
        Q_EMIT chosen(configured_);
        return;
    }
    const auto multipliers = axis_.label_multipliers();
    for (std::size_t i = 0; i < multipliers.size(); ++i) {
        if (label_rect(static_cast<int>(i)).contains(event->position().toPoint())) {
            Q_EMIT chosen(multipliers[i]);
            return;
        }
    }
}

ProcessorSidebar::ProcessorSidebar(SessionSpeed& speed, QWidget* parent)
    : QWidget(parent), speed_(speed), scale_(new SpeedScaleWidget(this)),
      unlimited_(new QCheckBox(tr("Unlimited"), this)) {
    setObjectName(QStringLiteral("processorMode"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 8);
    auto* headline = new QLabel(tr("Emulation Speed"), this);
    QFont bold = headline->font();
    bold.setBold(true);
    headline->setFont(bold);
    layout->addWidget(headline);
    layout->addWidget(scale_);
    unlimited_->setToolTip(tr("Run the emulator as fast as the host allows"));
    unlimited_->setFocusPolicy(Qt::NoFocus);  // the keys stay the machine's
    layout->addWidget(unlimited_);
    layout->addStretch(1);

    connect(&speed_, &SessionSpeed::sampled, this, &ProcessorSidebar::on_sample);
    connect(scale_, &SpeedScaleWidget::chosen, this, [this](double multiplier) {
        speed_.set_multiplier(control_.apply(multiplier));
        present();
    });
    connect(unlimited_, &QCheckBox::toggled, this, [this](bool on) {
        speed_.set_multiplier(control_.set_unlimited(on));
        present();
    });
    present();
}

void ProcessorSidebar::showEvent(QShowEvent* event) {
    speed_.set_watching(true);
    QWidget::showEvent(event);
}

void ProcessorSidebar::hideEvent(QHideEvent* event) {
    speed_.set_watching(false);
    QWidget::hideEvent(event);
}

void ProcessorSidebar::on_sample(const PacingSample& sample) {
    control_.adopt(sample.configured);
    attainable_.add(sample.achieved, sample.estimated_max);
    scale_->set_axis(speed_axis_for(attainable_.value()));
    scale_->set_achieved(sample.achieved);
    present();
}

void ProcessorSidebar::present() {
    scale_->set_configured(control_.configured(), control_.unlimited());
    const QSignalBlocker quiet(unlimited_);
    unlimited_->setChecked(control_.unlimited());
}

}  // namespace beebium::linux_client
