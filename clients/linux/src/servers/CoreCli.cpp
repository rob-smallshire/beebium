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

#include "servers/CoreCli.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

#include <memory>

namespace beebium::linux_client {

void CoreCli::run(const QString& executable_filepath, const QStringList& arguments, Done done,
                  int timeout_ms) {
    auto* process = new QProcess(this);
    auto finished = std::make_shared<bool>(false);
    const auto finish = [process, done, finished](CliResult result) {
        if (*finished) {
            return;
        }
        *finished = true;
        result.standard_output = process->readAllStandardOutput();
        result.standard_error = QString::fromUtf8(process->readAllStandardError()).trimmed();
        process->deleteLater();
        done(result);
    };
    connect(process, &QProcess::finished, this, [finish](int exit_code, QProcess::ExitStatus status) {
        CliResult result;
        result.started = true;
        result.exit_code = status == QProcess::NormalExit ? exit_code : -1;
        finish(result);
    });
    connect(process, &QProcess::errorOccurred, this, [process, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            CliResult result;
            result.standard_error = process->errorString();
            finish(result);
        }
    });
    QTimer::singleShot(timeout_ms, process, [process, finish] {
        process->kill();
        CliResult result;
        result.started = true;
        result.exit_code = -1;
        finish(result);
    });
    process->start(executable_filepath, arguments);
}

std::vector<PresetListing> parse_preset_listing(const QByteArray& jsonl) {
    std::vector<PresetListing> presets;
    for (const QByteArray& line : jsonl.split('\n')) {
        const QJsonObject row = QJsonDocument::fromJson(line).object();
        if (row.isEmpty() || !row.contains(QStringLiteral("id")) ||
            !row.contains(QStringLiteral("model"))) {
            continue;
        }
        PresetListing preset;
        preset.id = row.value(QStringLiteral("id")).toString();
        preset.name = row.value(QStringLiteral("name")).toString(preset.id);
        preset.source = row.value(QStringLiteral("source")).toString();
        preset.model = row.value(QStringLiteral("model")).toString();
        preset.description = row.value(QStringLiteral("description")).toString();
        preset.release_date = row.value(QStringLiteral("release_date")).toString();
        preset.filepath = row.value(QStringLiteral("filepath")).toString();
        preset.has_thumbnail = row.value(QStringLiteral("thumbnail")).toBool();
        presets.push_back(preset);
    }
    return presets;
}

std::optional<MachineDescription> parse_machine_description(const QByteArray& jsonl) {
    for (const QByteArray& line : jsonl.split('\n')) {
        const QJsonObject row = QJsonDocument::fromJson(line).object();
        const auto text = [&row](const char* key) {
            return row.value(QString::fromLatin1(key)).toString();
        };
        MachineDescription machine{text("machine_dirpath"), text("name"), text("description"),
                                   text("uuid"), text("model"), text("preset"),
                                   text("created"), text("last_used"),
                                   text("thumbnail_filepath")};
        if (!machine.machine_dirpath.isEmpty() && !machine.uuid.isEmpty() &&
            !machine.model.isEmpty()) {
            return machine;
        }
    }
    return std::nullopt;
}

}  // namespace beebium::linux_client
