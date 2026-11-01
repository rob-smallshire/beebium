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

#ifndef BEEBIUM_LINUX_CONNECTION_ENDPOINT_HPP
#define BEEBIUM_LINUX_CONNECTION_ENDPOINT_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::linux_client {

// Where a server listens (client specification 4.1).
struct Endpoint {
    static constexpr std::uint16_t DEFAULT_PORT = 48875;

    std::string host;  // a name, an IPv4 address or an IPv6 address (no brackets)
    std::uint16_t port = DEFAULT_PORT;

    // "host:port", with an IPv6 address bracketed: what the user sees and
    // types, and what gRPC dials.
    std::string to_string() const;

    // The same server however it was spelt: the host lower-cased, so
    // "LocalHost:48875" and "localhost" are one endpoint.
    std::string key() const;

    bool operator==(const Endpoint&) const = default;
};

// Parse what the user typed: "host:port" with the port 1..65535, a bare host
// with the default port, or a bracketed IPv6 address with or without a
// port. Leading and trailing spaces are ignored. Nullopt for anything else,
// which disables the Connect action (4.1).
std::optional<Endpoint> parse_endpoint(std::string_view text);

// The recent connections list after a successful connection to `added`:
// it first, an earlier entry for the same endpoint dropped, at most `cap`.
std::vector<Endpoint> updated_recents(const std::vector<Endpoint>& recents, const Endpoint& added,
                                      std::size_t cap);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_ENDPOINT_HPP
