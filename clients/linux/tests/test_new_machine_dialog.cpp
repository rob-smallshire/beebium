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

// The New Machine dialog (client specification 5.1, 5.2): pick a preset,
// its resolved document becomes the draft, the server judges each edit,
// errors block Create, and Create launches the draft, saving it as a new
// preset first when asked.

#include "app/ClientSettings.hpp"
#include "app/NewMachineDialog.hpp"

#include "WaitUntil.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;
using nlohmann::ordered_json;

namespace {

PresetListing listing(const char* id, const char* name, bool built_in) {
    PresetListing preset;
    preset.id = QString::fromLatin1(id);
    preset.name = QString::fromLatin1(name);
    preset.source = built_in ? QStringLiteral("system") : QStringLiteral("user");
    preset.model = QStringLiteral("model-b");
    preset.description = QStringLiteral("A preset");
    preset.filepath = QStringLiteral("/presets/%1.preset.beebium").arg(QString::fromLatin1(id));
    return preset;
}

// The server, scripted: what resolve-preset answers for each subject.
struct Server {
    std::vector<QStringList> resolves;
    std::vector<QByteArray> drafts;  // the files given to resolve-preset
    QByteArray next = R"({"preset": {"model": "model-b", "name": "BBC Model B", "speed": 1.0,
                          "machine_description": null}, "diagnostics": []})";
    std::vector<std::pair<QString, QByteArray>> imported;
    bool import_ok = true;
    std::vector<std::pair<PresetListing, QByteArray>> launched;
    std::vector<PresetListing> presets{listing("model-b", "BBC Model B", true),
                                       listing("model-b-dfs", "BBC with DFS", true),
                                       listing("mine", "My Model B", false)};

    NewMachineHost host() {
        NewMachineHost host;
        host.list_presets = [this](NewMachineHost::Presets done) { done(presets, {}); };
        host.executable = [](const QString& model) { return QStringLiteral("/opt/beebium/bin/beebium-") + model; };
        host.describe_schema = [](const QString&, NewMachineHost::Schema done) {
            done(PresetSchema::parse(R"({"schema_version": 2, "model": {"id": "model-b", "name": "BBC Model B"},
                                         "sections": [{"type": "speed", "label": "Speed"}]})"));
        };
        host.resolve_runner = [this](const QString&, const QStringList& arguments, CoreCli::Done done) {
            resolves.push_back(arguments);
            QFile file(arguments.value(1));
            drafts.push_back(file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray());
            done(CliResult{true, 0, next, {}});
        };
        host.import_preset = [this](const QString&, const QString& filepath, NewMachineHost::Imported done) {
            QFile file(filepath);
            REQUIRE(file.open(QIODevice::ReadOnly));
            imported.emplace_back(filepath, file.readAll());
            done(import_ok, import_ok ? QString() : QStringLiteral("a preset called that already exists"));
        };
        host.launch = [this](const PresetListing& preset, const QString& filepath) {
            QFile file(filepath);
            REQUIRE(file.open(QIODevice::ReadOnly));
            launched.emplace_back(preset, file.readAll());
        };
        return host;
    }
};

struct Fixture {
    QTemporaryDir directory;
    ClientSettings settings{directory.filePath(QStringLiteral("Beebium.conf"))};
    Server server;
    std::unique_ptr<NewMachineDialog> dialog;
    void open() {
        dialog = std::make_unique<NewMachineDialog>(server.host(), &settings);
        REQUIRE(wait_until([&] { return dialog->create_button()->isEnabled(); }));
    }
};

}  // namespace

TEST_CASE("The dialog opens on the first built-in preset, resolved, ready to create",
          "[new-machine]") {
    Fixture f;
    f.open();
    REQUIRE(f.dialog->preset());
    CHECK(f.dialog->preset()->id == QStringLiteral("model-b"));
    // Built-in and the user's presets, under headings.
    QStringList items;
    for (int i = 0; i < f.dialog->preset_picker()->count(); ++i) {
        items << f.dialog->preset_picker()->itemText(i).trimmed();
    }
    CHECK(items == QStringList({QStringLiteral("Built-in"), QStringLiteral("BBC Model B"),
                                QStringLiteral("BBC with DFS"), QStringLiteral("My Presets"),
                                QStringLiteral("My Model B")}));
    REQUIRE(f.server.resolves.size() == 1);
    CHECK(f.server.resolves[0] == QStringList({QStringLiteral("resolve-preset"),
                                               QStringLiteral("/presets/model-b.preset.beebium"),
                                               QStringLiteral("--json")}));
    CHECK(f.dialog->draft().get("/speed") == ordered_json(1.0));
}

TEST_CASE("The dialog reopens on the preset last chosen", "[new-machine]") {
    Fixture f;
    f.settings.store().setValue(QString::fromLatin1(NewMachineDialog::LAST_PRESET_KEY),
                                QStringLiteral("mine"));
    f.open();
    CHECK(f.dialog->preset()->id == QStringLiteral("mine"));
    // Choosing another remembers it.
    Q_EMIT f.dialog->preset_picker()->activated(2);
    CHECK(f.settings.store().value(QString::fromLatin1(NewMachineDialog::LAST_PRESET_KEY)).toString() ==
          QStringLiteral("model-b-dfs"));
}

