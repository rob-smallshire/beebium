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

#include "app/DeepLink.hpp"
#include "app/ErrorPresentation.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"
#include "app/PresetsPage.hpp"
#include "app/SavedMachinesPage.hpp"
#include "app/SettingsDialog.hpp"
#include "app/SystemPowerMonitor.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>
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
        } else if (argument.endsWith(QLatin1String(".beebium-preset"), Qt::CaseInsensitive)) {
            // A preset file, as the file manager opens one: launched as the
            // presets page would launch it.
            requests << QStringLiteral("--preset-file") << QDir(working_dirpath).absoluteFilePath(argument);
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
    // For diagnosis, every launch: the servers this client will use, and how
    // it chose them.
    std::fprintf(stderr, "beebium: %s\n",
                 qPrintable(servers_log_line(locate_servers_on_this_host(settings_.servers_dirpath()))));
    // The client decides when it ends (13.4), not the last window closing.
    QGuiApplication::setQuitOnLastWindowClosed(false);
    saved_machines_ = std::make_unique<SavedMachines>(
        settings_, [this](const QString& dirpath, SavedMachines::Described done) {
            describe_machine(dirpath, std::move(done));
        });
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
    connect(window, &MainWindow::quit_requested, this, &Application::quit_all);
    connect(window, &MainWindow::home_requested, this, [this] { show_home(); });
    // Checked from the event loop, once the window has quite gone.
    connect(window, &QObject::destroyed, this, [this] {
        QMetaObject::invokeMethod(this, &Application::check_finished, Qt::QueuedConnection);
    });
    track_saved_machine(window);
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
    if (home_) {
        // A machine window opening changes which saved machines run.
        home_->running_machines_page()->refresh();
        saved_machines_->reload();
    }
    return window;
}

void Application::track_saved_machine(MainWindow* window) {
    auto remembered = std::make_shared<QString>();
    const auto track = [this, window, remembered] {
        const MachineSession& session = window->session();
        if (!session.machine_saved() || session.machine_dirpath().isEmpty()) {
            return;
        }
        // Another host's path means nothing here: describe-machine could
        // never read it (3.2).
        if (!session.server_shares_filesystem()) {
            return;
        }
        if (*remembered == session.machine_dirpath()) {
            return;
        }
        // Moved or renamed: the old directory is no longer the machine.
        if (!remembered->isEmpty()) {
            settings_.forget_machine(*remembered);
        }
        *remembered = session.machine_dirpath();
        settings_.remember_machine(*remembered);
        saved_machines_->reload();
    };
    MachineSession* session = &window->session();
    connect(session, &MachineSession::connected_to_server, this, track);
    connect(session, &MachineSession::save_finished, this, track);
    connect(session, &MachineSession::move_finished, this, track);
    connect(session, &MachineSession::system_info_refreshed, this, track);
}

void Application::show_home(std::optional<HomePage> page) {
    if (!home_) {
        home_ = std::make_unique<HomeWindow>(
            QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                .filePath(QStringLiteral("thumbnails")),
            *saved_machines_, discovery_,
            [this](const DiscoveredMachine& machine) { return is_open(machine); });
        HomeWindow* home = home_.get();
        home->presets_page()->set_servers(locate_servers_on_this_host(settings_.servers_dirpath()));
        home->running_machines_page()->set_recents(settings_.recent_connections());
        connect(home->presets_page(), &PresetsPage::launch_requested, this,
                [this, home](const PresetListing& preset) {
                    if (const auto found = servers(home)) {
                        launch_preset(*found, preset, home);
                    }
                });
        connect(home->presets_page(), &PresetsPage::configure_requested, this,
                [this, home] { configure(home); });
        connect(home, &HomeWindow::new_machine_requested, this, [this, home] { new_machine(home); });
        connect(home->saved_machines_page(), &SavedMachinesPage::open_requested, this,
                [this, home](const QString& dirpath) { launch_saved_machine(dirpath, home); });
        connect(home->saved_machines_page(), &SavedMachinesPage::browse_requested, this,
                [this, home] { browse_for_machine(home); });
        connect(home->running_machines_page(), &RunningMachinesPage::connect_requested, this,
                [this](const Endpoint& endpoint, const QString& uuid) { open(endpoint, uuid); });
        connect(home, &HomeWindow::configure_requested, this, [this, home] { configure(home); });
        connect(home, &HomeWindow::quit_requested, this, &Application::quit_all);
        connect(home, &HomeWindow::closed, this, [this] {
            QMetaObject::invokeMethod(this, &Application::check_finished, Qt::QueuedConnection);
        });
        if (!page) {
            page = initial_home_page(saved_machines_->has_recent_machines());
        }
    }
    home_->running_machines_page()->set_recents(settings_.recent_connections());
    home_->running_machines_page()->refresh();
    if (page) {
        home_->show_page(*page);
    } else {
        home_->show_page(home_->page());
    }
}

