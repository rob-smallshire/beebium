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

#ifndef BEEBIUM_LINUX_APP_SAVED_MACHINES_PAGE_HPP
#define BEEBIUM_LINUX_APP_SAVED_MACHINES_PAGE_HPP

#include "servers/CoreCli.hpp"

#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedLayout;

namespace beebium::linux_client {

class CardGrid;
class SavedMachines;

// "Last used <date and time>", or that the machine has not been run.
QString last_used_text(const MachineDescription& machine);

// The home window's saved machines (client specification 5.3, 13.4): a card
// per recent machine, its thumbnail, name and when it was last used, marked
// when it is running; Open, Open Other... to browse for one, Remove from
// List, and Clear. While shown it describes the machines again every few
// seconds, so the running marks follow machines started elsewhere.
class SavedMachinesPage : public QWidget {
    Q_OBJECT

public:
    static constexpr int REFRESH_MS = 3000;

    explicit SavedMachinesPage(SavedMachines& machines, QWidget* parent = nullptr);

    // The machine directory of the chosen card, or empty.
    QString chosen() const;

    CardGrid* grid() const { return grid_; }
    QPushButton* open_button() const { return open_; }
    bool showing_empty_state() const;

Q_SIGNALS:
    void open_requested(const QString& machine_dirpath);
    void browse_requested();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void rebuild();
    void update_buttons();
    void open_chosen();
    void show_context_menu(const QPoint& position);
    void show_in_file_manager(const QString& machine_dirpath);

    SavedMachines& machines_;
    QStackedLayout* stack_;
    CardGrid* grid_;
    QWidget* empty_;
    QPushButton* clear_;
    QPushButton* remove_;
    QPushButton* open_;
    QTimer refresh_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SAVED_MACHINES_PAGE_HPP
