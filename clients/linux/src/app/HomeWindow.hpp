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

#ifndef BEEBIUM_LINUX_APP_HOME_WINDOW_HPP
#define BEEBIUM_LINUX_APP_HOME_WINDOW_HPP

#include "app/RunningMachinesPage.hpp"

#include <QMainWindow>

class QListWidget;
class QStackedWidget;

namespace beebium::linux_client {

class DiscoveredMachines;
class PresetsPage;
class SavedMachines;
class SavedMachinesPage;

// The home window's pages, in their order (client specification 13.4).
enum class HomePage { presets, saved_machines, running_machines };

// The page shown first: saved machines for a user with some among the
// recents, else the presets (13.4, after the macOS welcome window).
HomePage initial_home_page(bool has_recent_machines);

// The About box, from the home window or a machine window.
void show_about_beebium(QWidget* parent);

// The client's own window, on a platform without an application menu bar
// (13.4): an ordinary, persistent window in the manner of a virtual machine
// manager. Its pages are the presets with Launch, the saved machines with
// Open, and the running machines with Connect; its menus hold the
// application's commands: Configure, About, Quit. Closing it only hides it;
// the client decides whether that ends the client.
class HomeWindow : public QMainWindow {
    Q_OBJECT

public:
    HomeWindow(QString thumbnail_cache_dirpath, SavedMachines& saved_machines,
               DiscoveredMachines& discovered, RunningMachinesPage::IsOpen is_open,
               QWidget* parent = nullptr);

    // Show the window, on this page, bringing it forward.
    void show_page(HomePage page);
    HomePage page() const;

    PresetsPage* presets_page() const { return presets_; }
    SavedMachinesPage* saved_machines_page() const { return saved_; }
    RunningMachinesPage* running_machines_page() const { return running_; }

Q_SIGNALS:
    void configure_requested();
    void new_machine_requested();
    void quit_requested();
    // The window was closed (only hidden).
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void build_menus();
    QWidget* build_header();

    QListWidget* pages_list_;
    QStackedWidget* pages_;
    PresetsPage* presets_;
    SavedMachinesPage* saved_;
    RunningMachinesPage* running_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_HOME_WINDOW_HPP
