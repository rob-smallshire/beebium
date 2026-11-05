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

#ifndef BEEBIUM_LINUX_APP_SELECTION_OVERLAY_WIDGET_HPP
#define BEEBIUM_LINUX_APP_SELECTION_OVERLAY_WIDGET_HPP

#include "app/SelectionCoordinator.hpp"

#include <QColor>
#include <QFont>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QWidget>

class QPainter;

namespace beebium::linux_client {

// A selection's feedback over the picture (client specification 6.5), in
// one tint, the palette's highlight, as the macOS client draws it: the
// selected range lightly, the cells that will copy more strongly on top,
// and for anywhere the dragged marquee as a thin outline. Rects are
// widened to whole pixels. A small label beside the selection names how
// it reads, following the qualifiers as they change.
inline constexpr double RANGE_ALPHA = 0.25;
inline constexpr double TEXT_ALPHA = 0.50;
inline constexpr double MARQUEE_ALPHA = 0.50;

void paint_selection(QPainter& painter, const SelectionOverlay& overlay, const QColor& tint);

// The name of how a selection reads, shown beside it while it lasts.
QString interpretation_label(Interpretation interpretation);

// Where a label of this size goes: behind the corner the drag began from,
// clear of the selection, flipped to the selection's other side or beside
// it where the widget's edge leaves no room, and always inside the widget.
// Only a selection with no room around it at all is overlapped.
QRectF interpretation_label_rect(const SelectionOverlay& overlay, QSizeF label, QSizeF widget);

// The label, in the tint with its text in `ink`, in the small font.
void paint_interpretation_label(QPainter& painter, const SelectionOverlay& overlay,
                                QSizeF widget, const QColor& tint, const QColor& ink,
                                const QFont& font);

// The overlay as a transparent child of the display: it takes no input,
// so the mouse goes on to the display beneath it, and shows only while a
// selection lasts.
class SelectionOverlayWidget : public QWidget {
    Q_OBJECT

public:
    SelectionOverlayWidget(const SelectionCoordinator& coordinator, QWidget* display);

    // The selection changed: show what it now is.
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const SelectionCoordinator& coordinator_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SELECTION_OVERLAY_WIDGET_HPP
