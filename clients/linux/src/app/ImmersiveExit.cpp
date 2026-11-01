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

#include "app/ImmersiveExit.hpp"

#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QPushButton>

namespace beebium::linux_client {

ImmersiveExit::ImmersiveExit(QWidget* display) : QWidget(display), display_(display) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    button_ = new QPushButton(tr("Exit Immersive Mode (F11)"), this);
    button_->setObjectName(QStringLiteral("exitImmersive"));
    // Never take keyboard focus from the display: the keys stay the machine's.
    button_->setFocusPolicy(Qt::NoFocus);
    sidebar_button_ = new QPushButton(tr("Sidebar"), this);
    sidebar_button_->setObjectName(QStringLiteral("immersiveSidebar"));
    status_bar_button_ = new QPushButton(tr("Status Bar"), this);
    status_bar_button_->setObjectName(QStringLiteral("immersiveStatusBar"));
    for (QPushButton* toggle : {sidebar_button_, status_bar_button_}) {
        toggle->setCheckable(true);
        toggle->setFocusPolicy(Qt::NoFocus);
    }
    layout->addStretch(1);
    layout->addWidget(sidebar_button_);
    layout->addWidget(status_bar_button_);
    layout->addSpacing(12);
    layout->addWidget(button_);
    layout->addStretch(1);
    connect(button_, &QPushButton::clicked, this, &ImmersiveExit::exit_requested);
    connect(sidebar_button_, &QPushButton::clicked, this, &ImmersiveExit::sidebar_toggled);
    connect(status_bar_button_, &QPushButton::clicked, this, &ImmersiveExit::status_bar_toggled);
    rest_timer_.setSingleShot(true);
    rest_timer_.setInterval(REST_MS);
    connect(&rest_timer_, &QTimer::timeout, this, &ImmersiveExit::rest);
    display_->installEventFilter(this);
    hide();
}

void ImmersiveExit::show_overlay_state(bool sidebar, bool status_bar) {
    sidebar_button_->setChecked(sidebar);
    status_bar_button_->setChecked(status_bar);
}

bool ImmersiveExit::bar_shown() const {
    return isVisible();
}

void ImmersiveExit::set_active(bool active) {
    active_ = active;
    display_->setMouseTracking(active);
    if (active) {
        // Shown at first, so the way out is seen before it is needed.
        reveal();
    } else {
        rest_timer_.stop();
        hide();
        display_->unsetCursor();
        pointer_hidden_ = false;
    }
}

void ImmersiveExit::reveal() {
    place();
    show();
    raise();
    display_->unsetCursor();
    pointer_hidden_ = false;
    rest_timer_.start();
}

void ImmersiveExit::rest() {
    if (!active_ || pointer_over_bar()) {
        if (active_) {
            rest_timer_.start();  // not while the pointer is on the button
        }
        return;
    }
    hide();
    display_->setCursor(Qt::BlankCursor);
    pointer_hidden_ = true;
}

bool ImmersiveExit::pointer_over_bar() const {
    return isVisible() && rect().contains(mapFromGlobal(QCursor::pos()));
}

void ImmersiveExit::place() {
    setGeometry(0, 0, display_->width(), sizeHint().height());
}

bool ImmersiveExit::eventFilter(QObject* watched, QEvent* event) {
    if (watched == display_ && active_) {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress) {
            reveal();
        } else if (event->type() == QEvent::Resize && isVisible()) {
            place();
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace beebium::linux_client
