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

#include "AunMapWriter.hpp"

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
#include <random>
#include <sstream>
#include <system_error>

namespace beebium {

namespace {

using nlohmann::ordered_json;

// Validate a /24 CIDR, naming the field on failure. Mirrors the parser's rule:
// only /24 is supported (the one net-level mapping with a unique endpoint per
// station).
std::string validate_subnet(const std::string& text) {
    auto slash = text.find('/');
    if (slash == std::string::npos || text.substr(slash + 1) != "24") {
        return "subnet must be a.b.c.0/24";
    }
    in_addr addr{};
    if (inet_pton(AF_INET, text.substr(0, slash).c_str(), &addr) != 1) {
        return "subnet has an invalid IPv4 address";
    }
    return {};
}

// Re-emit one entry with `key_order` first (those present), then any other keys
// as they were -- the stable key order the converter writes.
ordered_json canonical_entry(const ordered_json& entry,
                             const std::vector<std::string>& key_order) {
    ordered_json out = ordered_json::object();
    for (const auto& key : key_order) {
        if (entry.contains(key)) {
            out[key] = entry.at(key);
        }
    }
    for (const auto& [key, value] : entry.items()) {
        if (!out.contains(key)) {
            out[key] = value;
        }
    }
    return out;
}

}  // namespace

AunMapDocument AunMapDocument::empty() {
    ordered_json doc = ordered_json::object();
    doc["peers"] = ordered_json::array();
    doc["subnets"] = ordered_json::array();
    return AunMapDocument(std::move(doc));
}

AunMapDocumentLoad AunMapDocument::load(
        const std::filesystem::path& filepath) {
    AunMapDocumentLoad result;
    std::error_code ec;
    if (!std::filesystem::exists(filepath, ec) || ec) {
        result.file_present = false;
        result.document = empty();
        return result;
    }
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        result.error = filepath.string() + ": could not open the file";
        return result;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    ordered_json doc;
    try {
        doc = ordered_json::parse(buffer.str());
    } catch (const nlohmann::json::parse_error& e) {
        result.error = filepath.string() + ": invalid JSON at byte " +
                       std::to_string(e.byte);
        return result;
    }
    if (!doc.is_object()) {
        result.error = filepath.string() + ": top level must be a JSON object";
        return result;
    }
    result.document = AunMapDocument(std::move(doc));
    return result;
}

std::string AunMapDocument::add_or_replace_peer(std::uint8_t net,
                                                std::uint8_t stn,
                                                const std::string& host,
                                                std::uint16_t port,
                                                const std::string& label) {
    if (stn < 1 || stn > 254) {
        return "station must be 1-254";
    }
    if (host.empty()) {
        return "host must not be empty";
    }
    if (port < 1) {
        return "port must be 1-65535";
    }
    if (!doc_.contains("peers") || !doc_["peers"].is_array()) {
        doc_["peers"] = ordered_json::array();
    }
    // Replace in place (keeping position and unknown keys) if present.
    for (auto& entry : doc_["peers"]) {
        if (entry.is_object() && entry.value("net", -1) == net &&
            entry.value("station", -1) == stn) {
            entry["host"] = host;
            entry["port"] = port;
            if (label.empty()) {
                entry.erase("label");
            } else {
                entry["label"] = label;
            }
            return {};
        }
    }
    ordered_json added = ordered_json::object();
    added["net"] = net;
    added["station"] = stn;
    added["host"] = host;
    added["port"] = port;
    if (!label.empty()) {
        added["label"] = label;
    }
    doc_["peers"].push_back(std::move(added));
    return {};
}

bool AunMapDocument::remove_peer(std::uint8_t net, std::uint8_t stn) {
    if (!doc_.contains("peers") || !doc_["peers"].is_array()) {
        return false;
    }
    auto& peers = doc_["peers"];
    for (auto it = peers.begin(); it != peers.end(); ++it) {
        if (it->is_object() && it->value("net", -1) == net &&
            it->value("station", -1) == stn) {
            peers.erase(it);
            return true;
        }
    }
    return false;
}

std::string AunMapDocument::add_or_replace_subnet(std::uint8_t net,
                                                  const std::string& subnet_text,
                                                  const std::string& label) {
    if (std::string err = validate_subnet(subnet_text); !err.empty()) {
        return err;
    }
    if (!doc_.contains("subnets") || !doc_["subnets"].is_array()) {
        doc_["subnets"] = ordered_json::array();
    }
    for (auto& entry : doc_["subnets"]) {
        if (entry.is_object() && entry.value("net", -1) == net) {
            entry["subnet"] = subnet_text;
            if (label.empty()) {
                entry.erase("label");
            } else {
                entry["label"] = label;
            }
            return {};
        }
    }
    ordered_json added = ordered_json::object();
    added["net"] = net;
    added["subnet"] = subnet_text;
    if (!label.empty()) {
        added["label"] = label;
    }
    doc_["subnets"].push_back(std::move(added));
    return {};
}

bool AunMapDocument::remove_subnet(std::uint8_t net) {
    if (!doc_.contains("subnets") || !doc_["subnets"].is_array()) {
        return false;
    }
    auto& subnets = doc_["subnets"];
    for (auto it = subnets.begin(); it != subnets.end(); ++it) {
        if (it->is_object() && it->value("net", -1) == net) {
            subnets.erase(it);
            return true;
        }
    }
    return false;
}

std::string AunMapDocument::to_json() const {
    static const std::vector<std::string> peer_keys = {"net", "station", "host",
                                                        "port", "label"};
    static const std::vector<std::string> subnet_keys = {"net", "subnet",
                                                          "label"};
    // Rebuild the top level in its existing key order, canonicalising each
    // peers[]/subnets[] entry's key order and leaving every other key as it was.
    ordered_json out = ordered_json::object();
    for (const auto& [key, value] : doc_.items()) {
        if (key == "peers" && value.is_array()) {
            ordered_json peers = ordered_json::array();
            for (const auto& entry : value) {
                peers.push_back(entry.is_object()
                                    ? canonical_entry(entry, peer_keys)
                                    : entry);
            }
            out["peers"] = std::move(peers);
        } else if (key == "subnets" && value.is_array()) {
            ordered_json subnets = ordered_json::array();
            for (const auto& entry : value) {
                subnets.push_back(entry.is_object()
                                      ? canonical_entry(entry, subnet_keys)
                                      : entry);
            }
            out["subnets"] = std::move(subnets);
        } else {
            out[key] = value;
        }
    }
    return out.dump(2) + "\n";
}

std::string AunMapDocument::save(const std::filesystem::path& filepath) const {
    std::error_code ec;
    std::filesystem::path directory = filepath.parent_path();
    if (directory.empty()) {
        directory = ".";
    }
    std::filesystem::create_directories(directory, ec);  // best-effort

    std::random_device rd;
    std::filesystem::path temp =
        directory / ("." + filepath.filename().string() + ".tmp." +
                     std::to_string(rd()));
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return "could not create a temporary file in " + directory.string();
        }
        out << to_json();
        out.flush();
        if (!out) {
            std::filesystem::remove(temp, ec);
            return "could not write " + temp.string();
        }
    }
    std::filesystem::rename(temp, filepath, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return "could not replace " + filepath.string() + ": " + ec.message();
    }
    return {};
}

}  // namespace beebium
