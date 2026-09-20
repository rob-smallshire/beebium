// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Lightweight CLI argument value parsers. Extracted from ServerMain.hpp so
// they can be unit-tested without pulling in the full server template stack.
//
// Style: every parser tolerates ASCII case in enum-style values, and every
// thrown error message names the offending value, the flag, and the set of
// allowed values so the user can copy-paste the right thing.

#ifndef BEEBIUM_SERVER_CLI_ARG_PARSERS_HPP
#define BEEBIUM_SERVER_CLI_ARG_PARSERS_HPP

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The shared colon key=value tokenizer (split_colon_args); --sideways and the
// extension args build on the same one.
#include "beebium/CliArgSplit.hpp"

namespace beebium::server {

enum class WaitMode {
    None,   // Start immediately
    Cli,    // Wait for RETURN on console
    Api     // Wait for Run() RPC
};

enum class OutputFormat {
    Auto,    // Detect based on platform::is_stdout_tty()
    Pretty,  // Human-friendly formatted output
    Tsv,     // Tab-separated values with header
    Jsonl    // JSON Lines (one object per line)
};

enum class SidewaysSlotType { Empty, Rom, Ram };

struct SidewaysConfig {
    std::uint8_t slot;
    SidewaysSlotType type;
    std::string image_filepath;  // Optional: filepath for ROM or pre-loaded RAM
    bool write_protected = false;  // RAM only: engage the write-protect switch
};

inline std::string ascii_to_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

// Parse a base-prefixed integer ("123", "0xff", "0b1010", "0o17").
// Throws std::runtime_error on invalid input.
inline int parse_int(const std::string& str, const std::string& context = "") {
    if (str.empty()) {
        throw std::runtime_error(
            "Empty integer value" + (context.empty() ? "" : " for " + context));
    }

    std::string_view sv = str;
    int base = 10;
    std::size_t start = 0;

    if (sv.length() >= 2 && sv[0] == '0') {
        char prefix = static_cast<char>(
            std::tolower(static_cast<unsigned char>(sv[1])));
        if (prefix == 'b') { base = 2;  start = 2; }
        else if (prefix == 'o') { base = 8;  start = 2; }
        else if (prefix == 'x') { base = 16; start = 2; }
    }

    if (start >= sv.length()) {
        throw std::runtime_error(
            "Invalid integer: " + str + (context.empty() ? "" : " for " + context));
    }

    std::string digits(sv.substr(start));
    char* end = nullptr;
    errno = 0;
    long long value = std::strtoll(digits.c_str(), &end, base);

    if (end == digits.c_str() || *end != '\0') {
        throw std::runtime_error(
            "Invalid integer: " + str + (context.empty() ? "" : " for " + context));
    }
    // Parenthesised to defeat the windows.h min/max macros (a consumer may have
    // pulled them in before this header).
    if (errno == ERANGE
        || value < (std::numeric_limits<int>::min)()
        || value > (std::numeric_limits<int>::max)()) {
        throw std::runtime_error(
            "Integer overflow: " + str + (context.empty() ? "" : " for " + context));
    }
    return static_cast<int>(value);
}

// Parse "drive:filepath" (or "drive:url") for --floppy.
inline std::pair<std::uint8_t, std::string> parse_floppy_arg(const std::string& arg) {
    auto colon_pos = arg.find(':');
    if (colon_pos == std::string::npos || colon_pos == 0) {
        throw std::runtime_error(
            "Invalid --floppy format: " + arg + " (expected drive:filepath)");
    }

    std::string drive_str = arg.substr(0, colon_pos);
    std::string filepath_or_url = arg.substr(colon_pos + 1);

    int drive = parse_int(drive_str, "--floppy drive");
    if (drive < 0 || drive > 1) {
        throw std::runtime_error(
            "Invalid --floppy drive number: " + drive_str + " (must be 0 or 1)");
    }
    if (filepath_or_url.empty()) {
        throw std::runtime_error(
            "Invalid --floppy format: " + arg + " (filepath required)");
    }
    return {static_cast<std::uint8_t>(drive), filepath_or_url};
}

// Parse --sideways SLOT:TYPE[:IMAGE]. Type comparison is case-insensitive.
//   15:rom:bbc-basic_2.rom    - ROM with image
//   4:ram                     - Empty RAM
//   4:ram:preload.bin         - RAM with pre-loaded image
//   2:empty                   - Empty slot
// The colon-separated key=value tokenizer is shared with the extension-args
// parser (beebium::split_colon_args in ExtensionArgParser.hpp): the same
// convention -- and now the same code -- backs `--<ext> key=value:...` and
// `--sideways slot=...:type=...`. It honours double quotes so a value may
// contain a ':' (a URL or a Windows path); the caller strips the quotes.

inline std::string strip_quotes(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

// Parse a --sideways slot specification. Colon-separated key=value pairs,
// matching the extension-args convention (see split_colon_args in
// ExtensionArgParser.cpp / `--<ext> key=value:...`):
//
//   slot=<0-15>:type=<rom|ram|empty>[:image=<path>][:write-protect]
//
// - slot and type are required.
// - image is required for rom, optional for ram (a pre-load), forbidden for empty.
// - write-protect is a bare flag (RAM only) engaging the write-protect switch;
//   whether the socket actually has one is validated against the topology later.
//
// The former positional form (SLOT:TYPE[:IMAGE]) is no longer accepted; a token
// without '=' (other than the write-protect flag) is reported with guidance.
inline SidewaysConfig parse_sideways_arg(const std::string& arg) {
    SidewaysConfig config{};

    bool have_slot = false;
    bool have_type = false;

    for (const auto& token : split_colon_args(arg)) {
        if (token.empty()) {
            throw std::runtime_error(
                "Invalid --sideways: empty field in '" + arg
                + "' (expected slot=<0-15>:type=<rom|ram|empty>[:image=<path>]"
                  "[:write-protect])");
        }

        auto eq = token.find('=');
        if (eq == std::string::npos) {
            // The only valid bare flag is write-protect.
            if (ascii_to_lower(token) == "write-protect") {
                config.write_protected = true;
                continue;
            }
            throw std::runtime_error(
                "Invalid --sideways field '" + token + "' in '" + arg
                + "'. Use key=value pairs: slot=<0-15>:type=<rom|ram|empty>"
                  "[:image=<path>][:write-protect] "
                  "(the positional SLOT:TYPE[:IMAGE] form is no longer supported)");
        }

        std::string key = ascii_to_lower(token.substr(0, eq));
        std::string value = token.substr(eq + 1);

        if (key == "slot") {
            int slot = parse_int(value, "--sideways slot");
            if (slot < 0 || slot > 15) {
                throw std::runtime_error(
                    "Invalid --sideways slot number: " + value + " (must be 0-15)");
            }
            config.slot = static_cast<std::uint8_t>(slot);
            have_slot = true;
        } else if (key == "type") {
            std::string type_lc = ascii_to_lower(value);
            if (type_lc == "rom") {
                config.type = SidewaysSlotType::Rom;
            } else if (type_lc == "ram") {
                config.type = SidewaysSlotType::Ram;
            } else if (type_lc == "empty") {
                config.type = SidewaysSlotType::Empty;
            } else {
                throw std::runtime_error(
                    "Invalid --sideways type: '" + value
                    + "' (expected one of: rom, ram, empty -- case-insensitive)");
            }
            have_type = true;
        } else if (key == "image") {
            config.image_filepath = strip_quotes(value);
        } else if (key == "write-protect") {
            std::string v = ascii_to_lower(value);
            if (v == "true" || v == "1") {
                config.write_protected = true;
            } else if (v == "false" || v == "0") {
                config.write_protected = false;
            } else {
                throw std::runtime_error(
                    "Invalid --sideways write-protect value: '" + value
                    + "' (expected true or false, or the bare flag)");
            }
        } else {
            throw std::runtime_error(
                "Invalid --sideways key '" + key + "' in '" + arg
                + "' (expected slot, type, image, write-protect)");
        }
    }

    if (!have_slot || !have_type) {
        throw std::runtime_error(
            "Invalid --sideways '" + arg
            + "': slot and type are required "
              "(slot=<0-15>:type=<rom|ram|empty>[:image=<path>][:write-protect])");
    }

    if (config.type == SidewaysSlotType::Empty && !config.image_filepath.empty()) {
        throw std::runtime_error(
            "Invalid --sideways: 'empty' type cannot have an image");
    }
    if (config.type == SidewaysSlotType::Rom && config.image_filepath.empty()) {
        throw std::runtime_error(
            "Invalid --sideways: 'rom' type requires an image");
    }
    if (config.write_protected && config.type != SidewaysSlotType::Ram) {
        throw std::runtime_error(
            "Invalid --sideways: write-protect applies only to a RAM slot");
    }
    return config;
}

// Parse --wait[=mode]. Case-insensitive.
inline WaitMode parse_wait_arg(const std::string& value) {
    std::string lc = ascii_to_lower(value);
    if (lc == "cli") return WaitMode::Cli;
    if (lc == "api") return WaitMode::Api;
    throw std::runtime_error(
        "Invalid --wait value: '" + value
        + "' (expected one of: cli, api -- case-insensitive)");
}

// Parse --speed. Accepts a positive, finite real multiplier (1.0 = real-time,
// 2.0 = double) or the word "unlimited" (run flat out). Returns the pacing
// speed multiplier; 0.0 is the internal unlimited sentinel. Zero and negative
// values are rejected here -- on a CLI "speed 0" reads as "stopped", the
// opposite of unlimited, so the word form is the only way to request flat out.
inline double parse_speed_arg(const std::string& value) {
    if (ascii_to_lower(value) == "unlimited") return 0.0;

    double multiplier = 0.0;
    try {
        std::size_t consumed = 0;
        multiplier = std::stod(value, &consumed);
        if (consumed != value.size()) {
            throw std::invalid_argument(value);
        }
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Invalid --speed value: '" + value
            + "' (expected a positive multiplier such as 1.0 or 2.0, or 'unlimited')");
    }
    if (!std::isfinite(multiplier) || multiplier <= 0.0) {
        throw std::runtime_error(
            "Invalid --speed value: '" + value
            + "' (must be a positive, finite multiplier; use 'unlimited' to run flat out)");
    }
    return multiplier;
}

// Parse --format. Case-insensitive.
inline OutputFormat parse_format_arg(const std::string& value) {
    std::string lc = ascii_to_lower(value);
    if (lc == "pretty") return OutputFormat::Pretty;
    if (lc == "tsv") return OutputFormat::Tsv;
    if (lc == "jsonl") return OutputFormat::Jsonl;
    throw std::runtime_error(
        "Invalid --format value: '" + value
        + "' (expected one of: pretty, tsv, jsonl -- case-insensitive)");
}

}  // namespace beebium::server

#endif  // BEEBIUM_SERVER_CLI_ARG_PARSERS_HPP
