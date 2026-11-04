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

// The home window and the client's lifetime (client specification 13.4):
// its pages drive the same launch, open and connect paths the dialogs did,
// and the client ends only as 13.4 says.

#include "app/Application.hpp"
#include "app/CardGrid.hpp"
#include "app/HomeWindow.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"
#include "app/PresetsPage.hpp"
#include "app/RunningMachinesPage.hpp"
#include "app/SavedMachines.hpp"
#include "app/SavedMachinesPage.hpp"

#include "FakeBeebiumServer.hpp"
#include "beebium/host_identity/HostFingerprint.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSet>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

// A stand-in for this host's servers: a beebium-model-b script that lists
// one preset, describes any directory as a saved machine, and "starts" by
// reporting the fake gRPC server's port. Each run's arguments are logged.
struct FakeServers {
    QTemporaryDir directory;
    QString log_filepath;

    explicit FakeServers(int grpc_port) {
        log_filepath = directory.filePath(QStringLiteral("calls.log"));
        const QString preset_filepath = directory.filePath(QStringLiteral("model-b.beebium-preset"));
        QFile preset(preset_filepath);
        REQUIRE(preset.open(QIODevice::WriteOnly));
        preset.close();
        const QByteArray body =
            "echo \"$*\" >> '" + log_filepath.toUtf8() + "'\n"
            "case \"$1\" in\n"
            "  start) echo 'Listening on port " + QByteArray::number(grpc_port) + "'; exec sleep 30;;\n"
            "  show-preset) printf '{\"model\":\"model-b\",\"name\":\"From a File\"}\\n';;\n"
            "  describe-machine) printf '{\"machine_dirpath\":\"%s\",\"name\":\"Elite\","
            "\"uuid\":\"u-1\",\"model\":\"model-b\",\"created\":\"2026-11-01T10:00:00Z\"}\\n' \"$2\";;\n"
            "  *) echo '{\"id\":\"model-b\",\"name\":\"Model B\",\"source\":\"system\","
            "\"model\":\"model-b\",\"description\":\"32K Model B\",\"filepath\":\"" +
            preset_filepath.toUtf8() + "\",\"thumbnail\":false}';;\n"
            "esac\n";
        const QString filepath = directory.filePath(QStringLiteral("beebium-model-b"));
        QFile file(filepath);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\n" + body);
        file.close();
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
    }

    QString dirpath() const { return directory.path(); }

    QStringList calls() const {
        QFile file(log_filepath);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
};

// The client's settings start clean for each case.
void reset(Application& client, const QString& servers_dirpath = {}) {
    client.settings().clear_machines();
    client.settings().set_servers_dirpath(servers_dirpath);
}

bool home_shown(const Application& client) {
    return client.home_window() && client.home_window()->isVisible();
}

int home_windows() {
    int count = 0;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (qobject_cast<HomeWindow*>(widget)) {
            ++count;
        }
    }
    return count;
}

Endpoint endpoint_of(const FakeServer& server) {
    return {"127.0.0.1", static_cast<std::uint16_t>(server.port)};
}

}  // namespace

// --- the pages ---------------------------------------------------------------

TEST_CASE("The first page is the recent machines for a user with some, else the presets",
          "[home]") {
    CHECK(initial_home_page(true) == HomePage::saved_machines);
    CHECK(initial_home_page(false) == HomePage::presets);
}

TEST_CASE("The discovered machines page defaults to localhost and offers the recents",
          "[home][connect]") {
    DiscoveredMachines machines;
    RunningMachinesPage page(machines, {});
    CHECK(page.address_box()->currentText() == QStringLiteral("localhost:48875"));
    CHECK(page.connect_button()->isEnabled());

    page.set_recents({{"host-b", 48876}, {"host-a.local", 48875}});
    REQUIRE(page.address_box()->count() == 2);
    CHECK(page.address_box()->itemText(0) == QStringLiteral("host-b:48876"));
}

