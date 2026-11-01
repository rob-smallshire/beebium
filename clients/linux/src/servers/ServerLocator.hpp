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

#ifndef BEEBIUM_LINUX_SERVERS_SERVER_LOCATOR_HPP
#define BEEBIUM_LINUX_SERVERS_SERVER_LOCATOR_HPP

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace beebium::linux_client {

// Where this host's Beebium servers are: the directory holding the
// beebium-<model> executables (docs/deployment.md). Each server finds its own
// ROMs, presets and extensions relative to itself, so the directory is all
// the client needs.
struct ServerLocation {
    QString servers_dirpath;
    QString found_by;  // "settings", "PATH" or "default", for the settings view

    // The executable for a model id ("model-b" -> <dir>/beebium-model-b).
    QString executable_filepath(const QString& model) const;
};

// Search, first match wins:
//  1. `configured`, the user's setting, if set;
//  2. each directory on PATH;
//  3. `fallback_dirpaths` (the packages' /opt/beebium/bin).
// A directory qualifies when it holds an executable beebium-model-b, the
// model every build has. Nullopt when none does.
std::optional<ServerLocation> locate_servers(
    const QString& configured, const QStringList& path_dirpaths,
    const QStringList& fallback_dirpaths,
    const std::function<bool(const QString& filepath)>& is_executable);

// The same search on this host: the PATH environment variable and
// /opt/beebium/bin.
std::optional<ServerLocation> locate_servers_on_this_host(const QString& configured);

// Which servers, and how they were chosen, in words for a message or the
// log: "the servers in /usr/bin, found on PATH", "the servers in /x, from
// the Beebium setting", "the servers in /opt/beebium/bin".
QString describe_servers(const ServerLocation& servers);

// The line the client logs at every launch: the servers it will use and how
// it chose them, or that it found none.
QString servers_log_line(const std::optional<ServerLocation>& servers);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_SERVERS_SERVER_LOCATOR_HPP
