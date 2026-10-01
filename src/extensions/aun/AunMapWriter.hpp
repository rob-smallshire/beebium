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

#ifndef BEEBIUM_EXTENSIONS_AUN_AUN_MAP_WRITER_HPP
#define BEEBIUM_EXTENSIONS_AUN_AUN_MAP_WRITER_HPP

// The writer half of the one aun-map.json library (the parser is AunMapFile).
// It edits the map file the server owns: add or replace a peer or subnet,
// remove one, and save atomically. A hand-maintained file keeps its entry
// order and any unknown keys -- a GUI or subcommand edit does not reshuffle it
// -- and the output is pretty-printed with the same stable key order the
// tools/aun converter writes (net, station, host, port, label for peers; net,
// subnet, label for subnets; any other keys follow, unchanged).
//
// This header pulls in nlohmann/json, so it is included only by the .cpp files
// that edit the file (the dispatcher, the CLI subcommands, tests), never by the
// extension's own header.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace beebium {

// The outcome of loading the map file as an editable document (defined in full
// after AunMapDocument, since it holds one by value).
struct AunMapDocumentLoad;

class AunMapDocument {
public:
    // Load the file as an ordered document, preserving entry order and unknown
    // keys.
    static AunMapDocumentLoad load(const std::filesystem::path& filepath);

    // A fresh empty document: {"peers": [], "subnets": []}.
    static AunMapDocument empty();

    // Add or replace the peers[] entry for (net, stn). On replace the entry
    // keeps its position and any unknown keys; an empty label removes the label
    // key. Returns an empty string on success or a field-named validation error.
    std::string add_or_replace_peer(std::uint8_t net, std::uint8_t stn,
                                    const std::string& host, std::uint16_t port,
                                    const std::string& label);

    // Remove the peers[] entry for (net, stn). Returns true if one was removed.
    bool remove_peer(std::uint8_t net, std::uint8_t stn);

    // Add or replace the subnets[] entry for net (one /24 per net). Returns an
    // empty string on success or a field-named validation error.
    std::string add_or_replace_subnet(std::uint8_t net,
                                      const std::string& subnet_text,
                                      const std::string& label);

    // Remove the subnets[] entry for net. Returns true if one was removed.
    bool remove_subnet(std::uint8_t net);

    // Serialize to canonical JSON text: known keys in their documented order,
    // unknown keys and entry order preserved, two-space indent, trailing
    // newline.
    std::string to_json() const;

    // Write to_json() to `filepath` through a temporary file renamed over it,
    // so a reader never sees a half-written file and a failed write leaves the
    // original intact. Returns an empty string on success or an error.
    std::string save(const std::filesystem::path& filepath) const;

private:
    explicit AunMapDocument(nlohmann::ordered_json doc) : doc_(std::move(doc)) {}

    nlohmann::ordered_json doc_;  // top-level object with peers[]/subnets[]
};

struct AunMapDocumentLoad {
    bool file_present = true;
    std::optional<AunMapDocument> document;
    std::string error;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_AUN_AUN_MAP_WRITER_HPP
