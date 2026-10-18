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

// Writing a preset: the one place a preset is written, by create-preset,
// import-preset and export-preset alike (PresetSource is the one place one
// is read). A preset is written as a zip archive -- the stored form -- or,
// for editing, unpacked into a directory laid out as the archive is; either
// way it is the document as preset.json and the files beside it.

#pragma once

#include <beebium/server/AtomicFile.hpp>
#include <beebium/disc/DiscUrl.hpp>
#include <beebium/server/PresetSource.hpp>
#include <beebium/zip/ZipArchive.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

// The files of a preset whose document is `document_text` and which holds
// nothing else (a lone preset file, or one just made).
inline std::vector<zip::ZipMember> preset_members(std::string_view document_text) {
    return {{PresetSource::DOCUMENT_FILENAME,
             std::vector<std::uint8_t>(document_text.begin(), document_text.end())}};
}

// The files of the preset `source`: its document as preset.json and every
// other file a directory or archive preset holds. Throws when one cannot be
// read.
inline std::vector<zip::ZipMember> preset_members(const PresetSource& source) {
    auto members = preset_members(source.document_text());
    for (const auto& name : source.file_names()) {
        if (name == PresetSource::DOCUMENT_FILENAME) continue;
        members.push_back({name, source.container().read_file(source.container().dirpath() /
                                                               std::filesystem::path(name))});
    }
    return members;
}

// Rewrites each reference in `document` to a file `container` holds -- an
// absolute path or a file URL within it, as the loader anchors one -- back to
// its path within the container, as a preset writes it. A preset made from
// one held in `container` takes that preset's files with it, and its
// document then names its own copies, wherever it is stored.
inline void make_references_relative(nlohmann::ordered_json& document,
                                     const ReferenceContainer& container) {
    if (document.is_object() || document.is_array()) {
        for (auto& value : document) make_references_relative(value, container);
        return;
    }
    if (!document.is_string()) return;
    const auto text = document.get<std::string>();
    std::filesystem::path filepath;
    if (text.find("://") != std::string::npos) {
        const auto url = beebium::DiscUrl::parse(text);
        if (!url || !url->is_local()) return;
        filepath = url->to_filepath();
    } else if (is_absolute_reference(text)) {
        filepath = text;
    } else {
        return;
    }
    const auto relative = container.relative_path_of(filepath);
    if (relative && container.has_file(filepath)) document = relative->generic_string();
}

// Write a preset made of `members` as a zip archive at `target_filepath`,
// atomically: a reader sees the old preset or the new one, never part of
// one. Returns an empty string, or why the preset could not be written.
inline std::string write_preset(std::vector<zip::ZipMember> members,
                                const std::filesystem::path& target_filepath) {
    try {
        write_file_atomically(target_filepath, [&](const std::filesystem::path& temp_filepath) {
            zip::write_zip(temp_filepath, std::move(members));
        });
        return {};
    } catch (const std::exception& e) {
        return "cannot write preset: " + target_filepath.string() + " (" + e.what() + ")";
    }
}

// Write a preset made of `members` unpacked into a new directory at
// `target_dirpath`, laid out as its archive is. The directory appears whole
// or not at all; one that already exists is refused. Returns an empty
// string, or why the preset could not be written.
inline std::string write_preset_directory(const std::vector<zip::ZipMember>& members,
                                          const std::filesystem::path& target_dirpath) {
    std::error_code ec;
    if (std::filesystem::exists(target_dirpath, ec)) {
        return "cannot write preset: " + target_dirpath.string() + " already exists";
    }
    std::random_device rd;
    const auto temp_dirpath =
        target_dirpath.parent_path() /
        (target_dirpath.filename().string() + ".partial-" + std::to_string(rd()));
    try {
        for (const auto& member : members) {
            const auto filepath = temp_dirpath / std::filesystem::path(member.name);
            std::filesystem::create_directories(filepath.parent_path());
            std::ofstream out(filepath, std::ios::binary);
            out.write(reinterpret_cast<const char*>(member.data.data()),
                      static_cast<std::streamsize>(member.data.size()));
            out.close();
            if (!out) throw std::runtime_error("cannot write " + filepath.string());
        }
        std::filesystem::rename(temp_dirpath, target_dirpath);
        return {};
    } catch (const std::exception& e) {
        std::filesystem::remove_all(temp_dirpath, ec);
        return "cannot write preset: " + target_dirpath.string() + " (" + e.what() + ")";
    }
}

}  // namespace beebium::server
