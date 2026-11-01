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

#include <QObject>
#include <QPointer>
#include <QRhiWidget>
#include <QStringList>

#include <functional>
#include <memory>
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
    // connected to it: the same endpoint, or the same machine by uuid.
    MainWindow* open(const Endpoint& endpoint, const QString& machine_uuid = {});

    // The Connect dialog; opens the chosen machine. Returns whether one was.
    bool show_connect_dialog(QWidget* parent);

    // A launch's arguments, this process's or a later one's: connect links
    // and --connect addresses open windows; none at all asks where to
    // connect, unless a window is already open (then it is brought forward).
    void handle_arguments(const QStringList& arguments);

    // Called with each new window, for options that apply to it (the
    // spike's statistics, an exit timer).
    void on_window_opened(std::function<void(MainWindow&)> hook) { window_hook_ = std::move(hook); }

    std::vector<MainWindow*> windows() const;
    ClientSettings& settings() { return settings_; }

private:
    MainWindow* find(const Endpoint& endpoint, const QString& machine_uuid) const;
    bool is_open(const DiscoveredMachine& machine) const;

    QRhiWidget::Api graphics_api_;
    ClientSettings settings_;
    std::unique_ptr<SystemPowerMonitor> power_;
    DiscoveredMachines discovery_;
    std::vector<QPointer<MainWindow>> windows_;
    std::function<void(MainWindow&)> window_hook_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_APPLICATION_HPP