TEST_CASE("A deleted last preset leaves the dialog on the first built-in", "[new-machine]") {
    Fixture f;
    f.settings.store().setValue(QString::fromLatin1(NewMachineDialog::LAST_PRESET_KEY),
                                QStringLiteral("mine"));
    f.open();
    REQUIRE(f.dialog->preset()->id == QStringLiteral("mine"));
    // Deleted from the library while the dialog was closed.
    f.server.presets.pop_back();
    f.dialog->reload();
    REQUIRE(wait_until([&] { return f.dialog->create_button()->isEnabled(); }));
    CHECK(f.dialog->preset()->id == QStringLiteral("model-b"));
    // And afresh.
    Fixture g;
    g.server.presets.pop_back();
    g.settings.store().setValue(QString::fromLatin1(NewMachineDialog::LAST_PRESET_KEY),
                                QStringLiteral("mine"));
    g.open();
    CHECK(g.dialog->preset()->id == QStringLiteral("model-b"));
    CHECK(g.dialog->launch_error().isEmpty());
}

TEST_CASE("Errors block Create and are listed; warnings do not", "[new-machine][diagnostics]") {
    Fixture f;
    f.open();
    f.server.next = R"({"preset": null, "diagnostics": [
        {"severity": "warning", "path": "/colour", "location": "colour", "message": "unknown key 'colour' is ignored"},
        {"severity": "error", "path": "/econet/station", "location": "Econet", "message": "station 300 is out of range"}]})";
    f.dialog->description_field()->setText(QStringLiteral("Station {econet-station}"));
    Q_EMIT f.dialog->description_field()->editingFinished();
    CHECK_FALSE(f.dialog->create_button()->isEnabled());  // judged afresh
    REQUIRE(wait_until([&] { return f.dialog->diagnostics_list()->count() == 2; }));
    // Errors first, the server's words, then where.
    CHECK(f.dialog->diagnostics_list()->item(0)->text() ==
          QStringLiteral("station 300 is out of range\nEconet"));
    CHECK_FALSE(f.dialog->create_button()->isEnabled());
    // The edit reached the server as a draft.
    CHECK(ordered_json::parse(f.server.drafts.back().toStdString())["machine_description"] ==
          "Station {econet-station}");

    f.server.next = R"({"preset": null, "diagnostics": [
        {"severity": "warning", "path": "/colour", "location": "colour", "message": "unknown key 'colour' is ignored"}]})";
    f.dialog->description_field()->setText(QStringLiteral("Station {econet-station} (AUN)"));
    Q_EMIT f.dialog->description_field()->editingFinished();
    REQUIRE(wait_until([&] { return f.dialog->create_button()->isEnabled(); }));
    CHECK(f.dialog->diagnostics_list()->count() == 1);
}

TEST_CASE("A blank description is no subtitle", "[new-machine]") {
    Fixture f;
    f.open();
    f.dialog->description_field()->setText(QStringLiteral("Station {econet-station}"));
    Q_EMIT f.dialog->description_field()->editingFinished();
    REQUIRE(wait_until([&] { return f.dialog->create_button()->isEnabled(); }));
    f.dialog->description_field()->setText(QString());
    Q_EMIT f.dialog->description_field()->editingFinished();
    CHECK_FALSE(f.dialog->draft().json().contains("machine_description"));
}

TEST_CASE("Create launches the whole draft from its preset", "[new-machine][create]") {
    Fixture f;
    f.open();
    f.dialog->show();
    f.dialog->create_button()->click();
    REQUIRE(f.server.launched.size() == 1);
    CHECK(f.server.launched[0].first.id == QStringLiteral("model-b"));
    const ordered_json launched = ordered_json::parse(f.server.launched[0].second.toStdString());
    CHECK(launched["speed"] == 1.0);
    CHECK(launched.contains("machine_description"));  // the whole document, nulls and all
    CHECK(f.server.imported.empty());
    CHECK_FALSE(f.dialog->isVisible());
}

TEST_CASE("Save as new preset imports the draft under its name first, and a refusal stops",
          "[new-machine][create]") {
    Fixture f;
    f.open();
    f.dialog->show();
    f.dialog->save_as_box()->setChecked(true);
    f.dialog->save_as_name()->setText(QStringLiteral("My Elite Machine"));
    f.server.import_ok = false;
    f.dialog->create_button()->click();
    REQUIRE(f.server.imported.size() == 1);
    CHECK(ordered_json::parse(f.server.imported[0].second.toStdString())["name"] == "My Elite Machine");
    CHECK(f.server.launched.empty());
    CHECK(f.dialog->launch_error() == QStringLiteral("a preset called that already exists"));
    CHECK(f.dialog->isVisible());
    // The imported file was a temporary one.
    CHECK_FALSE(QFile::exists(f.server.imported[0].first));

    f.server.import_ok = true;
    f.dialog->create_button()->click();
    CHECK(f.server.imported.size() == 2);
    REQUIRE(f.server.launched.size() == 1);
}
