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

#include "app/ColourChoice.hpp"

#include <QColorDialog>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QToolButton>

namespace beebium::linux_client {

QToolButton* make_reset_button(QWidget* parent, const QString& object_name) {
    auto* button = new QToolButton(parent);
    button->setObjectName(object_name);
    button->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo"),
                                     parent->style()->standardIcon(QStyle::SP_BrowserReload)));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);  // the keys stay the machine's
    button->setToolTip(QObject::tr("Reset to default"));
    return button;
}

ColourChoice::ColourChoice(const QString& object_name, const QString& title, Rgb fallback,
                           QWidget* parent)
    : QObject(parent), title_(title), fallback_(fallback), colour_(fallback),
      chooser_([parent](const QColor& current, const QString& heading) -> std::optional<QColor> {
          const QColor chosen = QColorDialog::getColor(current, parent, heading);
          return chosen.isValid() ? std::optional<QColor>(chosen) : std::nullopt;
      }),
      well_(new QToolButton(parent)),
      reset_(make_reset_button(parent, object_name + QStringLiteral("Reset"))) {
    well_->setObjectName(object_name);
    well_->setFocusPolicy(Qt::NoFocus);
    well_->setIconSize(QSize(36, 14));
    well_->setToolTip(title);
    connect(well_, &QToolButton::clicked, this, [this] {
        if (const auto picked = chooser_(QColor(colour_.r, colour_.g, colour_.b), title_)) {
            // Opaque: any alpha is dropped.
            choose(Rgb{static_cast<std::uint8_t>(picked->red()),
                       static_cast<std::uint8_t>(picked->green()),
                       static_cast<std::uint8_t>(picked->blue())});
        }
    });
    connect(reset_, &QToolButton::clicked, this, [this] { choose(fallback_); });
    well_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(well_, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QMenu menu(well_);
        QAction* reset = menu.addAction(tr("Reset to Default"));
        reset->setEnabled(!(colour_ == fallback_));
        if (menu.exec(well_->mapToGlobal(at)) == reset) {
            choose(fallback_);
        }
    });
    set_colour(fallback);
}

void ColourChoice::set_colour(Rgb colour) {
    colour_ = colour;
    reset_->setEnabled(!(colour_ == fallback_));
    QPixmap swatch(well_->iconSize());
    swatch.fill(QColor(colour.r, colour.g, colour.b));
    QPainter frame(&swatch);
    frame.setPen(well_->palette().color(QPalette::Mid));
    frame.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
    frame.end();
    well_->setIcon(QIcon(swatch));
}

void ColourChoice::choose(Rgb colour) {
    if (colour == colour_) {
        return;
    }
    set_colour(colour);
    Q_EMIT chosen(colour);
}

}  // namespace beebium::linux_client
