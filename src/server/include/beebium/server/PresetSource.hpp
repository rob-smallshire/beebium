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

// A preset as it is given: the one place a preset's document is read. Every
// command that takes a preset opens it here, and reads its document and the
// directory its file references are written in from it.
//
// The form is told by what is there, not by its name: a directory holds the
// document as preset.json; any other file is the document itself.

#pragma once

#include <beebium/server/ResourcePaths.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace beebium::server {

class PresetSource {
public:
    // The name of the document in a directory preset.
    static constexpr const char* DOCUMENT_FILENAME = "preset.json";
    // The name of the thumbnail in a directory preset.
    static constexpr const char* THUMBNAIL_FILENAME = "thumbnail.png";

    // The preset at `path`, read; or nullopt with `error` saying why it
    // cannot be read.
    static std::optional<PresetSource> open(const std::filesystem::path& path,
                                            std::string& error) {
        std::error_code ec;
        const bool is_directory = std::filesystem::is_directory(path, ec);
        const auto document_filepath = is_directory ? path / DOCUMENT_FILENAME : path;
        if (is_directory && !std::filesystem::is_regular_file(document_filepath, ec)) {
            error = "No " + std::string(DOCUMENT_FILENAME) + " in preset directory: " +
                    path.string();
            return std::nullopt;
        }
        std::ifstream file(document_filepath, std::ios::binary);
        if (!file.is_open()) {
            error = "Cannot open preset file: " + document_filepath.string();
            return std::nullopt;
        }
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto absolute_path = std::filesystem::absolute(path);
        return PresetSource(path, is_directory ? absolute_path : absolute_path.parent_path(),
                            document_filepath, std::move(text));
    }

    // The path the preset was given by.
    const std::filesystem::path& path() const { return path_; }

    // True when the preset is a directory holding its document.
    bool is_directory() const { return document_filepath_ != path_; }

    // The container the preset's file references are written in: the
    // preset's own directory, or the one holding the preset file.
    ReferenceContainer container() const { return ReferenceContainer(container_dirpath_); }

    // The document as stored.
    const std::string& document_text() const { return document_text_; }

    // The document, parsed; or nullopt with `error` saying why it is not a
    // JSON object.
    std::optional<nlohmann::json> parse_document(std::string& error) const {
        try {
            auto json = nlohmann::json::parse(document_text_);
            if (json.is_object()) return json;
            error = "Preset file must contain a JSON object: " + document_filepath_.string();
        } catch (const nlohmann::json::parse_error& e) {
            error = "Invalid JSON in preset file: " + document_filepath_.string() +
                    ": parse error at byte " + std::to_string(e.byte);
        }
        return std::nullopt;
    }

private:
    PresetSource(std::filesystem::path path, std::filesystem::path container_dirpath,
                 std::filesystem::path document_filepath, std::string document_text)
        : path_(std::move(path)),
          container_dirpath_(std::move(container_dirpath)),
          document_filepath_(std::move(document_filepath)),
          document_text_(std::move(document_text)) {}

    std::filesystem::path path_;
    std::filesystem::path container_dirpath_;
    std::filesystem::path document_filepath_;
    std::string document_text_;
};

}  // namespace beebium::server
