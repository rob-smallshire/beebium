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

#include "app/MainWindow.hpp"

#include <QMenuBar>
#include <QWidget>

namespace beebium::linux_client {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Beebium"));

    // The menus come from the client specification; the bar exists now so
    // the window has its final shape. menuBar() creates it.
    menuBar();

    auto* display_placeholder = new QWidget(this);
    display_placeholder->setObjectName(QStringLiteral("displayPlaceholder"));
    display_placeholder->setMinimumSize(640, 512);
    setCentralWidget(display_placeholder);
}

}  // namespace beebium::linux_client
