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

#include "app/DeepLink.hpp"

#include <QUrlQuery>

namespace beebium::linux_client {

std::optional<ConnectLink> parse_connect_link(const QUrl& url) {
    if (url.scheme().compare(QLatin1String("beebium"), Qt::CaseInsensitive) != 0 ||
        url.host().compare(QLatin1String("connect"), Qt::CaseInsensitive) != 0) {
        return std::nullopt;
    }
    const QUrlQuery query(url);
    const QString host = query.queryItemValue(QStringLiteral("host"), QUrl::FullyDecoded);
    if (host.isEmpty()) {
        return std::nullopt;
    }
    QString text = host.contains(QLatin1Char(':')) ? QStringLiteral("[%1]").arg(host) : host;
    if (query.hasQueryItem(QStringLiteral("port"))) {
        text += QLatin1Char(':') + query.queryItemValue(QStringLiteral("port"));
    }
    const auto endpoint = parse_endpoint(text.toStdString());
    if (!endpoint) {
        return std::nullopt;
    }
    ConnectLink link{*endpoint, {}, {}, {}};
    const auto optional_item = [&query](const char* name) -> std::optional<QString> {
        const QString key = QString::fromLatin1(name);
        if (!query.hasQueryItem(key)) {
            return std::nullopt;
        }
        return query.queryItemValue(key, QUrl::FullyDecoded);
    };
    link.run = optional_item("run");
    link.provenance = optional_item("provenance");
    link.sidebar = optional_item("sidebar");
    return link;
}

}  // namespace beebium::linux_client