TEST_CASE("An address that does not parse disables Connect", "[home][connect]") {
    DiscoveredMachines machines;
    RunningMachinesPage page(machines, {});
    page.address_box()->setEditText(QStringLiteral("host:99999"));
    CHECK_FALSE(page.connect_button()->isEnabled());
    page.address_box()->setEditText(QStringLiteral("host-a.local"));
    CHECK(page.connect_button()->isEnabled());
    CHECK(page.endpoint() == Endpoint{"host-a.local", 48875});
}

TEST_CASE("Connect asks for the typed machine", "[home][connect]") {
    DiscoveredMachines machines;
    RunningMachinesPage page(machines, {});
    std::optional<Endpoint> asked;
    QObject::connect(&page, &RunningMachinesPage::connect_requested,
                     [&](const Endpoint& endpoint, const QString&) { asked = endpoint; });
    page.address_box()->setEditText(QStringLiteral("host-b:48876"));
    page.connect_button()->click();
    CHECK(asked == Endpoint{"host-b", 48876});
}

TEST_CASE("Without servers the presets page says so and offers Configure", "[home][presets]") {
    QTemporaryDir cache;
    PresetsPage page(cache.path());
    page.set_servers(std::nullopt);
    CHECK(page.status_text().contains(QStringLiteral("could not find its servers")));
    CHECK(page.offering_configure());
    CHECK(page.grid()->count() == 0);
}

TEST_CASE("Servers that cannot list the presets are named, in their own words, with Configure",
          "[home][presets]") {
    // As a server too old for this client answers (#240).
    QTemporaryDir directory;
    const QString filepath = directory.filePath(QStringLiteral("beebium-model-b"));
    {
        QFile file(filepath);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\necho 'Unknown argument: --all-models' >&2\nexit 64\n");
        file.close();
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
    }
    QTemporaryDir cache;
    PresetsPage page(cache.path());
    page.set_servers(ServerLocation{directory.path(), QStringLiteral("PATH")});
    REQUIRE(wait_until([&] { return page.status_text().startsWith(QStringLiteral("Beebium could not")); }));
    CHECK(page.status_text() ==
          QStringLiteral("Beebium could not read the presets from the servers in %1, found on PATH.")
              .arg(directory.path()));
    CHECK(page.detail_text() == QStringLiteral("Unknown argument: --all-models"));
    CHECK(page.offering_configure());
    CHECK(page.grid()->count() == 0);
}

TEST_CASE("The presets page shows the catalogue as cards, and activating one launches it",
          "[home][presets]") {
    FakeServers servers(1);
    QTemporaryDir cache;
    PresetsPage page(cache.path());
    page.set_servers(ServerLocation{servers.dirpath(), QStringLiteral("settings")});
    REQUIRE(wait_until([&] { return page.grid()->count() == 1; }));
    CHECK(page.grid()->item(0)->text().startsWith(QStringLiteral("Model B")));
    CHECK_FALSE(page.offering_configure());
    CHECK(page.detail_text().isEmpty());
    // The card is the control: no Launch button beside the grid.
    CHECK(page.findChildren<QPushButton*>(QStringLiteral("launch")).isEmpty());

    std::optional<PresetListing> asked;
    QObject::connect(&page, &PresetsPage::launch_requested,
                     [&](const PresetListing& preset) { asked = preset; });
    // As a click or Return does, by the desktop's own setting.
    Q_EMIT page.grid()->itemActivated(page.grid()->item(0));
    REQUIRE(asked);
    CHECK(asked->id == QStringLiteral("model-b"));

    // Nothing more while a launch is under way.
    asked.reset();
    page.set_launching(true);
    Q_EMIT page.grid()->itemActivated(page.grid()->item(0));
    CHECK_FALSE(asked);
}

// --- recent machines -----------------------------------------------------------

