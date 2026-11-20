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

// The presets catalogue's search against a real server (client
// specification 5.1): the listing as the presets page reads it, each row
// indexed on the server's facets and keywords.

#include "servers/CoreCli.hpp"
#include "servers/PresetLibrary.hpp"

#include "RealServer.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

QByteArray run(const QStringList& arguments) {
    QProcess process;
    process.start(RealServer::executable_filepath(), arguments);
    if (!process.waitForFinished(30000) || process.exitCode() != 0) {
        return {};
    }
    return process.readAllStandardOutput();
}

QStringList built_in_matches(const std::vector<PresetListing>& presets, const char* query) {
    QStringList ids;
    for (const auto& preset : presets) {
        if (preset.built_in() && preset_matches(preset, QString::fromUtf8(query))) {
            ids << preset.id;
        }
    }
    return ids;
}

}  // namespace

TEST_CASE("Every preset a real server lists carries the six facets it describes, and keywords",
          "[presets][search][real-server]") {
    if (!RealServer::available()) {
        SKIP("No server at BEEBIUM_TEST_SERVER_FILEPATH to run.");
    }
    QSet<QString> keys;
    for (const QByteArray& line : run({QStringLiteral("describe-facets"), QStringLiteral("--json")}).split('\n')) {
        const QJsonObject facet = QJsonDocument::fromJson(line).object();
        if (!facet.isEmpty()) {
            keys << facet.value(QStringLiteral("key")).toString();
        }
    }
    REQUIRE(keys.size() == 6);
    // As the presets page asks.
    const auto presets = parse_preset_listing(
        run({QStringLiteral("--format"), QStringLiteral("jsonl"), QStringLiteral("list-presets"),
             QStringLiteral("--all-models")}));
    REQUIRE_FALSE(presets.empty());
    for (const auto& preset : presets) {
        INFO(preset.id.toStdString());
        QSet<QString> listed;
        for (const auto& facet : preset.facets) {
            listed << facet.first;
        }
        CHECK(listed == keys);
        CHECK_FALSE(preset.keywords.isEmpty());
    }
}

TEST_CASE("A real server's built-in presets answer a search a person would type",
          "[presets][search][real-server]") {
    if (!RealServer::available()) {
        SKIP("No server at BEEBIUM_TEST_SERVER_FILEPATH to run.");
    }
    const auto presets = parse_preset_listing(
        run({QStringLiteral("--format"), QStringLiteral("jsonl"), QStringLiteral("list-presets"),
             QStringLiteral("--all-models")}));
    REQUIRE_FALSE(presets.empty());
    CHECK(built_in_matches(presets, "65c102 master") == QStringList{QStringLiteral("master-128-65c102-copro")});
    // By keyword: the coprocessor presets, and only those.
    const QStringList copros = built_in_matches(presets, "6502 copro");
    CHECK(copros.contains(QStringLiteral("master-128-65c102-copro")));
    for (const auto& preset : presets) {
        if (copros.contains(preset.id)) {
            for (const auto& facet : preset.facets) {
                if (facet.first == QLatin1String("coprocessor")) {
                    CHECK(facet.second != QStringList{QStringLiteral("None")});
                }
            }
        }
    }
}
