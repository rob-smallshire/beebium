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

// The files of a machine directory that the core defines, and nothing else:
// the one place their names and formats are known. A saved machine is a
// directory, <name>.beebium-machine, holding
//
//   machine.json            its identity record (MachineRecord): the one
//                           file a machine directory is known by
//   preset.json             its configuration
//   thumbnail.png           its last screen
//   nvram/, extensions/     what its battery-backed units keep
//   lock                    held by the server running it
//   <name>.beebium-machine  the document a user double-clicks: a small JSON
//                           file naming the format and the machine's uuid
//   desktop.ini, .directory folder metadata (Windows, KDE): the display name
//                           and icon, written and never read
//
// and whatever else the machine's preset carried, at its own paths, which
// the core does not prescribe.

#pragma once

#include <beebium/server/AtomicFile.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace beebium::server {

namespace machine_layout {

inline constexpr std::string_view RECORD_FILENAME = "machine.json";
inline constexpr std::string_view PRESET_FILENAME = "preset.json";
inline constexpr std::string_view THUMBNAIL_FILENAME = "thumbnail.png";
inline constexpr std::string_view NVRAM_DIRNAME = "nvram";
inline constexpr std::string_view EXTENSIONS_DIRNAME = "extensions";
inline constexpr std::string_view LOCK_FILENAME = "lock";
inline constexpr std::string_view DESKTOP_INI_FILENAME = "desktop.ini";
inline constexpr std::string_view KDE_DIRECTORY_FILENAME = ".directory";
// The suffix of a saved machine's directory and of the document inside it.
inline constexpr std::string_view DOCUMENT_SUFFIX = ".beebium-machine";
// What the document file and machine.json say they are.
inline constexpr std::string_view FORMAT = "beebium-machine";
inline constexpr int FORMAT_VERSION = 1;

// True for a file or directory of a machine directory that the core
// defines, which is never one of the machine's own media.
inline bool is_core_entry(const std::filesystem::path& name) {
    const auto n = name.filename().string();
    for (auto core : {RECORD_FILENAME, PRESET_FILENAME, THUMBNAIL_FILENAME, NVRAM_DIRNAME,
                      EXTENSIONS_DIRNAME, LOCK_FILENAME, DESKTOP_INI_FILENAME,
                      KDE_DIRECTORY_FILENAME}) {
        if (n == core) return true;
    }
    return n.size() > DOCUMENT_SUFFIX.size() &&
           n.compare(n.size() - DOCUMENT_SUFFIX.size(), DOCUMENT_SUFFIX.size(),
                     DOCUMENT_SUFFIX) == 0;
}

}  // namespace machine_layout

// A machine's identity record, machine.json.
struct MachineRecord {
    std::string uuid;           // the MachineIdentity uuid, the same on every open
    std::string description;    // the machine's description: a template (#153)
    std::string model;          // the model id, e.g. "model-b-integra-b"
    std::string created;        // when the machine was made (ISO 8601, UTC)
    std::string last_used;      // when it was last opened (ISO 8601, UTC)
    std::string source_preset;  // the preset it was made from, for information; may be ""
};

// The current time as ISO 8601 UTC, to the second ("2026-10-17T14:30:00Z").
inline std::string iso8601_utc_now() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::array<char, 32> text{};
    std::strftime(text.data(), text.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text.data();
}

// True when `dirpath` is a machine directory: it holds machine.json. The
// suffix and the document file play no part.
inline bool is_machine_directory(const std::filesystem::path& dirpath) {
    std::error_code ec;
    return std::filesystem::is_regular_file(dirpath / machine_layout::RECORD_FILENAME, ec);
}

// The machine directory a path names: the directory itself, or the one
// holding the document file named. nullopt when it names neither.
inline std::optional<std::filesystem::path> machine_dirpath_named(
    const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        if (is_machine_directory(path)) return path;
        return std::nullopt;
    }
    const auto parent = std::filesystem::absolute(path, ec).parent_path();
    if (std::filesystem::is_regular_file(path, ec) && is_machine_directory(parent)) return parent;
    return std::nullopt;
}