void Application::quit_all() {
    for (MainWindow* window : windows()) {
        window->set_quitting(true);
        window->close();
    }
    // The client ends when the last machine window has gone; one whose
    // close is cancelled keeps it, and can bring the home window back.
    if (home_) {
        home_->close();
    }
    QMetaObject::invokeMethod(this, &Application::check_finished, Qt::QueuedConnection);
}

void Application::begin_work() {
    ++work_in_flight_;
    if (home_) {
        home_->presets_page()->set_launching(true);
    }
}

void Application::end_work() {
    --work_in_flight_;
    if (home_ && work_in_flight_ == 0) {
        home_->presets_page()->set_launching(false);
    }
    QMetaObject::invokeMethod(this, &Application::check_finished, Qt::QueuedConnection);
}

void Application::after_failed_request() {
    if (windows().empty() && (!home_ || !home_->isVisible())) {
        show_home();
    }
}

void Application::check_finished() {
    if (home_) {
        home_->running_machines_page()->refresh();
        if (home_->isVisible()) {
            saved_machines_->reload();
        }
    }
    if (finished_ || work_in_flight_ > 0 || !windows().empty() ||
        (home_ && home_->isVisible())) {
        return;
    }
    finished_ = true;
    Q_EMIT finished();
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

void Application::launch_preset(const ServerLocation& servers, const PresetListing& preset,
                                QWidget* parent) {
    const int ordinal = ++next_ordinal_[preset.id];
    const QString provenance_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    launch(servers.executable_filepath(preset.model),
           preset_launch_arguments(preset.filepath, ordinal, provenance_uuid,
                                   settings_.rom_dirpath()),
           provenance_uuid, preset.name, parent);
}

NewMachineHost Application::new_machine_host() {
    NewMachineHost host;
    const auto located = [this] { return locate_servers_on_this_host(settings_.servers_dirpath()); };
    host.list_presets = [this, located](NewMachineHost::Presets done) {
        const auto found = located();
        if (!found) {
            done({}, tr("No Beebium servers were found on this computer."));
            return;
        }
        cli_.run(found->executable_filepath(QStringLiteral("model-b")),
                 {QStringLiteral("--format"), QStringLiteral("jsonl"), QStringLiteral("list-presets"),
                  QStringLiteral("--all-models")},
                 [found, done = std::move(done)](const CliResult& result) {
                     if (!result.ok()) {
                         done({}, result.standard_error.trimmed().isEmpty()
                                      ? tr("The presets could not be read.")
                                      : result.standard_error.trimmed());
                         return;
                     }
                     done(launchable_presets(parse_preset_listing(result.standard_output),
                                             [&](const QString& model) {
                                                 const QFileInfo server(
                                                     found->executable_filepath(model));
                                                 return server.isFile() && server.isExecutable();
                                             }),
                          {});
                 });
    };
    host.executable = [located](const QString& model) {
        const auto found = located();
        return found ? found->executable_filepath(model) : QString();
    };
    host.describe_schema = [this, located](const QString& model, NewMachineHost::Schema done) {
        const auto found = located();
        if (!found) {
            done(std::nullopt);
            return;
        }
        cli_.run(found->executable_filepath(model), {QStringLiteral("describe-preset-schema")},
                 [done = std::move(done)](const CliResult& result) {
                     done(result.ok() ? PresetSchema::parse(result.standard_output.toStdString())
                                      : std::nullopt);
                 });
    };
    host.import_preset = [this, located](const QString& model, const QString& filepath,
                                         NewMachineHost::Imported done) {
        const auto found = located();
        if (!found) {
            done(false, tr("No Beebium servers were found on this computer."));
            return;
        }
        cli_.run(found->executable_filepath(model), {QStringLiteral("import-preset"), filepath},
                 [done = std::move(done)](const CliResult& result) {
                     done(result.ok(), result.standard_error.trimmed());
                 });
    };
    host.describe_rom = [this, located](const QString& model, const QString& rom,
                                        NewMachineHost::RomDescribed done) {
        const auto found = located();
        if (!found) {
            done(std::nullopt);
            return;
        }
        QStringList arguments{QStringLiteral("describe-rom"), rom};
        if (!settings_.rom_dirpath().isEmpty()) {
            arguments << QStringLiteral("--rom-dir") << settings_.rom_dirpath();
        }
        cli_.run(found->executable_filepath(model), arguments,
                 [done = std::move(done)](const CliResult& result) {
                     done(result.ok() ? parse_rom_description(result.standard_output) : std::nullopt);
                 });
    };
    host.list_serial_ports = [this, located](NewMachineHost::SerialPorts done) {
        const auto found = located();
        if (!found) {
            done({});
            return;
        }
        cli_.run(found->executable_filepath(QStringLiteral("model-b")),
                 {QStringLiteral("--format"), QStringLiteral("jsonl"),
                  QStringLiteral("list-serial-ports")},
                 [done = std::move(done)](const CliResult& result) {
                     done(result.ok() ? parse_serial_ports(result.standard_output) : QStringList());
                 });
    };
    host.launch = [this, located](const PresetListing& preset, const QString& filepath) {
        const auto found = located();
        if (!found) {
            QFile::remove(filepath);
            return;
        }
        PresetListing draft = preset;
        draft.filepath = filepath;
        const int ordinal = ++next_ordinal_[preset.id];
        const QString provenance_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        // The server has read the draft once it has started, or failed to.
        launch(found->executable_filepath(preset.model),
               preset_launch_arguments(filepath, ordinal, provenance_uuid, settings_.rom_dirpath()),
               provenance_uuid, preset.name, home_.get(), [filepath] { QFile::remove(filepath); });
    };
    return host;
}

void Application::new_machine(QWidget* parent) {
    if (!servers(parent)) {
        return;
    }
    if (!new_machine_) {
        new_machine_ = std::make_unique<NewMachineDialog>(new_machine_host(), &settings_);
    } else if (new_machine_->isHidden()) {
        new_machine_->reload();
    }
    new_machine_->show();
    new_machine_->raise();
    new_machine_->activateWindow();
}

void Application::launch_preset_by_id(const QString& id, QWidget* parent) {
    const auto found = servers(parent);
    if (!found) {
        after_failed_request();
        return;
    }
    begin_work();
    QPointer<QWidget> guard(parent);
    cli_.run(found->executable_filepath(QStringLiteral("model-b")),
             {QStringLiteral("--format"), QStringLiteral("jsonl"), QStringLiteral("list-presets"),
              QStringLiteral("--all-models")},
             [this, found, id, guard](const CliResult& result) {
                 if (result.ok()) {
                     for (const auto& preset : parse_preset_listing(result.standard_output)) {
                         if (preset.id == id) {
                             launch_preset(*found, preset, guard);
                             end_work();
                             return;
                         }
                     }
                 }
                 ServerMessageDialog dialog(tr("Could Not Start"),
                                            result.ok()
                                                ? tr("There is no preset called %1.").arg(id)
                                                : tr("The presets could not be read."),
                                            result.standard_error, guard);
                 dialog.exec();
                 after_failed_request();
                 end_work();
             });
}

void Application::launch_preset_file(const QString& filepath, QWidget* parent) {
    const auto found = servers(parent);
    if (!found) {
        after_failed_request();
        return;
    }
    // Which model, and so which server: the preset says (5.1).
    begin_work();
    QPointer<QWidget> guard(parent);
    cli_.run(found->executable_filepath(QStringLiteral("model-b")),
             {QStringLiteral("show-preset"), filepath},
             [this, found, filepath, guard](const CliResult& result) {
                 const QJsonObject preset = QJsonDocument::fromJson(result.standard_output).object();
                 const QString model = preset.value(QStringLiteral("model")).toString();
                 if (!result.ok() || model.isEmpty()) {
                     ServerMessageDialog dialog(
                         tr("Could Not Open"),
                         tr("Beebium could not read a preset at %1.").arg(filepath),
                         result.standard_error, guard);
                     dialog.exec();
                     after_failed_request();
                     end_work();
                     return;
                 }
                 PresetListing listing;
                 listing.filepath = filepath;
                 listing.model = model;
                 listing.id = QFileInfo(filepath).completeBaseName();
                 listing.name = preset.value(QStringLiteral("name")).toString(listing.id);
                 launch_preset(*found, listing, guard);
                 end_work();
             });
}

void Application::browse_for_machine(QWidget* parent) {
    const QString dirpath = QFileDialog::getExistingDirectory(
        parent, tr("Open Machine"), QDir::homePath(), QFileDialog::ShowDirsOnly);
    if (!dirpath.isEmpty()) {
        launch_saved_machine(dirpath, parent);
    }
}

void Application::describe_machine(const QString& machine_dirpath, SavedMachines::Described done) {
    const auto found = locate_servers_on_this_host(settings_.servers_dirpath());
    if (!found) {
        done(std::nullopt);
        return;
    }
    cli_.run(found->executable_filepath(QStringLiteral("model-b")),
             {QStringLiteral("describe-machine"), machine_dirpath, QStringLiteral("--format"),
              QStringLiteral("jsonl")},
             [done = std::move(done)](const CliResult& result) {
                 done(result.ok() ? parse_machine_description(result.standard_output)
                                  : std::nullopt);
             });
}

void Application::launch_saved_machine(const QString& dirpath, QWidget* parent) {
    const auto found = servers(parent);
    if (!found) {
        after_failed_request();
        return;
    }
    // Which model, and so which server: the directory says (5.3).
    begin_work();
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
                     after_failed_request();
                     end_work();
                     return;
                 }
                 if (machine->running()) {
                     // Its server is up already: attach rather than start a
                     // second, which its lock would refuse (5.3).
                     for (const auto& discovered : discovery_.machines()) {
                         if (discovered.on_this_host && discovered.uuid == machine->uuid) {
                             open(discovered.endpoint(), discovered.uuid);
                             end_work();
                             return;
                         }
                     }
                     // Not advertised: starting it says why it cannot run.
                 }
                 const QString provenance_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                 launch(found->executable_filepath(machine->model),
                        machine_launch_arguments(machine->machine_dirpath, provenance_uuid,
                                                 settings_.rom_dirpath()),
                        provenance_uuid, machine->name, guard);
                 end_work();
             });
}

