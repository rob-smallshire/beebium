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

#include "app/ClientSettings.hpp"

#include <QStringList>

namespace beebium::linux_client {

namespace {

const QString RECENT_CONNECTIONS_KEY = QStringLiteral("connections/recent");

}  // namespace

ClientSettings::ClientSettings()
    : settings_(std::make_unique<QSettings>(QStringLiteral("Beebium"), QStringLiteral("Beebium"))) {}

ClientSettings::ClientSettings(const QString& filepath)
    : settings_(std::make_unique<QSettings>(filepath, QSettings::IniFormat)) {}

std::vector<Endpoint> ClientSettings::recent_connections() const {
    std::vector<Endpoint> recents;
    for (const QString& text : settings_->value(RECENT_CONNECTIONS_KEY).toStringList()) {
        // A hand-edited or stale entry that no longer parses is skipped.
        if (auto endpoint = parse_endpoint(text.toStdString())) {
            recents.push_back(*endpoint);
        }
    }
    return recents;
}

void ClientSettings::remember_connection(const Endpoint& endpoint) {
    QStringList texts;
    for (const auto& recent :
         updated_recents(recent_connections(), endpoint, RECENT_CONNECTIONS_CAP)) {
        texts << QString::fromStdString(recent.to_string());
    }
    settings_->setValue(RECENT_CONNECTIONS_KEY, texts);
    settings_->sync();
}

}  // namespace beebium::linux_client
