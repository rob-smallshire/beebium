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

#include "app/ApplicationInfo.hpp"

#include "beebium/service/ProtocolFingerprint.hpp"

#include <QString>

namespace beebium::linux_client {

std::string_view client_version() {
    return BEEBIUM_LINUX_CLIENT_VERSION;
}

std::string_view client_protocol_fingerprint() {
    return beebium::service::PROTOCOL_FINGERPRINT;
}

std::string version_text() {
    std::string text = "beebium ";
    text.append(client_version());
    text.append(" (protocol ");
    text.append(client_protocol_fingerprint());
    text.append(")");
    return text;
}

QIcon application_icon() {
    // The package's hicolor sizes, built in; the 512 is left to the package.
    QIcon built_in;
    for (const int size : {16, 22, 24, 32, 48, 64, 128, 256}) {
        built_in.addFile(QStringLiteral(":/icons/%1x%1/apps/org.beebium.Beebium.png").arg(size),
                         QSize(size, size));
    }
    return QIcon::fromTheme(QStringLiteral("org.beebium.Beebium"), built_in);
}

}  // namespace beebium::linux_client