void Application::launch(const QString& executable_filepath, const QStringList& arguments,
                         const QString& provenance_uuid, const QString& what, QWidget* parent,
                         std::function<void()> after) {
    std::fprintf(stderr, "beebium: launching %s\n", qPrintable(what));
    begin_work();
    QApplication::setOverrideCursor(Qt::BusyCursor);
    QPointer<QWidget> guard(parent);
    launcher_.launch(executable_filepath, arguments, provenance_uuid,
                     [this, what, guard, after](const LaunchResult& result) {
                         QApplication::restoreOverrideCursor();
                         if (after) {
                             after();
                         }
                         if (const auto* server = std::get_if<LaunchedServer>(&result)) {
                             open(Endpoint{"127.0.0.1", static_cast<std::uint16_t>(server->port)},
                                  {}, *server);
                             end_work();
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
                         after_failed_request();
                         end_work();
                     });
}

void Application::configure(QWidget* parent) {
    SettingsDialog dialog(settings_, parent);
    dialog.exec();
    // The servers may be somewhere else now.
    if (home_) {
        home_->presets_page()->set_servers(
            locate_servers_on_this_host(settings_.servers_dirpath()));
        saved_machines_->reload();
    }
}

void Application::handle_arguments(const QStringList& arguments) {
    bool requested = false;
    for (int i = 0; i < arguments.size(); ++i) {
        const QString& argument = arguments.at(i);
        if (argument == QLatin1String("--connect") && i + 1 < arguments.size()) {
            if (auto endpoint = parse_endpoint(arguments.at(++i).toStdString())) {
                open(*endpoint);
                requested = true;
            }
        } else if (argument == QLatin1String("--new-machine") && i + 1 < arguments.size()) {
            launch_preset_by_id(arguments.at(++i), nullptr);
            requested = true;
        } else if (argument == QLatin1String("--preset-file") && i + 1 < arguments.size()) {
            launch_preset_file(arguments.at(++i), nullptr);
            requested = true;
        } else if (argument == QLatin1String("--open") && i + 1 < arguments.size()) {
            // A saved machine, as the file manager opens one; one that cannot
            // be read is said so (5.3).
            launch_saved_machine(arguments.at(++i), nullptr);
            requested = true;
        } else if (auto link = parse_connect_link(QUrl(argument))) {
            // A link's run flag: Run on connect, for a machine started blocked.
            open(link->endpoint, {}, std::nullopt, link->run.has_value());
            requested = true;
        }
    }
    if (!requested) {
        // No request, at launch or from a later launch: the home window (13.4).
        show_home();
    }
}

}  // namespace beebium::linux_client
