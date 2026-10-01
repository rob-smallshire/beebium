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

#include "AunMapFile.hpp"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <fstream>
#include <map>
#include <sstream>

namespace beebium {

namespace {

// Net-number ranges, kept in one place. A net is the full Econet net byte
// (0..255), which a guest can address; the RISC OS AUNMap convention uses high
// net numbers (e.g. 128) for IP-mapped subnets. The dest_net=0 -> local-net
// translation is unchanged, so a guest addressing net 128 goes through as 128.
constexpr long kPeerNetMin = 0;
constexpr long kPeerNetMax = 255;
constexpr long kSubnetNetMin = 0;
constexpr long kSubnetNetMax = 255;

// Pull an integer field in [lo, hi] from a JSON object, or set `error`.
bool get_int_field(const nlohmann::json& obj, const char* key, long lo, long hi,
                   const std::string& where, long& out, std::string& error) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        error = where + ": missing '" + key + "'";
        return false;
    }
    if (!it->is_number_integer() && !it->is_number_unsigned()) {
        error = where + ": '" + key + "' must be an integer";
        return false;
    }
    long value = it->get<long>();
    if (value < lo || value > hi) {
        error = where + ": '" + key + "' " + std::to_string(value) +
                " out of range " + std::to_string(lo) + ".." + std::to_string(hi);
        return false;
    }
    out = value;
    return true;
}

bool get_string_field(const nlohmann::json& obj, const char* key,
                      const std::string& where, bool required, std::string& out,
                      std::string& error) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        if (required) {
            error = where + ": missing '" + key + "'";
            return false;
        }
        out.clear();
        return true;
    }
    if (!it->is_string()) {
        error = where + ": '" + key + "' must be a string";
        return false;
    }
    out = it->get<std::string>();
    if (required && out.empty()) {
        error = where + ": '" + key + "' must not be empty";
        return false;
    }
    return true;
}

// Parse "a.b.c.d/24" into the /24 network address (network byte order, last
// octet cleared). Only /24 is supported (section 4). Returns false + error
// otherwise.
bool parse_subnet_cidr(const std::string& text, const std::string& where,
                       std::uint32_t& base_ip_out, std::string& error) {
    auto slash = text.find('/');
    if (slash == std::string::npos) {
        error = where + ": subnet '" + text + "' must be a.b.c.0/24";
        return false;
    }
    std::string addr_text = text.substr(0, slash);
    std::string prefix_text = text.substr(slash + 1);
    if (prefix_text != "24") {
        error = where + ": subnet '" + text +
                "' must be a /24 (only the station-is-last-octet convention is "
                "supported)";
        return false;
    }
    in_addr addr{};
    if (inet_pton(AF_INET, addr_text.c_str(), &addr) != 1) {
        error = where + ": subnet '" + text + "' has an invalid IPv4 address";
        return false;
    }
    // Mask to the /24 network address so the last octet is cleared regardless
    // of what was written.
    base_ip_out = addr.s_addr & htonl(0xFFFFFF00u);
    return true;
}

}  // namespace

