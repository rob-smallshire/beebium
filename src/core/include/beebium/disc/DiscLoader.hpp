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

#include "DiscFormatHandler.hpp"
#include "DiscFormatRegistry.hpp"
#include "DiscUrl.hpp"
#include "formats/SsdFormatHandler.hpp"
#include "formats/AdfsFormatHandler.hpp"
#include "formats/HfeFormatHandler.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace beebium {

// Get a shared format registry with all built-in format handlers registered.
inline DiscFormatRegistry& default_format_registry() {
    static DiscFormatRegistry registry = [] {
        DiscFormatRegistry r;
        r.register_handler(std::make_unique<SsdFormatHandler>());
        r.register_handler(std::make_unique<AdfsFormatHandler>());
        r.register_handler(std::make_unique<HfeFormatHandler>());
        return r;
    }();
    return registry;
}

// Load a disc image from a URL.
//
// Uses the default format registry to auto-detect the format and load
// the disc image into a Disc object with pulse-level track data.
//
// Currently supports file: URLs for local filesystem access.
inline DiscLoadResult load_disc_from_url(const std::string& url) {
    auto parsed = DiscUrl::parse(url);
    if (!parsed) {
        return {nullptr, "Invalid or unsupported URL: " + url,
                DiscLoadErrorKind::CannotOpen};
    }

    switch (parsed->scheme()) {
        case DiscUrlScheme::File: {
            auto filepath = parsed->to_filepath();
            return default_format_registry().load_from_filepath(filepath);
        }

        case DiscUrlScheme::Unknown:
        default:
            return {nullptr, "Unsupported URL scheme: " + url,
                    DiscLoadErrorKind::CannotOpen};
    }
}

// Load a disc image from a URL or bare filepath.
// If the input doesn't look like a URL (no :// found), treats it as a local filepath.
inline DiscLoadResult load_disc_from_url_or_filepath(const std::string& url_or_filepath) {
    if (url_or_filepath.find("://") != std::string::npos) {
        return load_disc_from_url(url_or_filepath);
    }

    // Bare filepath: load directly via registry
    std::filesystem::path filepath(url_or_filepath);
    return default_format_registry().load_from_filepath(filepath);
}

} // namespace beebium
