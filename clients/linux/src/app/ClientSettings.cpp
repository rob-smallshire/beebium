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

#include <QDir>
#include <QStringList>

namespace beebium::linux_client {

namespace {

const QString RECENT_CONNECTIONS_KEY = QStringLiteral("connections/recent");
const QString RECENT_MACHINES_KEY = QStringLiteral("machines/recent");
const QString VIDEO_STYLE_KEY = QStringLiteral("video/defaultStyle");
const QString MODE7_CHARACTERS_KEY = QStringLiteral("screenText/mode7Characters");
const QString VIDEO_PIXELS_KEY = QStringLiteral("video/defaultPixels");
const QString VIDEO_BOXING_KEY = QStringLiteral("video/defaultBoxingColour");
const QString RECENT_DISCS_KEY = QStringLiteral("discs/recent");

QString cleaned(const QString& dirpath) {
    return QDir::cleanPath(dirpath);
}

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

QStringList ClientSettings::recent_machines() const {
    return settings_->value(RECENT_MACHINES_KEY).toStringList();
}

void ClientSettings::remember_machine(const QString& machine_dirpath) {
    QStringList recents = recent_machines();
    const QString path = cleaned(machine_dirpath);
    recents.removeAll(path);
    recents.prepend(path);
    while (static_cast<std::size_t>(recents.size()) > RECENT_MACHINES_CAP) {
        recents.removeLast();
    }
    settings_->setValue(RECENT_MACHINES_KEY, recents);
    settings_->sync();
}

void ClientSettings::forget_machine(const QString& machine_dirpath) {
    QStringList recents = recent_machines();
    recents.removeAll(cleaned(machine_dirpath));
    settings_->setValue(RECENT_MACHINES_KEY, recents);
    settings_->sync();
}

void ClientSettings::clear_machines() {
    settings_->remove(RECENT_MACHINES_KEY);
    settings_->sync();
}

VideoSettings ClientSettings::video_defaults() const {
    VideoSettings defaults;
    const QString style = settings_->value(VIDEO_STYLE_KEY).toString();
    if (style == QStringLiteral("debug")) {
        defaults.style = DisplayStyle::debug;
    }
    const QString pixels = settings_->value(VIDEO_PIXELS_KEY).toString();
    if (pixels == QStringLiteral("crisp")) {
        defaults.pixels = PixelShape::crisp;
    }
    // "#rrggbb", as the macOS client keeps its boxing colour.
    const QString boxing = settings_->value(VIDEO_BOXING_KEY).toString();
    if (boxing.size() == 7 && boxing.startsWith(QLatin1Char('#'))) {
        bool ok = false;
        const uint rgb = boxing.mid(1).toUInt(&ok, 16);
        if (ok) {
            defaults.boxing = Rgb{static_cast<std::uint8_t>(rgb >> 16),
                                  static_cast<std::uint8_t>(rgb >> 8),
                                  static_cast<std::uint8_t>(rgb)};
        }
    }
    return defaults;
}

void ClientSettings::set_video_defaults(const VideoSettings& defaults) {
    settings_->setValue(VIDEO_STYLE_KEY, defaults.style == DisplayStyle::debug
                                             ? QStringLiteral("debug")
                                             : QStringLiteral("standard"));
    settings_->setValue(VIDEO_PIXELS_KEY, defaults.pixels == PixelShape::crisp
                                              ? QStringLiteral("crisp")
                                              : QStringLiteral("authentic"));
    settings_->setValue(VIDEO_BOXING_KEY, QStringLiteral("#%1%2%3")
                                              .arg(defaults.boxing.r, 2, 16, QLatin1Char('0'))
                                              .arg(defaults.boxing.g, 2, 16, QLatin1Char('0'))
                                              .arg(defaults.boxing.b, 2, 16, QLatin1Char('0')));
    settings_->sync();
}

QStringList ClientSettings::recent_discs() const {
    return settings_->value(RECENT_DISCS_KEY).toStringList();
}

void ClientSettings::remember_disc(const QString& filepath) {
    QStringList recents = recent_discs();
    const QString path = cleaned(filepath);
    recents.removeAll(path);
    recents.prepend(path);
    while (static_cast<std::size_t>(recents.size()) > RECENT_DISCS_CAP) {
        recents.removeLast();
    }
    settings_->setValue(RECENT_DISCS_KEY, recents);
    settings_->sync();
}

void ClientSettings::clear_discs() {
    settings_->remove(RECENT_DISCS_KEY);
    settings_->sync();
}

QString ClientSettings::servers_dirpath() const {
    return settings_->value(QStringLiteral("servers/dirpath")).toString();
}

void ClientSettings::set_servers_dirpath(const QString& dirpath) {
    settings_->setValue(QStringLiteral("servers/dirpath"), dirpath);
    settings_->sync();
}

Mode7Characters ClientSettings::mode7_characters() const {
    // Anything but the displayed glyphs, an unknown value included, is codes.
    return settings_->value(MODE7_CHARACTERS_KEY).toString() == QStringLiteral("displayed")
               ? Mode7Characters::displayed
               : Mode7Characters::codes;
}

void ClientSettings::set_mode7_characters(Mode7Characters characters) {
    settings_->setValue(MODE7_CHARACTERS_KEY, characters == Mode7Characters::displayed
                                                  ? QStringLiteral("displayed")
                                                  : QStringLiteral("codes"));
    settings_->sync();
}

QString ClientSettings::rom_dirpath() const {
    return settings_->value(QStringLiteral("servers/romDirpath")).toString();
}

void ClientSettings::set_rom_dirpath(const QString& dirpath) {
    settings_->setValue(QStringLiteral("servers/romDirpath"), dirpath);
    settings_->sync();
}

}  // namespace beebium::linux_client
