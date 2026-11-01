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

#include "servers/ServerLocator.hpp"

#include <QObject>

#include <QDir>
#include <QFileInfo>

namespace beebium::linux_client {

namespace {

const QString ANCHOR = QStringLiteral("beebium-model-b");

}  // namespace

QString ServerLocation::executable_filepath(const QString& model) const {
    return QDir(servers_dirpath).filePath(QStringLiteral("beebium-") + model);
}

std::optional<ServerLocation> locate_servers(
    const QString& configured, const QStringList& path_dirpaths,
    const QStringList& fallback_dirpaths,
    const std::function<bool(const QString& filepath)>& is_executable) {
    const auto holds_servers = [&](const QString& dirpath) {
        return !dirpath.isEmpty() && is_executable(QDir(dirpath).filePath(ANCHOR));
    };
    if (!configured.isEmpty()) {
        // An explicit choice is honoured or reported, never silently passed over.
        if (holds_servers(configured)) {
            return ServerLocation{configured, QStringLiteral("settings")};
        }
        return std::nullopt;
    }
    for (const QString& dirpath : path_dirpaths) {
        if (holds_servers(dirpath)) {
            return ServerLocation{dirpath, QStringLiteral("PATH")};
        }
    }
    for (const QString& dirpath : fallback_dirpaths) {
        if (holds_servers(dirpath)) {
            return ServerLocation{dirpath, QStringLiteral("default")};
        }
    }
    return std::nullopt;
}

std::optional<ServerLocation> locate_servers_on_this_host(const QString& configured) {
    const QStringList path_dirpaths =
        qEnvironmentVariable("PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return locate_servers(configured, path_dirpaths, {QStringLiteral("/opt/beebium/bin")},
                          [](const QString& filepath) {
                              const QFileInfo info(filepath);
                              return info.isFile() && info.isExecutable();
                          });
}

QString describe_servers(const ServerLocation& servers) {
    const QString dirpath = servers.servers_dirpath;
    if (servers.found_by == QLatin1String("settings")) {
        return QObject::tr("the servers in %1, from the Beebium setting").arg(dirpath);
    }
    if (servers.found_by == QLatin1String("PATH")) {
        return QObject::tr("the servers in %1, found on PATH").arg(dirpath);
    }
    return QObject::tr("the servers in %1").arg(dirpath);
}

QString servers_log_line(const std::optional<ServerLocation>& servers) {
    if (!servers) {
        return QStringLiteral("servers: none found (no Beebium setting, none on PATH or in "
                              "/opt/beebium/bin)");
    }
    return QStringLiteral("servers: ") + describe_servers(*servers);
}

}  // namespace beebium::linux_client
