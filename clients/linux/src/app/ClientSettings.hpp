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
#include "connection/ScreenReading.hpp"
#include "display/VideoSettings.hpp"

#include <QSettings>
#include <QStringList>

#include <memory>
#include <vector>

namespace beebium::linux_client {

// The client's own settings, which persist across runs (client
// specification 3.5): never machine or server state, which the server owns.
// Stored with QSettings, so in ~/.config/Beebium/Beebium.conf.
class ClientSettings {
public:
    static constexpr std::size_t RECENT_CONNECTIONS_CAP = 10;
    static constexpr std::size_t RECENT_MACHINES_CAP = 20;
    static constexpr std::size_t RECENT_DISCS_CAP = 10;

    // The user's settings in the standard place.
    ClientSettings();
    // Settings in a given file, for tests.
    explicit ClientSettings(const QString& filepath);

    // Successful connections, most recent first (4.1).
    std::vector<Endpoint> recent_connections() const;
    void remember_connection(const Endpoint& endpoint);

    // Saved machine directories opened or saved here, most recent first, for
    // the home window's saved machines (5.3, 13.4). Paths are kept cleaned,
    // so one machine is one entry however its path was spelt.
    QStringList recent_machines() const;
    void remember_machine(const QString& machine_dirpath);
    void forget_machine(const QString& machine_dirpath);
    void clear_machines();

    // Disc images inserted by the user here, most recent first, shared by
    // every drive (client specification 9.2): only an insert the server
    // accepted, never a disc a preset mounted. Paths are the identity.
    QStringList recent_discs() const;
    void remember_disc(const QString& filepath);
    void clear_discs();

    // Where this host's servers are, when the search should not decide;
    // empty to search (docs/deployment.md).
    QString servers_dirpath() const;
    void set_servers_dirpath(const QString& dirpath);
    // A ROM directory for launched servers, overriding their own; empty for
    // their own.
    QString rom_dirpath() const;
    void set_rom_dirpath(const QString& dirpath);

    // The video settings a new machine window starts with (client
    // specification 6.6), as the macOS client's defaults: the style, the
    // pixel shape and the boxing colour. The edge margin is per window and
    // always starts at its default. Open windows never follow a change.
    VideoSettings video_defaults() const;
    void set_video_defaults(const VideoSettings& defaults);

    // How a copy of Mode 7 text renders a byte (client specification 6.5):
    // as its character code, the default, or as the glyph displayed. Read
    // afresh at each copy, so every window follows a change at once.
    Mode7Characters mode7_characters() const;
    void set_mode7_characters(Mode7Characters characters);

    QSettings& store() { return *settings_; }

private:
    std::unique_ptr<QSettings> settings_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CLIENT_SETTINGS_HPP
