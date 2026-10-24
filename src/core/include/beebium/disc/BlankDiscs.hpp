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

// Blank disc templates: a directory of empty disc images, one per format,
// each named so that its file name without the extension is the label a user
// chooses it by. Beebium does not make disc formats itself; a new blank disc
// is a byte-for-byte copy of a template. The server finds the directory
// (blank-discs/ beside its presets) and the CLI's list-blank-discs and
// create-disc and DiscService's ListBlankDiscs and CreateDisc all come here.

#pragma once

#include "DiscLoader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace beebium {

// One blank disc template.
struct BlankDiscTemplate {
    std::string label;          // the file name without its extension
    std::string filename;       // the template's file name
    std::string kind;           // "floppy"
    std::uintmax_t size_bytes;
    std::filesystem::path filepath;
};

// Why create_blank_disc failed.
enum class CreateBlankDiscErrorKind {
    None,
    UnknownLabel,       // no template has that label
    DestinationExists,  // the destination exists and overwrite was not asked for
    WriteFailed,        // the template could not be read or the copy written
};

struct CreateBlankDiscResult {
    std::filesystem::path created_filepath;  // absolute, when it succeeded
    std::string error;                       // a lower-case fragment, when it failed
    CreateBlankDiscErrorKind kind = CreateBlankDiscErrorKind::None;

    explicit operator bool() const { return kind == CreateBlankDiscErrorKind::None; }
};

// The kind of disc a file named `filepath` is, by its extension: "floppy"
// when a floppy format handler loads that extension, or nullopt when no
// format does, so that a file that is not a disc image is not offered.
inline std::optional<std::string> blank_disc_kind(const std::filesystem::path& filepath) {
    std::string extension = filepath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& handler : default_format_registry().handlers()) {
        for (auto handled : handler->file_extensions()) {
            if (extension == handled) return std::string("floppy");
        }
    }
    return std::nullopt;
}

// The templates in `dirpath`, in file name order. Hidden files and files no
// disc format loads are left out. No directory means no templates.
inline std::vector<BlankDiscTemplate> list_blank_discs(
    const std::optional<std::filesystem::path>& dirpath) {
    std::vector<BlankDiscTemplate> discs;
    if (!dirpath) return discs;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(*dirpath, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const auto& filepath = entry.path();
        const auto filename = filepath.filename().string();
        if (filename.empty() || filename.front() == '.') continue;
        auto kind = blank_disc_kind(filepath);
        if (!kind) continue;
        const auto size_bytes = entry.file_size(ec);
        if (ec) continue;
        discs.push_back({filepath.stem().string(), filename, std::move(*kind), size_bytes,
                         filepath});
    }
    std::sort(discs.begin(), discs.end(),
              [](const BlankDiscTemplate& a, const BlankDiscTemplate& b) {
                  return a.filename < b.filename;
              });
    return discs;
}

// The template labelled `label` in `dirpath`, if there is one.
inline std::optional<BlankDiscTemplate> find_blank_disc(
    const std::optional<std::filesystem::path>& dirpath, const std::string& label) {
    for (auto& disc : list_blank_discs(dirpath)) {
        if (disc.label == label) return std::move(disc);
    }
    return std::nullopt;
}

// Copy the template labelled `label` in `dirpath` to `destination_filepath`,
// byte for byte, refusing an existing destination unless `overwrite`. The copy
// is an ordinary writable file whatever the template's permissions, so the
// new disc is not write-protected.
inline CreateBlankDiscResult create_blank_disc(const std::optional<std::filesystem::path>& dirpath,
                                          const std::string& label,
                                          const std::filesystem::path& destination_filepath,
                                          bool overwrite) {
    auto failed = [](CreateBlankDiscErrorKind kind, std::string error) {
        CreateBlankDiscResult result;
        result.kind = kind;
        result.error = std::move(error);
        return result;
    };

    auto disc = find_blank_disc(dirpath, label);
    if (!disc) {
        return failed(CreateBlankDiscErrorKind::UnknownLabel, "no blank disc named '" + label + "'");
    }
    if (destination_filepath.empty()) {
        return failed(CreateBlankDiscErrorKind::WriteFailed, "no destination given");
    }

    std::error_code ec;
    const auto status = std::filesystem::status(destination_filepath, ec);
    if (std::filesystem::is_directory(status)) {
        return failed(CreateBlankDiscErrorKind::DestinationExists,
                      "destination is a directory: " + destination_filepath.string());
    }
    if (std::filesystem::exists(status) && !overwrite) {
        return failed(CreateBlankDiscErrorKind::DestinationExists,
                      "destination already exists: " + destination_filepath.string());
    }

    std::ifstream in(disc->filepath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in.is_open() || bytes.size() != disc->size_bytes) {
        return failed(CreateBlankDiscErrorKind::WriteFailed,
                      "cannot read blank disc: " + disc->filepath.string());
    }

    std::ofstream out(destination_filepath, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        std::filesystem::remove(destination_filepath, ec);
        return failed(CreateBlankDiscErrorKind::WriteFailed,
                      "cannot write " + destination_filepath.string());
    }

    CreateBlankDiscResult result;
    result.created_filepath = std::filesystem::absolute(destination_filepath, ec);
    if (ec) result.created_filepath = destination_filepath;
    return result;
}

}  // namespace beebium
