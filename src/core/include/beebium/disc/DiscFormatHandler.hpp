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

#include "Disc.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// Result of format detection. Higher confidence = better match.
struct FormatDetectionResult {
    bool detected = false;
    int confidence = 0;         // 0-100. 0 = cannot handle, 100 = certain.
    std::string format_name;    // Human-readable format name (e.g. "SSD 80-track")
};

// Classification of a disc-load failure, so a caller -- and, through the disc
// service, a front end -- can react to the kind of failure without parsing the
// human-readable message. The message itself still travels in
// DiscLoadResult::error and is unchanged by this classification.
//
// This covers only the failures the loader itself produces. Service-level
// failures (no controller, bad drive number, drive already occupied) are
// classified by DiscService, not here.
enum class DiscLoadErrorKind {
    None = 0,       // Success -- no error.
    CannotOpen,     // The image file could not be opened (missing, permissions).
    Empty,          // The image file is zero bytes.
    ReadError,      // The file opened but could not be read to the end.
    Unrecognised,   // No registered handler recognised the format.
    LoadFailed,     // A handler recognised the format but failed to load it.
};

// Stable lower-case token for a load-error kind, for JSON/CLI output.
inline std::string_view disc_load_error_kind_name(DiscLoadErrorKind kind) {
    switch (kind) {
        case DiscLoadErrorKind::None:         return "none";
        case DiscLoadErrorKind::CannotOpen:   return "cannot_open";
        case DiscLoadErrorKind::Empty:        return "empty";
        case DiscLoadErrorKind::ReadError:    return "read_error";
        case DiscLoadErrorKind::Unrecognised: return "unrecognised";
        case DiscLoadErrorKind::LoadFailed:   return "load_failed";
    }
    return "none";
}

// Result of loading a disc image.
struct DiscLoadResult {
    std::unique_ptr<Disc> disc;
    std::string error;          // Non-empty on failure.
    DiscLoadErrorKind kind = DiscLoadErrorKind::None;  // Failure classification.
    bool success() const { return disc != nullptr; }
    explicit operator bool() const { return success(); }
};

// Abstract interface for disc image format handlers.
//
// Each format (SSD, DSD, ADFS, HFE, etc.) provides a handler that can
// detect whether a file is in that format and load it into a Disc object
// with pulse-level track data. Optionally supports write-back.
class DiscFormatHandler {
public:
    virtual ~DiscFormatHandler() = default;

    // Human-readable format name (e.g. "SSD/DSD (DFS)")
    virtual std::string_view format_name() const = 0;

    // Short description expanding acronyms (e.g. "Acorn Disc Filing System")
    virtual std::string_view format_description() const = 0;

    // File extensions this handler may recognise (e.g. {".ssd", ".dsd"})
    virtual std::vector<std::string_view> file_extensions() const = 0;

    // Detect whether this handler can parse the given file data.
    // Called with the full file contents and the file extension (lowercase, with dot).
    // Returns a confidence score: 0 = cannot handle, 100 = certain match.
    virtual FormatDetectionResult detect(std::span<const uint8_t> file_data,
                                          std::string_view extension) const = 0;

    // Load the file data into a Disc with pulse-level tracks.
    // The source_filepath is provided for write-back and display purposes.
    virtual DiscLoadResult load(std::span<const uint8_t> file_data,
                                 const std::filesystem::path& source_filepath) const = 0;

    // Whether this handler supports writing modified tracks back to the file.
    virtual bool supports_write() const = 0;
};

} // namespace beebium
