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
#include "app/HomeWindow.hpp"
#include "app/SavedMachines.hpp"
#include "connection/Endpoint.hpp"
#include "servers/CoreCli.hpp"
#include "servers/ServerLauncher.hpp"
#include "app/NewMachineDialog.hpp"
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

// The requests in a command line, normalised so that no option's value is
// taken for a path: the --connect and --new-machine values, then each
// positional argument as a connect link if it is one, as --preset-file for a
// .beebium-preset, else as --open, each with its absolute path. What one
// client hands another.
QStringList launch_requests(const QStringList& connect_values, const QStringList& new_machine_ids,
                            const QStringList& positional, const QString& working_dirpath);

// The client: its settings, discovery, the power monitor, the home window
// and every machine window, one per connection (client specification 4.1,
// 4.5, 13.4).
//
// The client's lifetime is its own, not Qt's last-window rule (13.4):
// closing the home window does not end it while a machine window is open;
// the last machine window closing ends it when the home window is closed,
// and leaves the home window when it is not. finished() says it has ended.
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

    // Show the home window, the one and only, bringing it forward: on the
    // page given, else where it was, else the first page for this user.
    void show_home(std::optional<HomePage> page = std::nullopt);
    // The home window, once it has been shown; null before.
    HomeWindow* home_window() const { return home_.get(); }

    // Launch a saved machine: its directory, or the document file inside it.
    void launch_saved_machine(const QString& path, QWidget* parent);
    // Launch the preset with this id, as the presets page would (for the
    // command line and scripts).
    void launch_preset_by_id(const QString& id, QWidget* parent);
    // Launch a preset file, as the file manager opens one.
    void launch_preset_file(const QString& filepath, QWidget* parent);
    // Settings > Configure Beebium.
    void configure(QWidget* parent);
    // File > New Machine...: the configuration editor, one for the
    // application, needing this host's servers (5.2).
    void new_machine(QWidget* parent);
    NewMachineDialog* new_machine_dialog() const { return new_machine_.get(); }

    // A launch's requests, this process's or a later one's, as
    // launch_requests() builds them: --connect <address>, --new-machine
    // <preset id>, --open <saved machine path> and connect links. A request
    // opens its machine's window only; none at all shows the home window
    // (4.1, 13.4).
    void handle_arguments(const QStringList& arguments);

    // Called with each new window, for options that apply to it (the
    // spike's statistics, an exit timer).
    void on_window_opened(std::function<void(MainWindow&)> hook) { window_hook_ = std::move(hook); }

    std::vector<MainWindow*> windows() const;
    SavedMachines& saved_machines() { return *saved_machines_; }

    // Quit: close every machine window as quitting, so only servers this
    // client launched and alone uses are shut down (4.5), and the home
    // window with them. A machine window whose close is cancelled stays,
    // and so does the client.
    void quit_all();
    ClientSettings& settings() { return settings_; }

Q_SIGNALS:
    // No window is open and none is on its way: the client has ended.
    void finished();

private:
    // This host's servers, or nullopt having told the user (with a way to
    // set where they are).
    std::optional<ServerLocation> servers(QWidget* parent);
    void browse_for_machine(QWidget* parent);
    void launch_preset(const ServerLocation& servers, const PresetListing& preset, QWidget* parent);
    // `after` runs once the launch has succeeded or failed.
    void launch(const QString& executable_filepath, const QStringList& arguments,
                const QString& provenance_uuid, const QString& what, QWidget* parent,
                std::function<void()> after = {});
    NewMachineHost new_machine_host();
    void describe_machine(const QString& machine_dirpath, SavedMachines::Described done);
    // Keep the recents in step with a window's saved machine.
    void track_saved_machine(MainWindow* window);
    MainWindow* find(const Endpoint& endpoint, const QString& machine_uuid) const;
    bool is_open(const DiscoveredMachine& machine) const;
    // Work on the way to a window (a launch, a describe): the client may not
    // end meanwhile.
    void begin_work();
    void end_work();
    // A request that opened nothing leaves the user somewhere: the home window.
    void after_failed_request();
    void check_finished();

    QRhiWidget::Api graphics_api_;
    ClientSettings settings_;
    std::unique_ptr<SystemPowerMonitor> power_;
    DiscoveredMachines discovery_;
    std::vector<QPointer<MainWindow>> windows_;
    std::function<void(MainWindow&)> window_hook_;
    CoreCli cli_;
    ServerLauncher launcher_;
    std::unique_ptr<NewMachineDialog> new_machine_;
    std::unique_ptr<SavedMachines> saved_machines_;
    std::unique_ptr<HomeWindow> home_;
    int work_in_flight_ = 0;
    bool finished_ = false;
    // Per preset, the next instance number this run: never reused (5.7).
    QHash<QString, int> next_ordinal_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_APPLICATION_HPP
