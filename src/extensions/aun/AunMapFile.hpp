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

#ifndef BEEBIUM_EXTENSIONS_AUN_AUN_MAP_FILE_HPP
#define BEEBIUM_EXTENSIONS_AUN_AUN_MAP_FILE_HPP

// Reader for the per-user aun-map.json (docs/discussion/aun-peer-map-file.md
// section 2.2): the standing AUN world -- the machines and subnets that do not
// announce themselves. Parsing is pure (no sockets, no DNS): a host is kept as
// the string it was written as and resolved later, off the emulation thread.
// Validation errors and a malformed file are reported with a position; the
// caller keeps the transport up regardless.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace beebium {

// One `peers[]` entry: an Econet (net, station) mapped to a UDP endpoint named
// by a host (an IPv4 literal or a DNS name, resolved later) and a port.
struct AunMapPeer {
    std::uint8_t net = 0;
    std::uint8_t stn = 0;
    std::string host;        // IPv4 literal or DNS name, as written (unresolved)
    std::uint16_t port = 0;
    std::string label;       // optional free-text note
};

// One `subnets[]` entry: the RISC OS convention that net N is a /24 whose
// station is the last octet, on port 32768 (both the inbound-identification and
// outbound-guess halves; there is no switch to take one without the other).
struct AunMapSubnet {
    std::uint8_t net = 0;
    std::uint32_t base_ip = 0;   // network byte order, /24 network address (last octet 0)
    std::string subnet_text;     // "a.b.c.0/24" exactly as written, for display
    std::string label;           // optional free-text note
};

// A parsed map. Empty is valid (an empty or absent file).
struct AunMap {
    std::vector<AunMapPeer> peers;
    std::vector<AunMapSubnet> subnets;
};

// The outcome of a parse or load. On success `map` holds the parsed contents
// and `error` is empty. On a malformed or invalid file `map` is nullopt and
// `error` names the fault with a position (a byte offset for a JSON syntax
// error, the array index for a semantic one). `file_present` is false only for
// load() when the file does not exist -- not an error, but reported so the
// caller can clear the file-derived layers and say so.
struct AunMapLoadResult {
    bool file_present = true;
    std::optional<AunMap> map;
    std::string error;
};

// Parse map JSON from text. `source_name` is used only in error messages (the
// file path, or a label in a test). Pure: no file or network access.
AunMapLoadResult parse_aun_map(std::string_view json_text,
                               const std::string& source_name);

// Read and parse the file at `filepath`. A missing file yields
// file_present=false with an empty map and no error; a present but unreadable
// or malformed file yields an error.
AunMapLoadResult load_aun_map(const std::filesystem::path& filepath);

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_AUN_AUN_MAP_FILE_HPP
