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

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace beebium {

// Supported URL schemes for disc images
enum class DiscUrlScheme {
    File,       // file:// - local filesystem
    Unknown     // Unsupported or unrecognized scheme
};

// Represents a parsed disc image URL.
//
// Currently supports file: URLs for local filesystem access.
// Future: may support http:, https:, or custom schemes.
//
// Usage:
//   auto url = DiscUrl::parse("file:///path/to/disc.ssd");
//   if (url) {
//       auto path = url->to_filepath();
//   }
class DiscUrl {
public:
    // Parse a URL string.
    // Returns std::nullopt if URL is malformed or uses an unsupported scheme.
    //
    // Supported formats:
    //   file:///absolute/path    (Unix)
    //   file:///C:/path          (Windows)
    //   file://localhost/path    (explicit localhost)
    static std::optional<DiscUrl> parse(std::string_view url);

    // Create a DiscUrl from a filesystem path.
    // Convenience for converting bare paths to file: URLs.
    static DiscUrl from_filepath(const std::filesystem::path& filepath);

    // Get the URL scheme
    DiscUrlScheme scheme() const { return scheme_; }

    // Get the original URL string
    const std::string& url() const { return url_; }

    // For file: URLs, return the local filesystem path.
    // Throws std::logic_error if scheme is not File.
    std::filesystem::path to_filepath() const;

    // Check if this is a local file URL
    bool is_local() const { return scheme_ == DiscUrlScheme::File; }

private:
    DiscUrl(DiscUrlScheme scheme, std::string url, std::filesystem::path filepath)
        : scheme_(scheme), url_(std::move(url)), filepath_(std::move(filepath)) {}

    DiscUrlScheme scheme_;
    std::string url_;
    std::filesystem::path filepath_;  // Only valid for file: URLs
};

// Implementation

namespace detail {

inline int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decode %XX escapes in a URL path component back to raw bytes. A '%' that is
// not followed by two hex digits is emitted verbatim and decoding continues, so
// malformed input never throws. Multi-byte UTF-8 (each byte separately encoded,
// e.g. %C3%A9) is decoded byte-by-byte and thus reconstructed exactly.
inline std::string percent_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hex_value(s[i + 1]);
            int lo = hex_value(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

// Percent-encode a filesystem path for use in a file: URL. Unreserved
// characters (RFC 3986) are kept, as are '/' (path separator) and ':' (the
// Windows drive-letter colon, which parse() relies on seeing literally). Every
// other byte -- space, '#', '?', '%', and all non-ASCII UTF-8 bytes -- becomes
// %XX, so the result is a valid URL that parse() decodes back to the original.
inline std::string percent_encode(std::string_view s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                          c == '.' || c == '~' || c == '/' || c == ':';
        if (keep) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

} // namespace detail

inline std::optional<DiscUrl> DiscUrl::parse(std::string_view url) {
    // Check for file: scheme
    constexpr std::string_view file_prefix = "file://";

    if (url.substr(0, file_prefix.size()) == file_prefix) {
        std::string_view remainder = url.substr(file_prefix.size());

        if (remainder.empty()) {
            return std::nullopt;  // Malformed: nothing after file://
        }

        // Handle file:///path (empty authority = localhost)
        if (remainder[0] == '/') {
            // On Unix: file:///path -> /path
            // On Windows: file:///C:/path -> C:/path (skip leading /)
#ifdef _WIN32
            // Skip leading / for Windows paths like file:///C:/...
            if (remainder.size() >= 3 && remainder[2] == ':') {
                remainder = remainder.substr(1);
            }
#endif
            // Percent-decode is the LAST step, after the structural '/' and the
            // Windows drive-letter checks above have run on the still-encoded
            // string (those delimiters are never encoded). NOTE: on Windows the
            // narrow path constructor reads these bytes as the active ANSI
            // codepage, not UTF-8, so non-ASCII filenames there are a
            // pre-existing limitation this change neither fixes nor worsens.
            std::filesystem::path path(detail::percent_decode(remainder));
            return DiscUrl(DiscUrlScheme::File, std::string(url), path);
        }

        // Handle file://localhost/path or file://host/path
        auto slash_pos = remainder.find('/');
        if (slash_pos != std::string_view::npos) {
            std::string_view authority = remainder.substr(0, slash_pos);
            std::string_view path_part = remainder.substr(slash_pos);

            // Only accept localhost or empty authority
            if (authority == "localhost" || authority.empty()) {
                // Decoded last, as above (see the file:///path branch note).
                std::filesystem::path path(detail::percent_decode(path_part));
                return DiscUrl(DiscUrlScheme::File, std::string(url), path);
            }
            // Non-localhost authority not supported
            return std::nullopt;
        }

        // No path component
        return std::nullopt;
    }

    // Unknown scheme
    return std::nullopt;
}

inline DiscUrl DiscUrl::from_filepath(const std::filesystem::path& filepath) {
    std::filesystem::path abs_path = std::filesystem::absolute(filepath);
#ifdef _WIN32
    // On Windows, construct proper file:///C:/path/to/file URL
    // Use generic_string() to get forward slashes
    std::string url = "file:///" + detail::percent_encode(abs_path.generic_string());
#else
    std::string url = "file://" + detail::percent_encode(abs_path.string());
#endif
    return DiscUrl(DiscUrlScheme::File, std::move(url), abs_path);
}

inline std::filesystem::path DiscUrl::to_filepath() const {
    if (scheme_ != DiscUrlScheme::File) {
        throw std::logic_error("Cannot convert non-file URL to filepath: " + url_);
    }
    return filepath_;
}

} // namespace beebium
