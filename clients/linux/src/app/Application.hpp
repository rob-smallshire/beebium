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

#ifndef BEEBIUM_LINUX_APP_APPLICATION_HPP
#define BEEBIUM_LINUX_APP_APPLICATION_HPP

#include "app/ClientSettings.hpp"
#include "app/DiscoveredMachines.hpp"
#include "connection/Endpoint.hpp"
#include "servers/CoreCli.hpp"
#include "servers/ServerLauncher.hpp"
#include "servers/ServerLocator.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRhiWidget>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace beebium::linux_client {

class MainWindow;
class SystemPowerMonitor;

// The client: its settings, discovery, the power monitor, and every machine
// window, one per connection (client specification 4.1, 4.5).
class Application : public QObject {
    Q_OBJECT

public:
    explicit Application(QRhiWidget::Api graphics_api, QObject* parent = nullptr);
    ~Application() override;

    // Open a window on a server, or bring forward the window already
    // connected to it: the same endpoint, or the same machine by uuid. A
    // server this client launched is owned by the window it opens in (4.5).
    MainWindow* open(const Endpoint& endpoint, const QString& machine_uuid = {},
                     const std::optional<LaunchedServer>& launched = std::nullopt,
                     bool run_on_connect = false);

    // File > New Machine: choose a preset, launch it, open it (5.1).
    void new_machine(QWidget* parent);
    // File > Open Machine: choose a saved machine directory, launch it (5.3).
    void open_machine(QWidget* parent);
    // Launch a saved machine: its directory, or the document file inside it.
    void launch_saved_machine(const QString& path, QWidget* parent);
    // Launch the preset with this id, as New Machine would (for the command
    // line and scripts).
    void launch_preset_by_id(const QString& id, QWidget* parent);
    // Tools > Configure Beebium.
    void configure(QWidget* parent);

    // The Connect dialog; opens the chosen machine. Returns whether one was.
    bool show_connect_dialog(QWidget* parent);

    // A launch's arguments, this process's or a later one's: connect links
    // and --connect addresses open windows, --new-machine <preset id> and a
    // saved machine's path launch one; none at all asks where to connect,
    // unless a window is already open (then it is brought forward).
    void handle_arguments(const QStringList& arguments);

    // Called with each new window, for options that apply to it (the
    // spike's statistics, an exit timer).
    void on_window_opened(std::function<void(MainWindow&)> hook) { window_hook_ = std::move(hook); }

    std::vector<MainWindow*> windows() const;
    ClientSettings& settings() { return settings_; }

private:
    // This host's servers, or nullopt having told the user (with a way to
    // set where they are).
    std::optional<ServerLocation> servers(QWidget* parent);
    void launch_preset(const ServerLocation& servers, const PresetListing& preset, QWidget* parent);
    void launch(const QString& executable_filepath, const QStringList& arguments,
                const QString& provenance_uuid, const QString& what, QWidget* parent);
    MainWindow* find(const Endpoint& endpoint, const QString& machine_uuid) const;
    bool is_open(const DiscoveredMachine& machine) const;

    QRhiWidget::Api graphics_api_;
    ClientSettings settings_;
    std::unique_ptr<SystemPowerMonitor> power_;
    DiscoveredMachines discovery_;
    std::vector<QPointer<MainWindow>> windows_;
    std::function<void(MainWindow&)> window_hook_;
    CoreCli cli_;
    ServerLauncher launcher_;
    // Per preset, the next instance number this run: never reused (5.7).
    QHash<QString, int> next_ordinal_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_APPLICATION_HPP
