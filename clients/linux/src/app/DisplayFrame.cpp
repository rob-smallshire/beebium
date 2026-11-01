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

#include "app/DisplayFrame.hpp"

#include <QEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace beebium::linux_client {

DisplayFrame::DisplayFrame(QWidget* display, QWidget* parent) : QWidget(parent), display_(display) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(MARGIN, MARGIN, MARGIN, MARGIN);
    layout->addWidget(display_);
    display_->installEventFilter(this);
}

bool DisplayFrame::showing_focus() const {
    return !bare_ && display_->hasFocus();
}

void DisplayFrame::set_bare(bool bare) {
    bare_ = bare;
    const int margin = bare ? 0 : MARGIN;
    layout()->setContentsMargins(margin, margin, margin, margin);
    update();
}

bool DisplayFrame::eventFilter(QObject* watched, QEvent* event) {
    if (watched == display_ &&
        (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
        update();
    }
    return QWidget::eventFilter(watched, event);
}

void DisplayFrame::paintEvent(QPaintEvent*) {
    if (!showing_focus()) {
        return;
    }
    QPainter painter(this);
    QPen pen(palette().color(QPalette::Active, QPalette::Highlight));
    pen.setWidth(2);
    painter.setPen(pen);
    painter.drawRect(rect().adjusted(1, 1, -1, -1));
}

}  // namespace beebium::linux_client
