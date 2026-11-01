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

#ifndef BEEBIUM_LINUX_APP_DEEP_LINK_HPP
#define BEEBIUM_LINUX_APP_DEEP_LINK_HPP

#include "connection/Endpoint.hpp"

#include <QString>
#include <QUrl>

#include <optional>

namespace beebium::linux_client {

// beebium://connect?host=H&port=P[&run=..][&provenance=..][&sidebar=..]
// (client specification 4.1): attach a window to a server without discovery
// or the Connect dialog, including one on an un-advertised port.
struct ConnectLink {
    Endpoint endpoint;
    std::optional<QString> run;
    std::optional<QString> provenance;
    std::optional<QString> sidebar;
};

// Nullopt unless `url` is a well-formed connect link: scheme beebium, host
// "connect", a host parameter, and a port that is valid when given.
std::optional<ConnectLink> parse_connect_link(const QUrl& url);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_DEEP_LINK_HPP
