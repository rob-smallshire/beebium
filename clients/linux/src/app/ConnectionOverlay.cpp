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

#include "app/ConnectionOverlay.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace beebium::linux_client {

OverlayContent overlay_content(ConnectionVerdict verdict, const QString& machine, int attempt) {
    OverlayContent content;
    switch (verdict) {
    case ConnectionVerdict::live:
        return content;
    case ConnectionVerdict::unreachable:
        content.title = QObject::tr("Connection to %1 lost").arg(machine);
        content.detail = QObject::tr("Trying to reconnect...");
        content.spinner = true;
        content.close = true;
        break;
    case ConnectionVerdict::reconnecting:
        content.title = QObject::tr("Reconnecting to %1").arg(machine);
        content.detail = QObject::tr("Attempt %1 of %2.")
                             .arg(attempt)
                             .arg(ReconnectPolicy::MAX_ATTEMPTS);
        content.spinner = true;
        content.close = true;
        break;
    case ConnectionVerdict::could_not_reconnect:
        content.title = QObject::tr("Could not reconnect to %1").arg(machine);
        content.detail = QObject::tr("The machine did not answer.");
        content.retry = true;
        content.close = true;
        break;
    case ConnectionVerdict::stopped:
        content.title = QObject::tr("%1 stopped").arg(machine);
        content.detail = QObject::tr("The emulator was shut down.");
        content.close = true;
        break;
    case ConnectionVerdict::died:
        content.title = QObject::tr("%1 stopped unexpectedly").arg(machine);
        content.detail = QObject::tr("The emulator ended without shutting down.");
        content.close = true;
        break;
    }
    content.visible = true;
    return content;
}

ConnectionOverlay::ConnectionOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);

    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("connectionPanel"));
    panel->setAutoFillBackground(true);
    auto* layout = new QVBoxLayout(panel);
    title_ = new QLabel(panel);
    QFont title_font = title_->font();
    title_font.setPointSizeF(title_font.pointSizeF() * 1.3);
    title_font.setBold(true);
    title_->setFont(title_font);
    title_->setAlignment(Qt::AlignHCenter);
    title_->setWordWrap(true);
    detail_ = new QLabel(panel);
    detail_->setAlignment(Qt::AlignHCenter);
    detail_->setWordWrap(true);
    spinner_ = new QProgressBar(panel);
    spinner_->setRange(0, 0);  // busy
    spinner_->setTextVisible(false);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    retry_ = new QPushButton(tr("Retry"), panel);
    close_ = new QPushButton(tr("Close"), panel);
    buttons->addWidget(retry_);
    buttons->addWidget(close_);
    buttons->addStretch(1);
    layout->addWidget(title_);
    layout->addWidget(detail_);
    layout->addWidget(spinner_);
    layout->addLayout(buttons);

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(panel, 0);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(1);

    connect(retry_, &QPushButton::clicked, this, &ConnectionOverlay::retry_requested);
    connect(close_, &QPushButton::clicked, this, &ConnectionOverlay::close_requested);
    hide();
}

void ConnectionOverlay::show_content(const OverlayContent& content) {
    title_->setText(content.title);
    detail_->setText(content.detail);
    spinner_->setVisible(content.spinner);
    retry_->setVisible(content.retry);
    close_->setVisible(content.close);
    setVisible(content.visible);
    if (content.visible) {
        raise();
    }
}

void ConnectionOverlay::paintEvent(QPaintEvent*) {
    // Dim the frozen picture so the panel reads as a state, not a dialog.
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 110));
}

}  // namespace beebium::linux_client
