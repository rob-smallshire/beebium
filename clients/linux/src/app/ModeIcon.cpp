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

#include "app/ModeIcon.hpp"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

#include <algorithm>
#include <cmath>

namespace beebium::linux_client {

namespace {

QString drawing_filepath(const QString& name, int grid) {
    return QStringLiteral(":/mode-icons/%1/%2.svg").arg(grid).arg(name);
}

}  // namespace

ModeIconEngine::ModeIconEngine(QString name) : name_(std::move(name)) {}

int ModeIconEngine::grid_for(int pixels) {
    return pixels < 20 ? 16 : 24;
}

QColor ModeIconEngine::ink(QIcon::Mode mode) {
    const QPalette palette = QApplication::palette();
    switch (mode) {
    case QIcon::Disabled:
        return palette.color(QPalette::Disabled, QPalette::WindowText);
    case QIcon::Selected:
        return palette.color(QPalette::Active, QPalette::HighlightedText);
    case QIcon::Normal:
    case QIcon::Active:
        break;
    }
    return palette.color(QPalette::Active, QPalette::WindowText);
}

QPixmap ModeIconEngine::scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State,
                                     qreal scale) {
    const QSize device(static_cast<int>(std::lround(size.width() * scale)),
                       static_cast<int>(std::lround(size.height() * scale)));
    if (device.isEmpty()) {
        return {};
    }
    // The grid for the size the icon shows at, drawn at the device's pixels.
    QImageReader reader(drawing_filepath(name_, grid_for(std::min(size.width(), size.height()))));
    reader.setScaledSize(device);
    QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // The drawing's own ink is a placeholder: its coverage, in the palette's.
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), ink(mode));
    painter.end();
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(scale);
    return pixmap;
}

QPixmap ModeIconEngine::pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) {
    return scaledPixmap(size, mode, state, 1.0);
}

void ModeIconEngine::paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
                           QIcon::State state) {
    const qreal scale = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
}

QList<QSize> ModeIconEngine::availableSizes(QIcon::Mode, QIcon::State) {
    return {QSize(16, 16), QSize(24, 24)};
}

QIconEngine* ModeIconEngine::clone() const {
    return new ModeIconEngine(name_);
}

QString ModeIconEngine::key() const {
    return QStringLiteral("beebium-mode");
}

bool ModeIconEngine::isNull() {
    return !QFile::exists(drawing_filepath(name_, 24));
}

}  // namespace beebium::linux_client
