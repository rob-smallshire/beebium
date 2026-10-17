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

// Writing a file so that no reader ever sees it half-written: the one way
// the server replaces a file it owns (a machine's disc copies, a stored
// preset).

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

namespace beebium::server {

// Replace `dst` with what `write` puts in the file it is given, atomically:
// `write` fills a uniquely named temporary in dst's directory, which is made
// owner-writable and renamed over dst. A reader opening dst sees the complete
// old file or the complete new one, and a write that throws part-way removes
// its temporary and leaves dst untouched.
//
// rename() replaces atomically on POSIX. On Windows std::filesystem rename
// uses MoveFileEx with MOVEFILE_REPLACE_EXISTING, which replaces an existing
// file but is not guaranteed atomic, and fails -- leaving dst intact -- if
// another process has dst open without delete sharing.
inline void write_file_atomically(
    const std::filesystem::path& dst,
    const std::function<void(const std::filesystem::path& temp_filepath)>& write) {
    // The random part distinguishes the temporaries of concurrent writes to
    // the same destination, in this process or another.
    std::random_device rd;
    const std::filesystem::path temp_filepath =
        dst.parent_path() /
        (dst.filename().string() + ".partial-" +
         std::to_string((static_cast<std::uint64_t>(rd()) << 32) ^ rd()));
    try {
        write(temp_filepath);
        std::filesystem::permissions(
            temp_filepath,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::add);
        // Windows refuses to replace a read-only file; POSIX does not care.
        // Making dst writable first opens no window in which it is missing or
        // partial.
        std::error_code ec;
        if (std::filesystem::exists(dst, ec)) {
            std::filesystem::permissions(dst, std::filesystem::perms::owner_write,
                                         std::filesystem::perm_options::add, ec);
        }
        std::filesystem::rename(temp_filepath, dst);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp_filepath, ec);
        throw;
    }
}

// Replace `dst` with `text`, atomically (see write_file_atomically).
inline void write_text_atomically(const std::filesystem::path& dst, std::string_view text) {
    write_file_atomically(dst, [&](const std::filesystem::path& temp_filepath) {
        std::ofstream out(temp_filepath, std::ios::binary);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) throw std::runtime_error("cannot write " + dst.string());
    });
}

}  // namespace beebium::server
