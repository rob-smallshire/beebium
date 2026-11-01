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

#ifndef BEEBIUM_LINUX_CONNECTION_COMPATIBILITY_HPP
#define BEEBIUM_LINUX_CONNECTION_COMPATIBILITY_HPP

#include <string_view>

namespace beebium::linux_client {

// The protocol handshake's verdict (client specification 4.2).
enum class ProtocolVerdict {
    compatible,
    // Both report a fingerprint and they differ.
    mismatch,
    // The server reports none: too old to say, so it counts as a mismatch.
    server_reports_none,
};

// Compare the server's SystemInfo.protocol_fingerprint with ours.
ProtocolVerdict assess_protocol(std::string_view server_fingerprint,
                                std::string_view client_fingerprint);

// True only for ProtocolVerdict::compatible.
bool is_compatible(ProtocolVerdict verdict);

// Whether the server can open paths this client exchanges with it (client
// specification 3.2): both host fingerprints known and equal. Fails closed:
// an empty fingerprint on either side means not local, and two empty ones are
// not a match.
bool is_server_local(std::string_view server_host_fingerprint,
                     std::string_view client_host_fingerprint);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_COMPATIBILITY_HPP