TEST_CASE("Recent machines are described in recents order, the undescribable left out",
          "[home][saved]") {
    QTemporaryDir directory;
    ClientSettings settings(directory.filePath(QStringLiteral("Beebium.conf")));
    settings.remember_machine(QStringLiteral("/m/Gone.beebium-machine"));
    settings.remember_machine(QStringLiteral("/m/Repton.beebium-machine"));
    settings.remember_machine(QStringLiteral("/m/Elite.beebium-machine"));
    SavedMachines machines(settings, [](const QString& dirpath, SavedMachines::Described done) {
        if (dirpath.contains(QStringLiteral("Gone"))) {
            done(std::nullopt);
            return;
        }
        MachineDescription machine;
        machine.machine_dirpath = dirpath;
        machine.name = QFileInfo(dirpath).completeBaseName();
        machine.locked = dirpath.contains(QStringLiteral("Elite"));
        done(machine);
    });
    machines.reload();
    REQUIRE(machines.machines().size() == 2);
    CHECK(machines.machines()[0].name == QStringLiteral("Elite"));
    CHECK(machines.machines()[0].running());
    CHECK(machines.machines()[1].name == QStringLiteral("Repton"));
    // Left out of the list, not forgotten: it may only be unreachable now.
    CHECK(settings.recent_machines().size() == 3);
}

TEST_CASE("Reloads asked for during a pass become one more pass", "[home][saved]") {
    QTemporaryDir directory;
    ClientSettings settings(directory.filePath(QStringLiteral("Beebium.conf")));
    settings.remember_machine(QStringLiteral("/m/A.beebium-machine"));
    std::vector<SavedMachines::Described> waiting;
    int described = 0;
    SavedMachines machines(settings, [&](const QString&, SavedMachines::Described done) {
        ++described;
        waiting.push_back(std::move(done));
    });
    machines.reload();
    machines.reload();
    machines.reload();
    CHECK(described == 1);  // one at a time
    MachineDescription machine;
    machine.machine_dirpath = QStringLiteral("/m/A.beebium-machine");
    waiting.back()(machine);
    CHECK(described == 2);  // the queued reloads, as one pass
    waiting.back()(machine);
    CHECK(described == 2);
    CHECK_FALSE(machines.loading());
}

TEST_CASE("Removing a saved machine forgets it from the list only", "[home][saved]") {
    QTemporaryDir directory;
    ClientSettings settings(directory.filePath(QStringLiteral("Beebium.conf")));
    settings.remember_machine(QStringLiteral("/m/A.beebium-machine"));
    settings.remember_machine(QStringLiteral("/m/B.beebium-machine"));
    SavedMachines machines(settings, [](const QString& dirpath, SavedMachines::Described done) {
        MachineDescription machine;
        machine.machine_dirpath = dirpath;
        machine.name = dirpath;
        done(machine);
    });
    SavedMachinesPage page(machines);
    machines.reload();
    REQUIRE(page.grid()->count() == 2);
    CHECK_FALSE(page.showing_empty_state());

    machines.forget(QStringLiteral("/m/B.beebium-machine"));
    CHECK(page.grid()->count() == 1);
    CHECK(settings.recent_machines() == QStringList{QStringLiteral("/m/A.beebium-machine")});

    machines.clear();
    CHECK(page.grid()->count() == 0);
    CHECK(page.showing_empty_state());
}

TEST_CASE("A saved machine is last used at a time, or not run yet", "[home][saved]") {
    MachineDescription machine;
    CHECK(last_used_text(machine) == QStringLiteral("Not run yet"));
    machine.last_used = QStringLiteral("2026-10-31T18:30:00Z");
    CHECK(last_used_text(machine).startsWith(QStringLiteral("Last used ")));
}

// --- the client's lifetime -----------------------------------------------------

