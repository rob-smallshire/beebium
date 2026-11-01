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

#ifndef BEEBIUM_LINUX_SERVERS_SERVER_LAUNCHER_HPP
#define BEEBIUM_LINUX_SERVERS_SERVER_LAUNCHER_HPP

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <variant>

namespace beebium::linux_client {

// Which client launched a server, for the provenance it records and for its
// authority to shut it down (client specification 4.5).
inline constexpr char PROVENANCE_TYPE[] = "linux-gui";

// `start` for a preset (5.1): the server composes the machine's name from the
// preset's and this ordinal ("Model B #2", 5.7); blocked until the client's
// Run (--wait=api, 5.5); on an ephemeral port it reports; advertised.
QStringList preset_launch_arguments(const QString& preset_filepath, int ordinal,
                                    const QString& provenance_uuid, const QString& rom_dirpath);

// `start` for a saved machine directory (5.3): its name and identity are its
// own, so no ordinal.
QStringList machine_launch_arguments(const QString& machine_dirpath,
                                     const QString& provenance_uuid, const QString& rom_dirpath);

// The port in a server's "Listening on port N" line, once it has printed it.
std::optional<int> parse_listening_port(const QByteArray& output);

// A server this client started.
struct LaunchedServer {
    qint64 pid = 0;
    int port = 0;
    QString provenance_uuid;
    QString log_filepath;  // the server's output, kept for diagnosis
};

struct LaunchFailure {
    QString server_message;  // what the server said, verbatim; may be empty
    bool timed_out = false;
};

using LaunchResult = std::variant<LaunchedServer, LaunchFailure>;

// Starts servers detached, so a server outlives the client when it should
// (4.5: shared or externally used servers are left running at quit), with
// their output in a log file rather than a pipe nobody may read. Waits for the
// "Listening on port" line, or the process's end, or a timeout.
class ServerLauncher : public QObject {
    Q_OBJECT

public:
    using Done = std::function<void(const LaunchResult&)>;

    // `log_dirpath` holds one log per launch.
    explicit ServerLauncher(QString log_dirpath, QObject* parent = nullptr);

    void launch(const QString& executable_filepath, const QStringList& arguments,
                const QString& provenance_uuid, Done done, int timeout_ms = 15000);

private:
    QString log_dirpath_;
};

// Remove server logs no longer worth keeping: those beyond the newest
// `keep_newest` and older than `keep_days`. Returns how many were removed.
int prune_server_logs(const QString& log_dirpath, const QDateTime& now, int keep_newest = 20,
                      int keep_days = 7);

// Whether a process is still there.
bool process_alive(qint64 pid);

// Ask a process to end (SIGTERM): the server shuts down gracefully, flushing
// its drives, as on the RPC.
void terminate_process(qint64 pid);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_SERVERS_SERVER_LAUNCHER_HPP
