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
// container its file references are written in from it.
//
// The form is told by what is there, not by its name: a zip archive (a
// stored preset) and a directory each hold the document as preset.json, with
// the files it references and its thumbnail beside it; any other file is the
// document itself.

#pragma once

#include <beebium/server/ResourcePaths.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace beebium::server {

class PresetSource {
public:
    // The name of the document in a directory or archive preset.
    static constexpr const char* DOCUMENT_FILENAME = "preset.json";
    // The name of the thumbnail in a directory or archive preset.
    static constexpr const char* THUMBNAIL_FILENAME = "thumbnail.png";

    // How a preset is held: a lone document, or a directory or zip archive
    // holding the document and what it references.
    enum class Form { File, Directory, Archive };

    // The preset at `path`, read; or nullopt with `error` saying why it
    // cannot be read.
    static std::optional<PresetSource> open(const std::filesystem::path& path,
                                            std::string& error) {
        const auto absolute_path = std::filesystem::absolute(path);
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            const auto document_filepath = path / DOCUMENT_FILENAME;
            if (!std::filesystem::is_regular_file(document_filepath, ec)) {
                error = "No " + std::string(DOCUMENT_FILENAME) + " in preset directory: " +
                        path.string();
                return std::nullopt;
            }
            auto text = read_text(document_filepath, error);
            if (!text) return std::nullopt;
            return PresetSource(path, Form::Directory, ReferenceContainer(absolute_path),
                                document_filepath, std::move(*text));
        }
        if (is_zip_archive(path)) {
            std::shared_ptr<const zip::ZipReader> archive;
            try {
                archive = std::make_shared<const zip::ZipReader>(path);
            } catch (const zip::ZipError& e) {
                error = std::string("Cannot read preset archive: ") + e.what();
                return std::nullopt;
            }
            if (!archive->contains(DOCUMENT_FILENAME)) {
                error = "No " + std::string(DOCUMENT_FILENAME) + " in preset archive: " +
                        path.string();
                return std::nullopt;
            }
            const auto bytes = archive->read(DOCUMENT_FILENAME);
            PresetSource source(path, Form::Archive,
                                ReferenceContainer::archive(absolute_path, archive),
                                path / DOCUMENT_FILENAME, std::string(bytes.begin(), bytes.end()));
            source.archive_ = std::move(archive);
            return source;
        }
        auto text = read_text(path, error);
        if (!text) return std::nullopt;
        return PresetSource(path, Form::File, ReferenceContainer(absolute_path.parent_path()),
                            path, std::move(*text));
    }

    // The path the preset was given by.
    const std::filesystem::path& path() const { return path_; }

    // How the preset is held.
    Form form() const { return form_; }

    // True when `path` names this preset itself: the file or directory it
    // was read from. Writing there would overwrite what is being read.
    bool is_at(const std::filesystem::path& path) const {
        std::error_code ec;
        return std::filesystem::equivalent(path, path_, ec);
    }

    // The container the preset's file references are written in: the
    // preset's own directory or archive, or the directory holding a lone
    // preset file.
    const ReferenceContainer& container() const { return container_; }

    // The document as stored.
    const std::string& document_text() const { return document_text_; }

    // The names of the files a directory or archive preset holds, the
    // document among them, as archive members are named; none for a lone
    // file. A directory's hidden files (a component starting with '.', such
    // as .DS_Store) are left out.
    std::vector<std::string> file_names() const {
        if (archive_) return archive_->member_names();
        std::vector<std::string> names;
        if (form_ != Form::Directory) return names;
        const auto& dirpath = container_.dirpath();
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(dirpath, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            const auto relative = it->path().lexically_relative(dirpath);
            const bool hidden = std::any_of(relative.begin(), relative.end(), [](const auto& c) {
                return c.string().rfind('.', 0) == 0;
            });
            if (hidden) {
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(ec)) names.push_back(relative.generic_string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

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
    PresetSource(std::filesystem::path path, Form form, ReferenceContainer container,
                 std::filesystem::path document_filepath, std::string document_text)
        : path_(std::move(path)),
          form_(form),
          container_(std::move(container)),
          document_filepath_(std::move(document_filepath)),
          document_text_(std::move(document_text)) {}

    // True when the file at `path` starts as a zip archive does.
    static bool is_zip_archive(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        char signature[4] = {};
        if (!file.read(signature, sizeof(signature))) return false;
        return signature[0] == 'P' && signature[1] == 'K' &&
               ((signature[2] == '\x03' && signature[3] == '\x04') ||   // a member
                (signature[2] == '\x05' && signature[3] == '\x06'));    // empty
    }

    static std::optional<std::string> read_text(const std::filesystem::path& filepath,
                                                std::string& error) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            error = "Cannot open preset file: " + filepath.string();
            return std::nullopt;
        }
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    }

    std::filesystem::path path_;
    Form form_;
    ReferenceContainer container_;
    std::filesystem::path document_filepath_;
    std::string document_text_;
    std::shared_ptr<const zip::ZipReader> archive_;
};

}  // namespace beebium::server
