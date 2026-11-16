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

#pragma once

#include "TapeImage.hpp"
#include "beebium/disc/BlankDiscs.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace beebium {

// The blank tape templates the server ships (tapes/blank/, made by
// scripts/make_blank_tapes.py): a UEF of silence for each length, named by its
// label. create_blank_tape() copies one into place as create_blank_disc() does
// a disc, and the copy is a tape like any other, recordable because it is a UEF.
struct BlankTapeTemplate {
    std::string label;          // the file name without its extension
    std::string filename;       // the template's file name
    std::string kind;           // "tape"
    std::uintmax_t size_bytes;
    double duration_seconds;
    std::filesystem::path filepath;
};

inline std::vector<BlankTapeTemplate> list_blank_tapes(const std::optional<std::filesystem::path>& dirpath) {
    std::vector<BlankTapeTemplate> tapes;
    if (!dirpath) return tapes;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(*dirpath, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const auto& filepath = entry.path();
        const auto filename = filepath.filename().string();
        if (filename.empty() || filename.front() == '.' || lower_case_extension(filepath) != ".uef") continue;
        const auto size_bytes = entry.file_size(ec);
        if (ec) continue;
        double duration_seconds = 0.0;
        try {
            const auto tape = open_tape_file(filepath);
            duration_seconds = static_cast<double>(tape->duration().value_or(0)) / ULA_CLOCK_HZ;
        } catch (const std::exception&) {
            continue;
        }
        tapes.push_back({filepath.stem().string(), filename, "tape", size_bytes, duration_seconds, filepath});
    }
    std::sort(tapes.begin(), tapes.end(),
              [](const BlankTapeTemplate& a, const BlankTapeTemplate& b) { return a.filename < b.filename; });
    return tapes;
}

inline CreateBlankDiscResult create_blank_tape(const std::optional<std::filesystem::path>& dirpath,
                                               const std::string& label,
                                               const std::filesystem::path& destination_filepath,
                                               bool overwrite) {
    for (const auto& tape : list_blank_tapes(dirpath)) {
        if (tape.label == label) {
            return copy_blank_template(tape.filepath, tape.size_bytes, destination_filepath, overwrite, "tape");
        }
    }
    CreateBlankDiscResult result;
    result.kind = CreateBlankDiscErrorKind::UnknownLabel;
    result.error = "no blank tape named '" + label + "'";
    return result;
}

}  // namespace beebium
