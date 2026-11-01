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

#include "servers/ServerLauncher.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#include <memory>

#include <signal.h>
#include <sys/types.h>

namespace beebium::linux_client {

namespace {

constexpr int POLL_MS = 50;

QStringList with_rom_dirpath(QStringList arguments, const QString& rom_dirpath) {
    if (!rom_dirpath.isEmpty()) {
        arguments << QStringLiteral("--rom-dir") << rom_dirpath;
    }
    return arguments;
}

}  // namespace

QStringList preset_launch_arguments(const QString& preset_filepath, int ordinal,
                                    const QString& provenance_uuid, const QString& rom_dirpath) {
    return with_rom_dirpath(
        {QStringLiteral("start"), QStringLiteral("--preset"), preset_filepath,
         QStringLiteral("--port"), QStringLiteral("0"), QStringLiteral("--advertise"),
         QStringLiteral("--wait=api"), QStringLiteral("--machine-ordinal"),
         QString::number(ordinal), QStringLiteral("--provenance-type"),
         QString::fromLatin1(PROVENANCE_TYPE), QStringLiteral("--provenance-uuid"),
         provenance_uuid},
        rom_dirpath);
}

QStringList machine_launch_arguments(const QString& machine_dirpath,
                                     const QString& provenance_uuid, const QString& rom_dirpath) {
    return with_rom_dirpath(
        {QStringLiteral("start"), QStringLiteral("--machine"), machine_dirpath,
         QStringLiteral("--port"), QStringLiteral("0"), QStringLiteral("--advertise"),
         QStringLiteral("--wait=api"), QStringLiteral("--provenance-type"),
         QString::fromLatin1(PROVENANCE_TYPE), QStringLiteral("--provenance-uuid"),
         provenance_uuid},
        rom_dirpath);
}

std::optional<int> parse_listening_port(const QByteArray& output) {
    static const QRegularExpression pattern(QStringLiteral("Listening on port (\\d+)"));
    const auto match = pattern.match(QString::fromUtf8(output));
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    bool ok = false;
    const int port = match.captured(1).toInt(&ok);
    return ok && port > 0 && port < 65536 ? std::optional<int>(port) : std::nullopt;
}

int prune_server_logs(const QString& log_dirpath, const QDateTime& now, int keep_newest,
                      int keep_days) {
    const QFileInfoList logs = QDir(log_dirpath).entryInfoList(
        {QStringLiteral("*.log")}, QDir::Files, QDir::Time);  // newest first
    int removed = 0;
    for (int i = keep_newest; i < logs.size(); ++i) {
        if (logs.at(i).lastModified().daysTo(now) > keep_days &&
            QFile::remove(logs.at(i).absoluteFilePath())) {
            ++removed;
        }
    }
    return removed;
}

bool process_alive(qint64 pid) {
    if (pid <= 0 || ::kill(static_cast<pid_t>(pid), 0) != 0) {
        return false;
    }
    // A detached server is not our child, so it is never left a zombie we
    // must reap; its state says whether it has ended all the same.
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (stat.open(QIODevice::ReadOnly)) {
        const QByteArray line = stat.readAll();
        const auto close = line.lastIndexOf(')');
        if (close >= 0 && close + 2 < line.size() && line.at(close + 2) == 'Z') {
            return false;
        }
    }
    return true;
}

void terminate_process(qint64 pid) {
    if (pid > 0) {
        ::kill(static_cast<pid_t>(pid), SIGTERM);
    }
}

ServerLauncher::ServerLauncher(QString log_dirpath, QObject* parent)
    : QObject(parent), log_dirpath_(std::move(log_dirpath)) {}

void ServerLauncher::launch(const QString& executable_filepath, const QStringList& arguments,
                            const QString& provenance_uuid, Done done, int timeout_ms) {
    QDir().mkpath(log_dirpath_);
    const QString log_filepath = QDir(log_dirpath_).filePath(provenance_uuid + QStringLiteral(".log"));
    QFile::remove(log_filepath);

    QProcess starter;
    starter.setProgram(executable_filepath);
    starter.setArguments(arguments);
    starter.setStandardInputFile(QProcess::nullDevice());
    starter.setStandardOutputFile(log_filepath);
    starter.setStandardErrorFile(log_filepath);
    qint64 pid = 0;
    if (!starter.startDetached(&pid)) {
        done(LaunchFailure{starter.errorString(), false});
        return;
    }

    // Watch the log for the port, and the process for an early end.
    auto* timer = new QTimer(this);
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    connect(timer, &QTimer::timeout, this,
            [timer, clock, pid, log_filepath, provenance_uuid, done, timeout_ms] {
                QFile log(log_filepath);
                const QByteArray output = log.open(QIODevice::ReadOnly) ? log.readAll() : QByteArray();
                if (const auto port = parse_listening_port(output)) {
                    timer->deleteLater();
                    done(LaunchedServer{pid, *port, provenance_uuid, log_filepath});
                    return;
                }
                if (!process_alive(pid)) {
                    timer->deleteLater();
                    done(LaunchFailure{QString::fromUtf8(output).trimmed(), false});
                    return;
                }
                if (clock->elapsed() > timeout_ms) {
                    timer->deleteLater();
                    terminate_process(pid);
                    done(LaunchFailure{QString::fromUtf8(output).trimmed(), true});
                }
            });
    timer->start(POLL_MS);
}

}  // namespace beebium::linux_client
