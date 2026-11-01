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

#ifndef BEEBIUM_LINUX_APP_DISPLAY_FRAME_HPP
#define BEEBIUM_LINUX_APP_DISPLAY_FRAME_HPP

#include <QWidget>

namespace beebium::linux_client {

// A margin around the display that shows whether it has keyboard focus
// (client specification 7.1): a frame in the desktop's highlight colour,
// the colour its own focus indication uses, while the display has focus;
// nothing otherwise.
class DisplayFrame : public QWidget {
    Q_OBJECT

public:
    static constexpr int MARGIN = 3;

    DisplayFrame(QWidget* display, QWidget* parent = nullptr);

    bool showing_focus() const;
    // Immersive: the picture to the edges, with no margin and no focus
    // frame (client specification 6.4).
    void set_bare(bool bare);
    bool bare() const { return bare_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QWidget* display_;
    bool bare_ = false;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_DISPLAY_FRAME_HPP
