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

// Temporary files and directories for tests, named so that no two test
// processes can choose the same one (#163).
//
// CTest runs every test case as its own process, several at once, and the
// same suite may be running in another build tree or for another user on the
// same machine. A fixed name, or one made unique only within a process (a
// counter, a fixture's address, an unseeded std::rand()), is shared by those
// processes, and one test then reads, overwrites or deletes another's file.
// Every name made here carries random bits from std::random_device as well as
// a per-process counter, and a ScopedTempDir is created exclusively, so a
// clash is detected rather than shared.

#ifndef BEEBIUM_TEST_TEMP_PATHS_HPP
#define BEEBIUM_TEST_TEMP_PATHS_HPP

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace beebium::test {

// A tag no other process will produce: 64 random bits and a per-process
// sequence number, in hex.
inline std::string unique_temp_tag() {
    static std::atomic<uint64_t> sequence{0};
    std::random_device rd;
    const uint64_t random_bits =
        (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%016llx-%llx",
                  static_cast<unsigned long long>(random_bits),
                  static_cast<unsigned long long>(sequence.fetch_add(1)));
    return buffer;
}

// A path in the system temp directory that no other test process will use:
// <prefix><tag><suffix>. Nothing is created; the caller removes what it makes.
// Give the suffix any extension the code under test looks for.
inline std::filesystem::path unique_temp_path(std::string_view prefix,
                                              std::string_view suffix = {}) {
    std::string name(prefix);
    name += unique_temp_tag();
    name += suffix;
    return std::filesystem::temp_directory_path() / name;
}

// A new, empty directory in the system temp directory, unique to this object
// and removed with everything in it when the object is destroyed. Files with
// fixed names may be created inside it freely.
class ScopedTempDir {
public:
    explicit ScopedTempDir(std::string_view prefix) {
        // create_directory fails if the directory exists, so a name that
        // somehow clashed is never shared; try another.
        for (int attempt = 0; attempt < 16; ++attempt) {
            auto candidate = unique_temp_path(prefix);
            std::error_code ec;
            if (std::filesystem::create_directory(candidate, ec)) {
                dirpath_ = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("could not create a unique temp directory");
    }

    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(dirpath_, ec);
    }

    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;

    const std::filesystem::path& path() const { return dirpath_; }

    std::filesystem::path operator/(const std::filesystem::path& relative) const {
        return dirpath_ / relative;
    }

private:
    std::filesystem::path dirpath_;
};

}  // namespace beebium::test

#endif  // BEEBIUM_TEST_TEMP_PATHS_HPP
