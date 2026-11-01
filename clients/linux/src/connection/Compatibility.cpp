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

#include "connection/Compatibility.hpp"

namespace beebium::linux_client {

ProtocolVerdict assess_protocol(std::string_view server_fingerprint,
                                std::string_view client_fingerprint) {
    if (server_fingerprint.empty()) {
        return ProtocolVerdict::server_reports_none;
    }
    return server_fingerprint == client_fingerprint ? ProtocolVerdict::compatible
                                                    : ProtocolVerdict::mismatch;
}

bool is_compatible(ProtocolVerdict verdict) {
    return verdict == ProtocolVerdict::compatible;
}

bool is_server_local(std::string_view server_host_fingerprint,
                     std::string_view client_host_fingerprint) {
    if (server_host_fingerprint.empty() || client_host_fingerprint.empty()) {
        return false;
    }
    return server_host_fingerprint == client_host_fingerprint;
}

}  // namespace beebium::linux_client
