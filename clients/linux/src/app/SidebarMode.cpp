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

QString sidebar_mode_icon_name(SidebarMode mode) {
    switch (mode) {
    case SidebarMode::storage: return QStringLiteral("drive-harddisk");
    case SidebarMode::memory: return QStringLiteral("media-flash");
    case SidebarMode::peripherals: return QStringLiteral("preferences-plugin");
    case SidebarMode::video: return QStringLiteral("video-display");
    case SidebarMode::sound: return QStringLiteral("audio-card");
    case SidebarMode::keyboard: return QStringLiteral("input-keyboard");
    case SidebarMode::processor: return QStringLiteral("cpu");
    case SidebarMode::network: return QStringLiteral("network-wired");
    }
    return {};
}

QIcon sidebar_mode_icon(SidebarMode mode) {
    const auto fallback = [mode] {
        switch (mode) {
        case SidebarMode::storage: return QStyle::SP_DriveHDIcon;
        case SidebarMode::memory: return QStyle::SP_DriveFDIcon;
        case SidebarMode::peripherals: return QStyle::SP_DirIcon;
        case SidebarMode::sound: return QStyle::SP_MediaVolume;
        case SidebarMode::network: return QStyle::SP_DriveNetIcon;
        case SidebarMode::video:
        case SidebarMode::keyboard:
        case SidebarMode::processor: break;
        }
        return QStyle::SP_ComputerIcon;
    };
    return QIcon::fromTheme(sidebar_mode_icon_name(mode),
                            QApplication::style()->standardIcon(fallback()));
}

bool shows_server_state(SidebarMode mode) {
    return mode != SidebarMode::video && mode != SidebarMode::keyboard;
}

std::vector<SidebarMode> offered_sidebar_modes() {
    return {SidebarMode::storage, SidebarMode::video, SidebarMode::sound, SidebarMode::keyboard,
            SidebarMode::processor};
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
