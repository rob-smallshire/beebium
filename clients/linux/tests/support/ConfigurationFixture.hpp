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

#ifndef BEEBIUM_LINUX_TESTS_CONFIGURATION_FIXTURE_HPP
#define BEEBIUM_LINUX_TESTS_CONFIGURATION_FIXTURE_HPP

#include "app/ClientSettings.hpp"
#include "app/NewMachineDialog.hpp"

#include "WaitUntil.hpp"

#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <fstream>
#include <memory>
#include <sstream>

namespace beebium::linux_client::testing {

// The model-b schema recorded from a 0.16.1 server.
inline std::string model_b_schema_text() {
    std::ifstream file(std::filesystem::path(BEEBIUM_LINUX_FIXTURES_DIRPATH) /
                       "describe-preset-schema-model-b.json");
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// A New Machine dialog on one preset whose resolved document is given,
// the server scripted: each resolution answers with the given diagnostics.
struct ConfigurationFixture {
    QTemporaryDir directory;
    ClientSettings settings{directory.filePath(QStringLiteral("Beebium.conf"))};
    nlohmann::ordered_json resolved;
    std::string schema_text = model_b_schema_text();
    nlohmann::ordered_json diagnostics = nlohmann::ordered_json::array();
    int resolutions = 0;
    std::optional<QString> answer;  // the next Browse's path
    std::map<QString, RomDescription> roms;  // describe-rom's answers, by name or path
    QStringList described;                   // what describe-rom was asked
    std::unique_ptr<NewMachineDialog> dialog;

    // Before: the server's answers are set before the dialog first asks,
    // since it asks once per model and per image.
    explicit ConfigurationFixture(nlohmann::ordered_json document,
                                  const std::function<void(ConfigurationFixture&)>& before = {})
        : resolved(std::move(document)) {
        if (before) {
            before(*this);
        }
        NewMachineHost host;
        host.list_presets = [](NewMachineHost::Presets done) {
            PresetListing preset;
            preset.id = QStringLiteral("test");
            preset.name = QStringLiteral("Test");
            preset.source = QStringLiteral("system");
            preset.model = QStringLiteral("model-b");
            done({preset}, {});
        };
        host.executable = [](const QString&) { return QStringLiteral("beebium-model-b"); };
        host.describe_schema = [this](const QString&, NewMachineHost::Schema done) {
            done(PresetSchema::parse(schema_text));
        };
        host.describe_rom = [this](const QString&, const QString& rom,
                                   NewMachineHost::RomDescribed done) {
            described << rom;
            const auto found = roms.find(rom);
            done(found == roms.end() ? std::nullopt : std::optional<RomDescription>(found->second));
        };
        host.resolve_runner = [this](const QString&, const QStringList&, CoreCli::Done done) {
            ++resolutions;
            const nlohmann::ordered_json answer{{"preset", resolved}, {"diagnostics", diagnostics}};
            done(CliResult{true, 0, QByteArray::fromStdString(answer.dump()), {}});
        };
        dialog = std::make_unique<NewMachineDialog>(host, &settings);
        dialog->set_chooser([this](const FormBrowse&) { return answer; });
        REQUIRE(wait_until([&] { return dialog->create_button()->isEnabled(); }));
    }

    template <typename Section>
    Section* section() {
        Section* found = dialog->findChild<Section*>();
        REQUIRE(found);
        return found;
    }
    QStringList section_names() const {
        QStringList names;
        for (int i = 0; i < dialog->section_list()->count(); ++i) {
            names << dialog->section_list()->item(i)->text();
        }
        return names;
    }
    // An edit has been judged again.
    void settle() {
        REQUIRE(wait_until([&] { return dialog->create_button()->isEnabled() ||
                                        !dialog->diagnostics().empty(); }));
    }
};

}  // namespace beebium::linux_client::testing

#endif  // BEEBIUM_LINUX_TESTS_CONFIGURATION_FIXTURE_HPP
