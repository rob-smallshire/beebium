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

#ifndef BEEBIUM_LINUX_APP_APPLICATION_INFO_HPP
#define BEEBIUM_LINUX_APP_APPLICATION_INFO_HPP

#include <QIcon>

#include <string>
#include <string_view>

namespace beebium::linux_client {

// This client's release version, the same number as the servers it pairs with.
std::string_view client_version();

// The wire-protocol fingerprint compiled into this client. A server whose
// SystemInfo.protocol_fingerprint differs is refused at connect.
std::string_view client_protocol_fingerprint();

// One line for --version: the program, its version and its protocol.
std::string version_text();

// The client's icon: the installed org.beebium.Beebium from the icon theme,
// or, run without the package installed, the copy built into the program.
QIcon application_icon();

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_APPLICATION_INFO_HPP