TEST_CASE("Launched with no request, the client shows the home window", "[home][lifetime]") {
    Application client(QRhiWidget::Api::OpenGL);
    reset(client);
    client.handle_arguments({});
    REQUIRE(home_shown(client));
    CHECK(client.home_window()->page() == HomePage::presets);
    CHECK(client.windows().empty());
}

TEST_CASE("The home window's pages are the presets, the recent and the discovered machines",
          "[home]") {
    Application client(QRhiWidget::Api::OpenGL);
    reset(client);
    client.handle_arguments({});
    REQUIRE(home_shown(client));
    auto* pages = client.home_window()->findChild<QListWidget*>(QStringLiteral("homePages"));
    REQUIRE(pages);
    QStringList names;
    for (int row = 0; row < pages->count(); ++row) {
        names << pages->item(row)->text();
    }
    CHECK(names == QStringList{QStringLiteral("Presets"), QStringLiteral("Recent Machines"),
                               QStringLiteral("Discovered Machines")});
    for (int row = 0; row < pages->count(); ++row) {
        CHECK(pages->item(row)->icon().isNull());  // names alone, as on macOS
    }
    auto* no_recents = client.home_window()->findChild<QWidget*>(QStringLiteral("noSavedMachines"));
    REQUIRE(no_recents);
    QStringList notes;
    for (QLabel* label : no_recents->findChildren<QLabel*>()) {
        notes << label->text();
    }
    CHECK(notes.contains(QStringLiteral("No recent machines yet")));
    client.home_window()->close();
}

TEST_CASE("The home window keeps its Ctrl shortcuts and answers to the Meta forms too",
          "[home][shortcuts]") {
    Application client(QRhiWidget::Api::OpenGL);
    reset(client);
    client.handle_arguments({});
    REQUIRE(home_shown(client));
    // It has no machine, so Ctrl is free; the Meta forms make one habit
    // work in both kinds of window.
    const std::vector<std::pair<QString, QSet<QString>>> table{
        {QStringLiteral("New Machine..."), {QStringLiteral("Ctrl+N"), QStringLiteral("Meta+N")}},
        {QStringLiteral("Open Machine..."), {QStringLiteral("Ctrl+O"), QStringLiteral("Meta+O")}},
        {QStringLiteral("Connect to Machine..."),
         {QStringLiteral("Ctrl+Shift+O"), QStringLiteral("Meta+Shift+O")}},
        {QStringLiteral("Close Window"), {QStringLiteral("Ctrl+W"), QStringLiteral("Meta+W")}},
        {QStringLiteral("Quit"), {QStringLiteral("Ctrl+Q"), QStringLiteral("Meta+Q")}},
        {QStringLiteral("Configure Beebium..."),
         {QStringLiteral("Ctrl+Shift+,"), QStringLiteral("Meta+,")}},
    };
    const auto actions = client.home_window()->findChildren<QAction*>();
    for (const auto& [text, expected] : table) {
        INFO(text.toStdString());
        const auto found = std::find_if(actions.begin(), actions.end(), [&](QAction* action) {
            return action->text().remove(QLatin1Char('&')) == text;
        });
        REQUIRE(found != actions.end());
        QSet<QString> forms;
        for (const QKeySequence& sequence : (*found)->shortcuts()) {
            forms.insert(sequence.toString(QKeySequence::PortableText));
        }
        CHECK(forms == expected);
    }
    client.home_window()->close();
}

TEST_CASE("Launched with a request, the client opens that machine's window only",
          "[home][lifetime]") {
    FakeServer server;
    Application client(QRhiWidget::Api::OpenGL);
    reset(client);
    client.handle_arguments({QStringLiteral("--connect"), server.endpoint()});
    REQUIRE(client.windows().size() == 1);
    CHECK(client.home_window() == nullptr);
    client.windows().front()->close();
    REQUIRE(wait_until([&] { return client.windows().empty(); }));
}

