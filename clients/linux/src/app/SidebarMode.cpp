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

#include "app/SidebarMode.hpp"

#include <QApplication>
#include <QObject>
#include <QStyle>

namespace beebium::linux_client {

QString sidebar_mode_label(SidebarMode mode) {
    switch (mode) {
    case SidebarMode::storage: return QObject::tr("Storage");
    case SidebarMode::memory: return QObject::tr("Memory");
    case SidebarMode::peripherals: return QObject::tr("Peripherals");
    case SidebarMode::video: return QObject::tr("Video");
    case SidebarMode::sound: return QObject::tr("Sound");
    case SidebarMode::keyboard: return QObject::tr("Keyboard");
    case SidebarMode::processor: return QObject::tr("Processor");
    case SidebarMode::network: return QObject::tr("Network");
    }
    return {};
}

QIcon sidebar_mode_icon(SidebarMode mode) {
    // Freedesktop icon names, with the style's own where a theme lacks one.
    const auto themed = [](const char* name, QStyle::StandardPixmap fallback) {
        return QIcon::fromTheme(QString::fromLatin1(name),
                                QApplication::style()->standardIcon(fallback));
    };
    switch (mode) {
    case SidebarMode::storage: return themed("drive-harddisk", QStyle::SP_DriveHDIcon);
    case SidebarMode::memory: return themed("media-flash", QStyle::SP_DriveFDIcon);
    case SidebarMode::peripherals: return themed("preferences-plugin", QStyle::SP_DirIcon);
    case SidebarMode::video: return themed("video-display", QStyle::SP_ComputerIcon);
    case SidebarMode::sound: return themed("audio-volume-high", QStyle::SP_MediaVolume);
    case SidebarMode::keyboard: return themed("input-keyboard", QStyle::SP_ComputerIcon);
    case SidebarMode::processor: return themed("cpu", QStyle::SP_ComputerIcon);
    case SidebarMode::network: return themed("network-wired", QStyle::SP_DriveNetIcon);
    }
    return {};
}

bool shows_server_state(SidebarMode mode) {
    return mode != SidebarMode::video && mode != SidebarMode::keyboard;
}

std::vector<SidebarMode> offered_sidebar_modes() {
    return {SidebarMode::sound};
}

StalePresentation stale_presentation(SidebarMode mode, ConnectionVerdict verdict) {
    if (!shows_server_state(mode) || is_live(verdict)) {
        return {};
    }
    const QString last_known = QObject::tr("Showing the last known state. Controls are disabled.");
    switch (verdict) {
    case ConnectionVerdict::died:
        return {true, QObject::tr("Emulator stopped unexpectedly"), last_known};
    case ConnectionVerdict::stopped:
        return {true, QObject::tr("Emulator stopped"), last_known};
    case ConnectionVerdict::could_not_reconnect:
        return {true, QObject::tr("Couldn't reconnect"), last_known};
    case ConnectionVerdict::unreachable:
    case ConnectionVerdict::reconnecting:
        return {true, QObject::tr("Connection lost"),
                QObject::tr("Showing the last known state. Controls are disabled until the "
                            "connection returns.")};
    case ConnectionVerdict::live:
        break;
    }
    return {};
}

}  // namespace beebium::linux_client