AunMapLoadResult parse_aun_map(std::string_view json_text,
                               const std::string& source_name) {
    AunMapLoadResult result;

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(json_text);
    } catch (const nlohmann::json::parse_error& e) {
        result.map = std::nullopt;
        result.error = source_name + ": invalid JSON at byte " +
                       std::to_string(e.byte) + ": " + e.what();
        return result;
    }
    if (!json.is_object()) {
        result.error = source_name + ": top level must be a JSON object";
        return result;
    }

    AunMap map;

    // peers[] -- one entry per (net, station); a duplicate names both.
    std::map<std::uint16_t, std::size_t> seen_peers;  // (net<<8|stn) -> index
    if (auto peers_it = json.find("peers"); peers_it != json.end()) {
        if (!peers_it->is_array()) {
            result.error = source_name + ": 'peers' must be an array";
            return result;
        }
        for (std::size_t i = 0; i < peers_it->size(); ++i) {
            const auto& entry = (*peers_it)[i];
            const std::string where =
                source_name + ": peers[" + std::to_string(i) + "]";
            if (!entry.is_object()) {
                result.error = where + ": must be an object";
                return result;
            }
            long net = 0, stn = 0, port = 0;
            std::string host, label;
            if (!get_int_field(entry, "net", kPeerNetMin, kPeerNetMax, where, net, result.error) ||
                !get_int_field(entry, "station", 1, 254, where, stn,
                               result.error) ||
                !get_string_field(entry, "host", where, true, host,
                                  result.error) ||
                !get_int_field(entry, "port", 1, 65535, where, port,
                               result.error) ||
                !get_string_field(entry, "label", where, false, label,
                                  result.error)) {
                return result;
            }
            auto key = static_cast<std::uint16_t>((net << 8) | stn);
            if (auto prior = seen_peers.find(key); prior != seen_peers.end()) {
                result.error = where + " duplicates (net " + std::to_string(net) +
                               ", station " + std::to_string(stn) +
                               ") first seen at peers[" +
                               std::to_string(prior->second) + "]";
                return result;
            }
            seen_peers.emplace(key, i);
            map.peers.push_back(AunMapPeer{
                static_cast<std::uint8_t>(net), static_cast<std::uint8_t>(stn),
                std::move(host), static_cast<std::uint16_t>(port),
                std::move(label)});
        }
    }

    // subnets[] -- one entry per net (a net maps to one /24); a duplicate names
    // both.
    std::map<std::uint8_t, std::size_t> seen_subnets;
    if (auto subnets_it = json.find("subnets"); subnets_it != json.end()) {
        if (!subnets_it->is_array()) {
            result.error = source_name + ": 'subnets' must be an array";
            return result;
        }
        for (std::size_t i = 0; i < subnets_it->size(); ++i) {
            const auto& entry = (*subnets_it)[i];
            const std::string where =
                source_name + ": subnets[" + std::to_string(i) + "]";
            if (!entry.is_object()) {
                result.error = where + ": must be an object";
                return result;
            }
            long net = 0;
            std::string subnet_text, label;
            // A subnet maps a whole net to a /24; the RISC OS AUNMap convention
            // uses high net numbers (e.g. 128) for these IP-mapped nets, so the
            // 0..127 cap that applies to explicit peers does not apply here.
            if (!get_int_field(entry, "net", kSubnetNetMin, kSubnetNetMax, where, net, result.error) ||
                !get_string_field(entry, "subnet", where, true, subnet_text,
                                  result.error) ||
                !get_string_field(entry, "label", where, false, label,
                                  result.error)) {
                return result;
            }
            std::uint32_t base_ip = 0;
            if (!parse_subnet_cidr(subnet_text, where, base_ip, result.error)) {
                return result;
            }
            if (auto prior = seen_subnets.find(static_cast<std::uint8_t>(net));
                prior != seen_subnets.end()) {
                result.error = where + " duplicates net " + std::to_string(net) +
                               " first seen at subnets[" +
                               std::to_string(prior->second) + "]";
                return result;
            }
            seen_subnets.emplace(static_cast<std::uint8_t>(net), i);
            map.subnets.push_back(AunMapSubnet{static_cast<std::uint8_t>(net),
                                               base_ip, std::move(subnet_text),
                                               std::move(label)});
        }
    }

    // Unknown top-level and per-entry keys are ignored on load by construction
    // (we read only the keys we know).
    result.map = std::move(map);
    return result;
}

AunMapLoadResult load_aun_map(const std::filesystem::path& filepath) {
    AunMapLoadResult result;
    std::error_code ec;
    if (!std::filesystem::exists(filepath, ec) || ec) {
        result.file_present = false;
        result.map = AunMap{};  // absent is not an error -- an empty world
        return result;
    }
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        result.map = std::nullopt;
        result.error = filepath.string() + ": could not open the file";
        return result;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parse_aun_map(buffer.str(), filepath.string());
}

}  // namespace beebium