TEST_CASE("Closing the home window with a machine open does not end the client; closing "
          "the machine then does",
          "[home][lifetime]") {
    FakeServer server;
    Application client(QRhiWidget::Api::OpenGL);
    reset(client);
    bool finished = false;
    QObject::connect(&client, &Application::finished, [&] { finished = true; });
    client.show_home();
    MainWindow* window = client.open(endpoint_of(server));
    REQUIRE(wait_until([&] { return window->session().connected(); }));

    client.home_window()->close();
    // Given time to end, it does not.
    CHECK_FALSE(wait_until([&] { return finished; }, 300ms));
    CHECK(client.windows().size() == 1);

    window->close();
    REQUIRE(wait_until([&] { return finished; }));
}

TEST_CASE("Closing the last machine window with the home window open leaves the home window",
          "[home][lifetime]") {
    FakeServer server;
    Application client(QRhiWidget::Api::OpenGL);
    reset(client);
    bool finished = false;
    QObject::connect(&client, &Application::finished, [&] { finished = true; });
    client.show_home();
    MainWindow* window = client.open(endpoint_of(server));
    REQUIRE(wait_until([&] { return window->session().connected(); }));

    window->close();
    REQUIRE(wait_until([&] { return client.windows().empty(); }));
    CHECK_FALSE(wait_until([&] { return finished; }, 300ms));
    CHECK(home_shown(client));

    // And closing the home window then ends it.
    client.home_window()->close();
    REQUIRE(wait_until([&] { return finished; }));
}

TEST_CASE("Quit from the home window closes every machine window and ends the client",
          "[home][lifetime]") {
    FakeServer first_server;
    FakeServer second_server;
    Application client(QRhiWidget::Api::OpenGL);
    reset(client);
    bool finished = false;
    QObject::connect(&client, &Application::finished, [&] { finished = true; });
    client.show_home();
    MainWindow* first = client.open(endpoint_of(first_server));
    MainWindow* second = client.open(endpoint_of(second_server));
    REQUIRE(wait_until([&] { return first->session().connected() && second->session().connected(); }));

    Q_EMIT client.home_window()->quit_requested();
    REQUIRE(wait_until([&] { return finished; }));
    CHECK(client.windows().empty());
    CHECK_FALSE(home_shown(client));
}

TEST_CASE("Showing the home window brings the one forward, never a second", "[home][lifetime]") {
    FakeServer server;
    Application client(QRhiWidget::Api::OpenGL);
    reset(client);
    MainWindow* window = client.open(endpoint_of(server));
    REQUIRE(wait_until([&] { return window->session().connected(); }));
    CHECK(client.home_window() == nullptr);

    // File > Show Beebium, twice, and a plain relaunch.
    Q_EMIT window->home_requested();
    HomeWindow* home = client.home_window();
    REQUIRE(home);
    CHECK(home->isVisible());
    home->close();
    Q_EMIT window->home_requested();
    client.handle_arguments({});
    CHECK(client.home_window() == home);
    CHECK(home->isVisible());
    CHECK(home_windows() == 1);

    window->close();
    REQUIRE(wait_until([&] { return client.windows().empty(); }));
}

TEST_CASE("A machine window has no application commands, and its Window menu shows Beebium",
          "[home][window]") {
    MainWindow window;
    const auto items_of = [&](const QString& title) {
        QStringList items;
        for (QAction* menu : window.menuBar()->actions()) {
            if (menu->text().remove(QLatin1Char('&')) == title) {
                for (QAction* item : menu->menu()->actions()) {
                    if (!item->isSeparator()) {
                        items << item->text().remove(QLatin1Char('&'));
                    }
                }
            }
        }
        return items;
    };
    CHECK(items_of(QStringLiteral("File")) ==
          QStringList{QStringLiteral("Save"), QStringLiteral("Save As..."),
                      QStringLiteral("Rename Machine..."), QStringLiteral("Move To..."),
                      QStringLiteral("Close Window"),
                      QStringLiteral("Quit")});
    CHECK(items_of(QStringLiteral("Window")) ==
          QStringList{QStringLiteral("Minimize"), QStringLiteral("Show Beebium")});
}

