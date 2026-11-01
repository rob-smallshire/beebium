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

#ifndef BEEBIUM_LINUX_APP_IMMERSIVE_EXIT_HPP
#define BEEBIUM_LINUX_APP_IMMERSIVE_EXIT_HPP

#include <QTimer>
#include <QWidget>

class QPushButton;

namespace beebium::linux_client {

// The way out of immersive mode that needs no shortcut (client
// specification 6.4), where the macOS client has its menu bar sliding down:
// moving the mouse shows an "Exit Immersive Mode" bar at the top of the
// picture, and the pointer; both hide again after three seconds' rest, as
// the macOS pointer does. Lives over the display, watching its mouse.
class ImmersiveExit : public QWidget {
    Q_OBJECT

public:
    static constexpr int REST_MS = 3000;

    explicit ImmersiveExit(QWidget* display);

    // Immersive mode began or ended.
    void set_active(bool active);
    bool active() const { return active_; }
    bool bar_shown() const;
    bool pointer_hidden() const { return pointer_hidden_; }
    // The bar's toggles for the surfaces that stay reachable while
    // immersive, over the picture rather than reflowing it (6.4).
    void show_overlay_state(bool sidebar, bool status_bar);

Q_SIGNALS:
    void exit_requested();
    void sidebar_toggled();
    void status_bar_toggled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void reveal();
    void rest();
    void place();
    bool pointer_over_bar() const;

    QWidget* display_;
    QPushButton* button_;
    QPushButton* sidebar_button_;
    QPushButton* status_bar_button_;
    QTimer rest_timer_;
    bool active_ = false;
    bool pointer_hidden_ = false;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_IMMERSIVE_EXIT_HPP
