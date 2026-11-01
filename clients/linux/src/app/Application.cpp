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
#include "app/NewMachineDialog.hpp"
#include "app/ErrorPresentation.hpp"
#include "app/SettingsDialog.hpp"
#include "app/SystemPowerMonitor.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QUuid>
#include <QUrl>

#include <algorithm>
#include <cstdio>

namespace beebium::linux_client {

QStringList launch_requests(const QStringList& connect_values, const QStringList& new_machine_ids,
                            const QStringList& positional, const QString& working_dirpath) {
    QStringList requests;
    for (const QString& value : connect_values) {
        requests << QStringLiteral("--connect") << value;
    }
    for (const QString& id : new_machine_ids) {
        requests << QStringLiteral("--new-machine") << id;
    }
    for (const QString& argument : positional) {
        if (parse_connect_link(QUrl(argument))) {
            requests << argument;
        } else {
            requests << QStringLiteral("--open") << QDir(working_dirpath).absoluteFilePath(argument);
        }
    }
    return requests;
}

Application::Application(QRhiWidget::Api graphics_api, QObject* parent)
    : QObject(parent),
      graphics_api_(graphics_api),
      power_(SystemPowerMonitor::create_for_this_host()),
      launcher_(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                    .filePath(QStringLiteral("servers"))) {
    // One log per launch: keep the recent ones for diagnosis, not all.
    prune_server_logs(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                          .filePath(QStringLiteral("servers")),
                      QDateTime::currentDateTime());
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

MainWindow* Application::open(const Endpoint& endpoint, const QString& machine_uuid,
                              const std::optional<LaunchedServer>& launched, bool run_on_connect) {
    if (MainWindow* existing = find(endpoint, machine_uuid)) {
        // Never a second window on the same machine (4.1).
        std::fprintf(stderr, "beebium: %s is open already; bringing its window forward\n",
                     endpoint.to_string().c_str());
        existing->show();
        existing->raise();
        existing->activateWindow();
        return existing;
    }
    std::fprintf(stderr, "beebium: opening a window on %s\n", endpoint.to_string().c_str());
    auto* window = new MainWindow(graphics_api_, power_.get(), &settings_);
    windows_.emplace_back(window);
    connect(window, &MainWindow::connect_requested, this,
            [this, window] { show_connect_dialog(window); });
    connect(window, &MainWindow::quit_requested, this, &Application::quit_all);
    connect(window, &MainWindow::new_machine_requested, this, [this, window] { new_machine(window); });
    connect(window, &MainWindow::open_machine_requested, this, [this, window] { open_machine(window); });
    connect(window, &MainWindow::configure_requested, this, [this, window] { configure(window); });
    if (launched) {
        window->session().set_launched_server(*launched);
    }
    if (run_on_connect) {
        window->session().set_run_on_connect(true);
    }
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
    if (dialog.choice() == ConnectDialog::Choice::new_machine) {
        new_machine(parent);
        return true;
    }
    if (dialog.choice() == ConnectDialog::Choice::open_machine) {
        open_machine(parent);
        return true;
    }
    const auto endpoint = dialog.endpoint();
    if (!endpoint) {
        return false;
    }
    open(*endpoint, dialog.machine_uuid());
    return true;
}

void Application::quit_all() {
    for (MainWindow* window : windows()) {
        window->set_quitting(true);
        window->close();
    }
}

std::optional<ServerLocation> Application::servers(QWidget* parent) {
    if (auto found = locate_servers_on_this_host(settings_.servers_dirpath())) {
        return found;
    }
    QMessageBox box(QMessageBox::Warning, tr("No Beebium Servers"),
                    settings_.servers_dirpath().isEmpty()
                        ? tr("Beebium could not find its servers on this computer.")
                        : tr("There are no Beebium servers in %1.").arg(settings_.servers_dirpath()),
                    QMessageBox::Cancel, parent);
    box.setInformativeText(tr("Install the beebium-server package, or say where the servers are."));
    QAbstractButton* configure_button = box.addButton(tr("Configure..."), QMessageBox::AcceptRole);
    box.exec();
    if (box.clickedButton() == configure_button) {
        configure(parent);
        return locate_servers_on_this_host(settings_.servers_dirpath());
    }
    return std::nullopt;
}

void Application::new_machine(QWidget* parent) {
    const auto found = servers(parent);
    if (!found) {
        return;
    }
    NewMachineDialog dialog(*found,
                            QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                                .filePath(QStringLiteral("thumbnails")),
                            parent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (const auto preset = dialog.chosen()) {
        launch_preset(*found, *preset, parent);
    }
}

void Application::launch_preset(const ServerLocation& servers, const PresetListing& preset,
                                QWidget* parent) {
    const int ordinal = ++next_ordinal_[preset.id];
    const QString provenance_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    launch(servers.executable_filepath(preset.model),
           preset_launch_arguments(preset.filepath, ordinal, provenance_uuid,
                                   settings_.rom_dirpath()),
           provenance_uuid, preset.name, parent);
}

void Application::launch_preset_by_id(const QString& id, QWidget* parent) {
    const auto found = servers(parent);
    if (!found) {
        return;
    }
    QPointer<QWidget> guard(parent);
    cli_.run(found->executable_filepath(QStringLiteral("model-b")),
             {QStringLiteral("--format"), QStringLiteral("jsonl"), QStringLiteral("list-presets"),
              QStringLiteral("--all-models")},
             [this, found, id, guard](const CliResult& result) {
                 if (!result.ok()) {
                     ServerMessageDialog dialog(tr("Could Not Start"),
                                                tr("The presets could not be read."),
                                                result.standard_error, guard);
                     dialog.exec();
                     return;
                 }
                 for (const auto& preset : parse_preset_listing(result.standard_output)) {
                     if (preset.id == id) {
                         launch_preset(*found, preset, guard);
                         return;
                     }
                 }
                 ServerMessageDialog dialog(tr("Could Not Start"),
                                            tr("There is no preset called %1.").arg(id),
                                            result.standard_error, guard);
                 dialog.exec();
             });
}

void Application::open_machine(QWidget* parent) {
    const QString dirpath = QFileDialog::getExistingDirectory(
        parent, tr("Open Machine"), QDir::homePath(), QFileDialog::ShowDirsOnly);
    if (!dirpath.isEmpty()) {
        launch_saved_machine(dirpath, parent);
    }
}

void Application::launch_saved_machine(const QString& dirpath, QWidget* parent) {
    const auto found = servers(parent);
    if (!found) {
        return;
    }
    // Which model, and so which server: the directory says (5.3).
    QPointer<QWidget> guard(parent);
    cli_.run(found->executable_filepath(QStringLiteral("model-b")),
             {QStringLiteral("describe-machine"), dirpath, QStringLiteral("--format"),
              QStringLiteral("jsonl")},
             [this, found, dirpath, guard](const CliResult& result) {
                 const auto machine = result.ok() ? parse_machine_description(result.standard_output)
                                                  : std::nullopt;
                 if (!machine) {
                     ServerMessageDialog dialog(
                         tr("Could Not Open"),
                         tr("Beebium could not read a saved machine at %1.").arg(dirpath),
                         result.standard_error, guard);
                     dialog.exec();
                     return;
                 }
                 const QString provenance_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                 launch(found->executable_filepath(machine->model),
                        machine_launch_arguments(machine->machine_dirpath, provenance_uuid,
                                                 settings_.rom_dirpath()),
                        provenance_uuid, machine->name, guard);
             });
}

void Application::launch(const QString& executable_filepath, const QStringList& arguments,
                         const QString& provenance_uuid, const QString& what, QWidget* parent) {
    std::fprintf(stderr, "beebium: launching %s\n", qPrintable(what));
    QApplication::setOverrideCursor(Qt::BusyCursor);
    QPointer<QWidget> guard(parent);
    launcher_.launch(executable_filepath, arguments, provenance_uuid,
                     [this, what, guard](const LaunchResult& result) {
                         QApplication::restoreOverrideCursor();
                         if (const auto* server = std::get_if<LaunchedServer>(&result)) {
                             open(Endpoint{"127.0.0.1", static_cast<std::uint16_t>(server->port)},
                                  {}, *server);
                             return;
                         }
                         const auto& failure = std::get<LaunchFailure>(result);
                         ServerMessageDialog dialog(
                             tr("Could Not Start"),
                             failure.timed_out
                                 ? tr("%1 did not start in time, so Beebium stopped it.").arg(what)
                                 : tr("Beebium could not start %1.").arg(what),
                             failure.server_message, guard);
                         dialog.exec();
                     });
}

void Application::configure(QWidget* parent) {
    SettingsDialog dialog(settings_, parent);
    dialog.exec();
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
        } else if (argument == QLatin1String("--new-machine") && i + 1 < arguments.size()) {
            launch_preset_by_id(arguments.at(++i), nullptr);
            opened = true;
        } else if (argument == QLatin1String("--open") && i + 1 < arguments.size()) {
            // A saved machine, as the file manager opens one; one that cannot
            // be read is said so (5.3).
            launch_saved_machine(arguments.at(++i), nullptr);
            opened = true;
        } else if (auto link = parse_connect_link(QUrl(argument))) {
            // A link's run flag: Run on connect, for a machine started blocked.
            open(link->endpoint, {}, std::nullopt, link->run.has_value());
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
