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

#include <QJsonArray>
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

QStringList parse_serial_ports(const QByteArray& jsonl) {
    QStringList paths;
    for (const QByteArray& line : jsonl.split('\n')) {
        const QJsonObject row = QJsonDocument::fromJson(line).object();
        const QString path = row.value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) {
            paths << path;
        }
    }
    return paths;
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

std::vector<DiscFormat> parse_disc_formats(const QByteArray& jsonl) {
    std::vector<DiscFormat> formats;
    for (const QByteArray& line : jsonl.split('\n')) {
        const QJsonObject row = QJsonDocument::fromJson(line).object();
        if (row.isEmpty() || !row.value(QStringLiteral("extensions")).isArray()) {
            continue;
        }
        DiscFormat format;
        format.name = row.value(QStringLiteral("name")).toString();
        format.kind = row.value(QStringLiteral("kind")).toString();
        for (const auto& extension : row.value(QStringLiteral("extensions")).toArray()) {
            QString text = extension.toString().toLower();
            if (text.startsWith(QLatin1Char('.'))) {
                text.remove(0, 1);
            }
            if (!text.isEmpty()) {
                format.extensions << text;
            }
        }
        formats.push_back(format);
    }
    return formats;
}

QStringList disc_extensions(const std::vector<DiscFormat>& formats, const QString& kind) {
    QStringList extensions;
    for (const auto& format : formats) {
        if (format.kind == kind) {
            extensions << format.extensions;
        }
    }
    extensions.removeDuplicates();
    return extensions;
}

std::optional<MachineDescription> parse_machine_description(const QByteArray& jsonl) {
    for (const QByteArray& line : jsonl.split('\n')) {
        const QJsonObject row = QJsonDocument::fromJson(line).object();
        const auto text = [&row](const char* key) {
            return row.value(QString::fromLatin1(key)).toString();
        };
        MachineDescription machine;
        machine.machine_dirpath = text("machine_dirpath");
        machine.name = text("name");
        machine.description = text("description");
        machine.uuid = text("uuid");
        machine.model = text("model");
        machine.preset = text("preset");
        machine.created = text("created");
        machine.last_used = text("last_used");
        machine.thumbnail_filepath = text("thumbnail_filepath");
        // Present and null: running, by a process that cannot be named.
        machine.locked = row.contains(QStringLiteral("locked_by_pid"));
        if (row.value(QStringLiteral("locked_by_pid")).isDouble()) {
            machine.locked_by_pid =
                static_cast<qint64>(row.value(QStringLiteral("locked_by_pid")).toDouble());
        }
        if (!machine.machine_dirpath.isEmpty() && !machine.uuid.isEmpty() &&
            !machine.model.isEmpty()) {
            return machine;
        }
    }
    return std::nullopt;
}

}  // namespace beebium::linux_client
