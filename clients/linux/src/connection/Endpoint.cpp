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

#include "connection/Endpoint.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace beebium::linux_client {

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<std::uint16_t> parse_port(std::string_view text) {
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 1 || value > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

bool valid_host(std::string_view host) {
    if (host.empty()) {
        return false;
    }
    return std::all_of(host.begin(), host.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
    });
}

}  // namespace

std::string Endpoint::to_string() const {
    const bool ipv6 = host.find(':') != std::string::npos;
    return (ipv6 ? "[" + host + "]" : host) + ":" + std::to_string(port);
}

std::string Endpoint::key() const {
    std::string lowered = host;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    Endpoint normal{lowered, port};
    return normal.to_string();
}

std::optional<Endpoint> parse_endpoint(std::string_view text) {
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    if (text.front() == '[') {
        const auto close = text.find(']');
        if (close == std::string_view::npos || close == 1) {
            return std::nullopt;
        }
        Endpoint endpoint{std::string(text.substr(1, close - 1)), Endpoint::DEFAULT_PORT};
        const auto rest = text.substr(close + 1);
        if (rest.empty()) {
            return endpoint;
        }
        if (rest.front() != ':') {
            return std::nullopt;
        }
        const auto port = parse_port(rest.substr(1));
        if (!port) {
            return std::nullopt;
        }
        endpoint.port = *port;
        return endpoint;
    }
    const auto colon = text.rfind(':');
    if (colon == std::string_view::npos) {
        return valid_host(text) ? std::optional<Endpoint>({std::string(text), Endpoint::DEFAULT_PORT})
                                : std::nullopt;
    }
    if (text.find(':') != colon) {
        // More than one colon without brackets: an IPv6 address alone.
        return Endpoint{std::string(text), Endpoint::DEFAULT_PORT};
    }
    const auto host = text.substr(0, colon);
    const auto port = parse_port(text.substr(colon + 1));
    if (!valid_host(host) || !port) {
        return std::nullopt;
    }
    return Endpoint{std::string(host), *port};
}

std::vector<Endpoint> updated_recents(const std::vector<Endpoint>& recents, const Endpoint& added,
                                      std::size_t cap) {
    std::vector<Endpoint> updated{added};
    for (const auto& recent : recents) {
        if (updated.size() >= cap) {
            break;
        }
        if (recent.key() != added.key()) {
            updated.push_back(recent);
        }
    }
    if (updated.size() > cap) {
        updated.resize(cap);
    }
    return updated;
}

}  // namespace beebium::linux_client