// --- the pages drive the launch, open and connect paths -------------------------

TEST_CASE("Connect on the discovered machines page opens a window on that machine",
          "[home][connect]") {
    FakeServer server;
    Application client(QRhiWidget::Api::OpenGL);
    reset(client);
    client.show_home(HomePage::running_machines);
    RunningMachinesPage* page = client.home_window()->running_machines_page();
    page->address_box()->setEditText(server.endpoint());
    page->connect_button()->click();
    REQUIRE(client.windows().size() == 1);
    REQUIRE(wait_until([&] { return client.windows().front()->session().connected(); }));
    client.windows().front()->close();
    REQUIRE(wait_until([&] { return client.windows().empty(); }));
}

TEST_CASE("Activating a preset starts the preset's server and opens its window",
          "[home][presets][launch]") {
    FakeServer server;
    FakeServers servers(server.port);
    Application client(QRhiWidget::Api::OpenGL);
    reset(client, servers.dirpath());
    client.show_home(HomePage::presets);
    PresetsPage* page = client.home_window()->presets_page();
    REQUIRE(wait_until([&] { return page->grid()->count() == 1; }));
    Q_EMIT page->grid()->itemActivated(page->grid()->item(0));
    REQUIRE(wait_until([&] { return client.windows().size() == 1; }));
    MainWindow* window = client.windows().front();
    REQUIRE(wait_until([&] { return window->session().connected(); }));
    CHECK(window->session().launched_server().has_value());
    const QStringList calls = servers.calls();
    CHECK(std::any_of(calls.begin(), calls.end(), [](const QString& call) {
        return call.startsWith(QStringLiteral("start --preset"));
    }));
    window->close();
    REQUIRE(wait_until([&] { return client.windows().empty(); }, 15000ms));
}

TEST_CASE("Activating a recent machine opens it, and it is remembered",
          "[home][saved][launch]") {
    FakeServer server;
    FakeServers servers(server.port);
    const QString machine_dirpath = servers.directory.filePath(QStringLiteral("Elite.beebium-machine"));
    {
        std::lock_guard lock(server.system.calls_mutex);
        server.system.saved_machine_dirpath = machine_dirpath.toStdString();
    }
    Application client(QRhiWidget::Api::OpenGL);
    reset(client, servers.dirpath());
    client.settings().remember_machine(machine_dirpath);
    client.show_home();
    // A user with a saved machine lands on them.
    REQUIRE(client.home_window()->page() == HomePage::saved_machines);
    SavedMachinesPage* page = client.home_window()->saved_machines_page();
    // The card is the control: no Open button beside the grid.
    CHECK(page->findChildren<QPushButton*>(QStringLiteral("open")).isEmpty());
    REQUIRE(wait_until([&] { return page->grid()->count() == 1; }));
    Q_EMIT page->grid()->itemActivated(page->grid()->item(0));
    REQUIRE(wait_until([&] { return client.windows().size() == 1; }));
    MainWindow* window = client.windows().front();
    REQUIRE(wait_until([&] { return window->session().connected(); }));
    const QStringList calls = servers.calls();
    CHECK(std::any_of(calls.begin(), calls.end(), [&](const QString& call) {
        return call.startsWith(QStringLiteral("start --machine ") + machine_dirpath);
    }));
    CHECK(client.settings().recent_machines().front() == machine_dirpath);
    window->close();
    REQUIRE(wait_until([&] { return client.windows().empty(); }, 15000ms));
}

