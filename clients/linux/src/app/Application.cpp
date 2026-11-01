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

#include "app/Application.hpp"

#include "app/ConnectDialog.hpp"
#include "app/DeepLink.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"
#include "app/SystemPowerMonitor.hpp"

#include <QApplication>
#include <QUrl>

#include <algorithm>

namespace beebium::linux_client {

Application::Application(QRhiWidget::Api graphics_api, QObject* parent)
    : QObject(parent),
      graphics_api_(graphics_api),
      power_(SystemPowerMonitor::create_for_this_host()) {
    discovery_.start();
}

Application::~Application() = default;

std::vector<MainWindow*> Application::windows() const {
    std::vector<MainWindow*> open;
    for (const auto& window : windows_) {
        if (window) {
            open.push_back(window);
        }
    }
    return open;
}

MainWindow* Application::find(const Endpoint& endpoint, const QString& machine_uuid) const {
    for (MainWindow* window : windows()) {
        const MachineSession& session = window->session();
        const auto window_endpoint = parse_endpoint(session.endpoint().toStdString());
        if (window_endpoint && window_endpoint->key() == endpoint.key()) {
            return window;
        }
        if (!machine_uuid.isEmpty() && session.machine_uuid() == machine_uuid) {
            return window;
        }
    }
    return nullptr;
}

bool Application::is_open(const DiscoveredMachine& machine) const {
    return find(machine.endpoint(), machine.uuid) != nullptr;
}

MainWindow* Application::open(const Endpoint& endpoint, const QString& machine_uuid) {
    if (MainWindow* existing = find(endpoint, machine_uuid)) {
        // Never a second window on the same machine (4.1).
        existing->show();
        existing->raise();
        existing->activateWindow();
        return existing;
    }
    auto* window = new MainWindow(graphics_api_, power_.get(), &settings_);
    windows_.emplace_back(window);
    connect(window, &MainWindow::connect_requested, this,
            [this, window] { show_connect_dialog(window); });
    connect(window, &MainWindow::quit_requested, qApp, &QApplication::closeAllWindows);
    connect(&window->session(), &MachineSession::connected_to_server, this, [this, endpoint] {
        // Only connections that worked are remembered (4.1).
        settings_.remember_connection(endpoint);
    });
    window->show();
    window->connect_to(QString::fromStdString(endpoint.to_string()));
    if (window_hook_) {
        window_hook_(*window);
    }
    return window;
}

bool Application::show_connect_dialog(QWidget* parent) {
    ConnectDialog dialog(discovery_, settings_.recent_connections(),
                         [this](const DiscoveredMachine& machine) { return is_open(machine); },
                         parent);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    const auto endpoint = dialog.endpoint();
    if (!endpoint) {
        return false;
    }
    open(*endpoint, dialog.machine_uuid());
    return true;
}

void Application::handle_arguments(const QStringList& arguments) {
    bool opened = false;
    for (int i = 0; i < arguments.size(); ++i) {
        const QString& argument = arguments.at(i);
        if (argument == QLatin1String("--connect") && i + 1 < arguments.size()) {
            if (auto endpoint = parse_endpoint(arguments.at(++i).toStdString())) {
                open(*endpoint);
                opened = true;
            }
        } else if (auto link = parse_connect_link(QUrl(argument))) {
            open(link->endpoint);
            opened = true;
        }
    }
    if (opened) {
        return;
    }
    const auto open_windows = windows();
    if (!open_windows.empty()) {
        // A plain relaunch: bring the client forward rather than start over.
        open_windows.back()->raise();
        open_windows.back()->activateWindow();
        return;
    }
    if (!show_connect_dialog(nullptr) && windows().empty()) {
        // Nothing chosen and nothing open: there is no client to keep.
        // Queued, since this may run before the event loop does.
        QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    }
}

}  // namespace beebium::linux_client
