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

#ifndef BEEBIUM_LINUX_APP_CLIENT_SETTINGS_HPP
#define BEEBIUM_LINUX_APP_CLIENT_SETTINGS_HPP

#include "connection/Endpoint.hpp"

#include <QSettings>

#include <memory>
#include <vector>

namespace beebium::linux_client {

// The client's own settings, which persist across runs (client
// specification 3.5): never machine or server state, which the server owns.
// Stored with QSettings, so in ~/.config/Beebium/Beebium.conf.
class ClientSettings {
public:
    static constexpr std::size_t RECENT_CONNECTIONS_CAP = 10;

    // The user's settings in the standard place.
    ClientSettings();
    // Settings in a given file, for tests.
    explicit ClientSettings(const QString& filepath);

    // Successful connections, most recent first (4.1).
    std::vector<Endpoint> recent_connections() const;
    void remember_connection(const Endpoint& endpoint);

    QSettings& store() { return *settings_; }

private:
    std::unique_ptr<QSettings> settings_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CLIENT_SETTINGS_HPP