// The record at `dirpath`, or nullopt with `error` saying why there is none.
inline std::optional<MachineRecord> read_machine_record(const std::filesystem::path& dirpath,
                                                        std::string& error) {
    const auto filepath = dirpath / machine_layout::RECORD_FILENAME;
    std::ifstream in(filepath, std::ios::binary);
    if (!in) {
        error = "no " + std::string(machine_layout::RECORD_FILENAME) + " in " + dirpath.string();
        return std::nullopt;
    }
    const auto json = nlohmann::json::parse(in, nullptr, false);
    if (!json.is_object() || json.value("format", "") != machine_layout::FORMAT ||
        !json.contains("uuid") || !json["uuid"].is_string()) {
        error = filepath.string() + " is not a machine record";
        return std::nullopt;
    }
    if (json.value("version", 0) > machine_layout::FORMAT_VERSION) {
        error = filepath.string() + " is from a later version of Beebium";
        return std::nullopt;
    }
    return MachineRecord{json["uuid"].get<std::string>(), json.value("description", ""),
                         json.value("model", ""),          json.value("created", ""),
                         json.value("last_used", ""),      json.value("source_preset", "")};
}

namespace machine_document_detail {

// `text` (UTF-8) as UTF-16LE with a byte-order mark: the encoding in which
// Explorer reads a desktop.ini holding more than ASCII.
inline std::string utf16le_with_bom(std::string_view text) {
    std::string out{"\xFF\xFE"};
    auto put = [&](std::uint32_t unit) {
        out.push_back(static_cast<char>(unit & 0xFF));
        out.push_back(static_cast<char>(unit >> 8));
    };
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const int length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        std::uint32_t code = length == 1 ? lead
                           : length == 2 ? (lead & 0x1F)
                           : length == 3 ? (lead & 0x0F)
                                         : (lead & 0x07);
        for (int k = 1; k < length && i + k < text.size(); ++k) {
            code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        }
        i += length;
        if (code >= 0x10000) {
            code -= 0x10000;
            put(0xD800 + (code >> 10));
            put(0xDC00 + (code & 0x3FF));
        } else {
            put(code);
        }
    }
    return out;
}

// Makes a file writable and visible again before it is replaced (Windows
// will not replace a hidden or read-only file); nothing elsewhere.
inline void clear_file_attributes([[maybe_unused]] const std::filesystem::path& filepath) {
#ifdef _WIN32
    ::SetFileAttributesW(filepath.c_str(), FILE_ATTRIBUTE_NORMAL);
#endif
}

}  // namespace machine_document_detail

