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

#ifndef BEEBIUM_LINUX_APP_PROCESSOR_SIDEBAR_HPP
#define BEEBIUM_LINUX_APP_PROCESSOR_SIDEBAR_HPP

#include "app/SpeedScale.hpp"

#include <QWidget>

class QCheckBox;
class QVariantAnimation;

namespace beebium::linux_client {

class SessionSpeed;
struct PacingSample;

// The emulation speed scale, drawn as the macOS client's: a log2 track with
// 1x at its centre and a label at each power of two, a thumb at the
// configured speed (dimmed while unlimited), and a caret above the track at
// the achieved speed, gliding to each new sample. Dragging the thumb moves
// it freely and chooses the speed when let go; clicking a label chooses that
// power of two.
class SpeedScaleWidget : public QWidget {
    Q_OBJECT

public:
    static constexpr int HEIGHT = 72;
    static constexpr int THUMB_RADIUS = 9;

    explicit SpeedScaleWidget(QWidget* parent = nullptr);

    void set_axis(const SpeedAxis& axis);
    const SpeedAxis& axis() const { return axis_; }
    void set_configured(double multiplier, bool dimmed);
    double configured() const { return configured_; }
    bool thumb_dimmed() const { return dimmed_; }
    // The achieved speed the caret is gliding to.
    void set_achieved(double multiplier);
    double achieved() const { return achieved_; }

    int axis_y() const;
    int x_for(double multiplier) const;
    // The hit area of the label at index i of the axis's label multipliers.
    QRect label_rect(int index) const;

Q_SIGNALS:
    void chosen(double multiplier);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    double multiplier_at_x(double x) const;

    SpeedAxis axis_ = speed_axis_for(1.0);
    double configured_ = 1.0;
    bool dimmed_ = false;
    double achieved_ = 1.0;
    double caret_ = 1.0;  // where the caret is drawn, on its way to achieved_
    bool dragging_ = false;
    QVariantAnimation* glide_;
};

// The Processor mode (client specification 5.6, 13.1): the emulation speed.
// It watches the pacing statistics while it shows, adopts the server's
// speed from the first of them, and sends each speed chosen.
class ProcessorSidebar : public QWidget {
    Q_OBJECT

public:
    explicit ProcessorSidebar(SessionSpeed& speed, QWidget* parent = nullptr);

    SpeedScaleWidget* scale() const { return scale_; }
    QCheckBox* unlimited() const { return unlimited_; }

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void on_sample(const PacingSample& sample);
    void present();

    SessionSpeed& speed_;
    SpeedControl control_;
    AttainableSpeed attainable_;
    SpeedScaleWidget* scale_;
    QCheckBox* unlimited_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_PROCESSOR_SIDEBAR_HPP
