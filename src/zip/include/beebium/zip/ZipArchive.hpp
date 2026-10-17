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

// Reading and writing zip archives: the one place Beebium touches the zip
// format (a stored preset is one). Nothing else includes the zip library.
//
// A member is named by a relative path with '/' between its components. An
// archive naming a member any other way -- absolute, with a drive, with a
// backslash, with an empty, "." or ".." component -- is refused when it is
// opened, so no member can reach outside the directory it is extracted into.
//
// An archive written from the same members is byte-identical: members are
// written in name order, every timestamp is zero, and nothing else of the
// writing machine (permissions, owner, time zone) is recorded.
//
// Files are written directly; a caller that must never leave a partial file
// writes to a temporary and renames it (the server's write_file_atomically).

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::zip {

// Why an archive cannot be read or written, naming the archive.
class ZipError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// True when `name` is a member name an archive may hold: relative, '/'
// between non-empty components, none of them "." or "..", no backslash, no
// drive. A directory entry's trailing '/' is allowed.
bool is_valid_member_name(std::string_view name);

// A zip archive opened for reading. Its directory is read when it is opened;
// a member is read or extracted on request.
class ZipReader {
public:
    // Opens the archive at `filepath`. Throws ZipError when it cannot be
    // read as a zip archive, names a member in a way it may not, or names
    // two members alike.
    explicit ZipReader(const std::filesystem::path& filepath);
    ~ZipReader();
    ZipReader(ZipReader&&) noexcept;
    ZipReader& operator=(ZipReader&&) noexcept;
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    // The archive's file members (directory entries left out), in the
    // order the archive lists them.
    const std::vector<std::string>& member_names() const;

    // True when the archive holds a file member called `name`.
    bool contains(std::string_view name) const;

    // The bytes of the member `name`. Throws ZipError when there is no such
    // member or it cannot be read.
    std::vector<std::uint8_t> read(std::string_view name) const;

    // Writes the member `name` to `dest_filepath`, streaming it. Throws
    // ZipError as read() does, or when it cannot be written.
    void extract(std::string_view name, const std::filesystem::path& dest_filepath) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A member to write: its name and its bytes.
struct ZipMember {
    std::string name;
    std::vector<std::uint8_t> data;
};

// Writes a zip archive of `members` to `filepath`, compressed.
// Members are written in name order whatever order they are given in. Throws
// ZipError when a name is not a valid member name, two members share one, or
// the archive cannot be written.
void write_zip(const std::filesystem::path& filepath, std::vector<ZipMember> members);

}  // namespace beebium::zip
