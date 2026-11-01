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

#ifndef BEEBIUM_LINUX_APP_MAIN_WINDOW_HPP
#define BEEBIUM_LINUX_APP_MAIN_WINDOW_HPP

#include <QMainWindow>

namespace beebium::linux_client {

// A machine window: one per connection to a server. For now it holds an
// empty menu bar and a placeholder where the emulator display will go.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_MAIN_WINDOW_HPP