TEST_CASE("A saved machine on a server that does not share this filesystem is not remembered",
          "[home][saved]") {
    FakeServer server;
    server.system.host_fingerprint = "another-host";
    {
        std::lock_guard lock(server.system.calls_mutex);
        server.system.saved_machine_dirpath = "/elsewhere/Elite.beebium-machine";
    }
    Application client(QRhiWidget::Api::OpenGL);
    reset(client);
    MainWindow* window = client.open(endpoint_of(server));
    REQUIRE(wait_until([&] { return window->session().connected(); }));
    REQUIRE(window->session().machine_saved());
    CHECK_FALSE(window->session().server_shares_filesystem());
    window->session().refresh_system_info();
    QCoreApplication::processEvents();
    CHECK(client.settings().recent_machines().isEmpty());
    window->close();
    REQUIRE(wait_until([&] { return client.windows().empty(); }));
}

TEST_CASE("A saved machine on a server that shares this filesystem is remembered",
          "[home][saved]") {
    // A host that will not identify itself shares its filesystem with no
    // server, rightly (client specification 3.2): nothing to show here.
    if (beebium::host_identity::host_fingerprint().empty()) {
        SKIP("this host has no machine-id, so no server can be known to share its filesystem");
    }
    FakeServer server;
    {
        std::lock_guard lock(server.system.calls_mutex);
        server.system.saved_machine_dirpath = "/here/Elite.beebium-machine";
    }
    Application client(QRhiWidget::Api::OpenGL);
    reset(client);
    MainWindow* window = client.open(endpoint_of(server));
    REQUIRE(wait_until([&] { return window->session().connected(); }));
    REQUIRE(wait_until([&] { return !client.settings().recent_machines().isEmpty(); }));
    CHECK(client.settings().recent_machines().front() == QStringLiteral("/here/Elite.beebium-machine"));
    window->close();
    REQUIRE(wait_until([&] { return client.windows().empty(); }));
}

TEST_CASE("A saved machine whose directory has gone is forgotten after three passes",
          "[home][saved]") {
    QTemporaryDir directory;
    ClientSettings settings(directory.filePath(QStringLiteral("Beebium.conf")));
    const QString gone = directory.filePath(QStringLiteral("Gone.beebium-machine"));
    // Present on disk but not describable (no servers, say): never forgotten.
    const QString unreadable = directory.filePath(QStringLiteral("Unreadable.beebium-machine"));
    REQUIRE(QDir().mkpath(unreadable));
    settings.remember_machine(gone);
    settings.remember_machine(unreadable);
    SavedMachines machines(settings, [](const QString&, SavedMachines::Described done) {
        done(std::nullopt);
    });
    machines.reload();
    machines.reload();
    CHECK(settings.recent_machines().size() == 2);
    machines.reload();
    CHECK(settings.recent_machines() == QStringList{unreadable});
    for (int pass = 0; pass < 5; ++pass) {
        machines.reload();
    }
    CHECK(settings.recent_machines() == QStringList{unreadable});
}

TEST_CASE("Opening a preset file launches it as the presets page would", "[home][launch]") {
    FakeServer server;
    FakeServers servers(server.port);
    const QString preset_filepath = servers.directory.filePath(QStringLiteral("Mine.beebium-preset"));
    {
        QFile preset(preset_filepath);
        REQUIRE(preset.open(QIODevice::WriteOnly));
    }
    Application client(QRhiWidget::Api::OpenGL);
    reset(client, servers.dirpath());
    client.handle_arguments({QStringLiteral("--preset-file"), preset_filepath});
    REQUIRE(wait_until([&] { return client.windows().size() == 1; }));
    // That machine's window only (4.1).
    CHECK(client.home_window() == nullptr);
    MainWindow* window = client.windows().front();
    REQUIRE(wait_until([&] { return window->session().connected(); }));
    const QStringList calls = servers.calls();
    CHECK(std::any_of(calls.begin(), calls.end(), [&](const QString& call) {
        return call.startsWith(QStringLiteral("start --preset ") + preset_filepath);
    }));
    window->close();
    REQUIRE(wait_until([&] { return client.windows().empty(); }, 15000ms));
}
