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

// Writing a preset: the one place a preset file is written, by
// create-preset, import-preset and export-preset alike (PresetSource is the
// one place one is read).

#pragma once

#include <beebium/server/AtomicFile.hpp>

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace beebium::server {

// Write a preset whose document is `document_text` to `target_filepath`,
// atomically: a reader sees the old preset or the new one, never part of
// one. Returns an empty string, or why the preset could not be written.
inline std::string write_preset(std::string_view document_text,
                                const std::filesystem::path& target_filepath) {
    try {
        write_text_atomically(target_filepath, document_text);
        return {};
    } catch (const std::exception& e) {
        return "cannot write preset file: " + target_filepath.string() + " (" + e.what() + ")";
    }
}

}  // namespace beebium::server