// Clears the read-only attribute Windows needs on a machine directory before
// Explorer honours its desktop.ini (write_machine_document_files sets it),
// which would otherwise stop the directory being removed or renamed. Nothing
// elsewhere.
inline void clear_directory_readonly([[maybe_unused]] const std::filesystem::path& dirpath) {
#ifdef _WIN32
    const DWORD attributes = ::GetFileAttributesW(dirpath.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) {
        ::SetFileAttributesW(dirpath.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    }
#endif
}

// `record` as machine.json holds it.
inline std::string machine_record_text(const MachineRecord& record) {
    nlohmann::ordered_json json{{"format", machine_layout::FORMAT},
                                {"version", machine_layout::FORMAT_VERSION},
                                {"uuid", record.uuid},
                                {"description", record.description},
                                {"model", record.model},
                                {"created", record.created},
                                {"last_used", record.last_used}};
    if (!record.source_preset.empty()) json["source_preset"] = record.source_preset;
    return json.dump(2) + "\n";
}

// Writes `record` to machine.json at `dirpath`, atomically.
inline void write_machine_record(const std::filesystem::path& dirpath,
                                 const MachineRecord& record) {
    write_text_atomically(dirpath / machine_layout::RECORD_FILENAME, machine_record_text(record));
}

// `text` (UTF-8) as a path, and a path as UTF-8: how paths cross the gRPC
// boundary, whose strings are UTF-8. On Windows a path made from, or
// converted to, a plain std::string uses the ANSI code page instead.
inline std::filesystem::path path_from_utf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
inline std::string utf8_of(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

// Why `name` cannot name a machine's document, or nullopt when it can. A
// document's name becomes its directory's and its document file's, and a
// machine moves between hosts unchanged, so the name must be a valid file
// name on every one of them: not empty, not "." or "..", no path separator
// and none of the characters or names Windows refuses.
inline std::optional<std::string> why_not_document_name(std::string_view name) {
    if (name.empty()) return "a name cannot be empty";
    if (name == "." || name == "..") return "\"" + std::string(name) + "\" is not a file name";
    for (const char c : name) {
        if (c == '/' || c == '\\') return "a name cannot contain a path separator ('/' or '\\')";
        if (static_cast<unsigned char>(c) < 0x20) return "a name cannot contain a control character";
        if (std::string_view("<>:\"|?*").find(c) != std::string_view::npos) {
            return std::string("a name cannot contain '") + c +
                   "', which Windows does not allow in a file name";
        }
    }
    if (name.back() == ' ' || name.back() == '.') {
        return "a name cannot end with a space or a '.', which Windows drops";
    }
    std::string upper;
    for (const char c : name.substr(0, name.find('.'))) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    for (const auto* reserved : {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
                                 "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                 "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"}) {
        if (upper == reserved) {
            return "\"" + std::string(name) + "\" is a device name on Windows, not a file name";
        }
    }
    return std::nullopt;
}

// The name of the document a machine directory is: the directory's name
// without the document suffix ("Elite Server" for "Elite
// Server.beebium-machine"), what {document} renders for a saved machine.
inline std::string machine_document_name(const std::filesystem::path& dirpath) {
    auto filename = dirpath.filename();
    if (filename.empty()) filename = dirpath.parent_path().filename();
    return utf8_of(filename.extension() == machine_layout::DOCUMENT_SUFFIX ? filename.stem()
                                                                           : filename);
}

// The document file of the machine directory `dirpath`: named after the
// directory, with the document suffix whether or not the directory has it.
inline std::filesystem::path machine_document_filepath(const std::filesystem::path& dirpath) {
    auto filename = dirpath.filename().string();
    if (std::filesystem::path(filename).extension() != machine_layout::DOCUMENT_SUFFIX) {
        filename += machine_layout::DOCUMENT_SUFFIX;
    }
    return dirpath / filename;
}

// Writes the files that present the machine at `dirpath` as a document,
// atomically, from its record and the name to show: the document file
// (machine_document_filepath; any other document file, left by an earlier
// name of the directory, is removed), desktop.ini and .directory. On Windows the
// directory is made read-only and desktop.ini hidden and system, as Explorer
// requires before it honours desktop.ini. None of these is ever read back.
inline void write_machine_document_files(const std::filesystem::path& dirpath,
                                         const MachineRecord& record,
                                         const std::string& display_name) {
    const auto document_filepath = machine_document_filepath(dirpath);
    const auto document_filename = document_filepath.filename();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dirpath, ec)) {
        const auto name = entry.path().filename();
        if (name != document_filename && name.extension() == machine_layout::DOCUMENT_SUFFIX &&
            entry.is_regular_file(ec)) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
    nlohmann::ordered_json document{{"format", machine_layout::FORMAT},
                                    {"version", machine_layout::FORMAT_VERSION},
                                    {"uuid", record.uuid}};
    write_text_atomically(document_filepath, document.dump(2) + "\n");

    const auto desktop_ini = dirpath / machine_layout::DESKTOP_INI_FILENAME;
    machine_document_detail::clear_file_attributes(desktop_ini);
    write_text_atomically(desktop_ini,
                          machine_document_detail::utf16le_with_bom(
                              "[.ShellClassInfo]\r\nLocalizedResourceName=" + display_name +
                              "\r\n"));
    write_text_atomically(dirpath / machine_layout::KDE_DIRECTORY_FILENAME,
                          "[Desktop Entry]\nIcon=beebium-machine\nName=" + display_name + "\n");
#ifdef _WIN32
    ::SetFileAttributesW(desktop_ini.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    const DWORD attributes = ::GetFileAttributesW(dirpath.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        ::SetFileAttributesW(dirpath.c_str(), attributes | FILE_ATTRIBUTE_READONLY);
    }
#endif
}

}  // namespace beebium::server
