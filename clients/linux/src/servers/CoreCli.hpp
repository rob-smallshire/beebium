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

#ifndef BEEBIUM_LINUX_SERVERS_CORE_CLI_HPP
#define BEEBIUM_LINUX_SERVERS_CORE_CLI_HPP

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

namespace beebium::linux_client {

// What a run-to-completion subcommand produced.
struct CliResult {
    bool started = false;  // false: the executable could not be run at all
    int exit_code = -1;
    QByteArray standard_output;
    QString standard_error;  // the server's own words, for a failure (3.4)

    bool ok() const { return started && exit_code == 0; }
};

// Runs server subcommands for work with no running machine: listings,
// descriptions, thumbnails (client specification 5, "the CLI and gRPC
// split"). Asynchronous: the GUI thread never waits on a process.
class CoreCli : public QObject {
    Q_OBJECT

public:
    using Done = std::function<void(const CliResult&)>;

    using QObject::QObject;

    // Run `executable_filepath arguments...`; `done` runs on this object's
    // thread once it has exited, or failed to start, or after `timeout_ms`.
    void run(const QString& executable_filepath, const QStringList& arguments, Done done,
             int timeout_ms = 30000);
};

// One row of `list-presets --format jsonl`.
struct PresetListing {
    QString id;
    QString name;
    QString source;  // "system" (built-in) or "user"
    QString model;   // "model-b": the server executable is beebium-<model>
    QString description;
    QString release_date;
    QString filepath;
    bool has_thumbnail = false;

    bool built_in() const { return source == QLatin1String("system"); }
};

// Parse the listing. A line that is not a preset is skipped.
std::vector<PresetListing> parse_preset_listing(const QByteArray& jsonl);

// `describe-machine --format jsonl`, for Open (5.3).
struct MachineDescription {
    QString machine_dirpath;
    QString name;
    QString description;
    QString uuid;
    QString model;
    QString preset;
    QString created;
    QString last_used;
    QString thumbnail_filepath;
    // Whether a server holds the machine's lock, and its pid when the lock
    // names one.
    bool locked = false;
    std::optional<qint64> locked_by_pid;

    bool running() const { return locked; }
    bool operator==(const MachineDescription&) const = default;
};

// Nullopt unless the output describes a machine (dirpath, uuid and model).
std::optional<MachineDescription> parse_machine_description(const QByteArray& jsonl);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_SERVERS_CORE_CLI_HPP
