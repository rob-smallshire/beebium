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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// Registry of disc format handlers with auto-detection and prioritised loading.
//
// When loading a disc image, the registry calls detect() on all registered
// handlers, sorts by confidence, and tries load() on the best match first.
// If the highest-confidence handler fails, it tries the next, and so on.
class DiscFormatRegistry {
public:
    DiscFormatRegistry() = default;

    // Register a format handler. Handlers are tried in detection-confidence order.
    void register_handler(std::unique_ptr<DiscFormatHandler> handler) {
        handlers_.push_back(std::move(handler));
    }

    // Load a disc image from a file path. Reads the file, detects format, and loads.
    DiscLoadResult load_from_filepath(const std::filesystem::path& filepath) const {
        // Read entire file into memory
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return {nullptr, "Cannot open disc image: " + filepath.string(),
                    DiscLoadErrorKind::CannotOpen};
        }

        auto file_size = file.tellg();
        if (file_size <= 0) {
            return {nullptr, "Empty disc image: " + filepath.string(),
                    DiscLoadErrorKind::Empty};
        }

        std::vector<uint8_t> data(static_cast<size_t>(file_size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(data.data()), file_size);
        if (!file.good()) {
            return {nullptr, "Error reading disc image: " + filepath.string(),
                    DiscLoadErrorKind::ReadError};
        }

        // Extract lowercase extension
        std::string ext = filepath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        return load_from_data(data, ext, filepath);
    }

    // Load a disc from in-memory data with a known extension and source path.
    DiscLoadResult load_from_data(std::span<const uint8_t> data,
                                   std::string_view extension,
                                   const std::filesystem::path& source_filepath) const {
        if (handlers_.empty()) {
            return {nullptr, "No disc format handlers registered",
                    DiscLoadErrorKind::LoadFailed};
        }

        // Detect format from all handlers
        struct DetectionEntry {
            const DiscFormatHandler* handler;
            FormatDetectionResult result;
        };
        std::vector<DetectionEntry> detections;

        for (const auto& handler : handlers_) {
            auto result = handler->detect(data, extension);
            if (result.detected && result.confidence > 0) {
                detections.push_back({handler.get(), std::move(result)});
            }
        }

        if (detections.empty()) {
            return {nullptr, "Unrecognised disc image format (size=" +
                    std::to_string(data.size()) + ", ext=" +
                    std::string(extension) + "): " + source_filepath.string(),
                    DiscLoadErrorKind::Unrecognised};
        }

        // Sort by confidence (descending)
        std::sort(detections.begin(), detections.end(),
                  [](const DetectionEntry& a, const DetectionEntry& b) {
                      return a.result.confidence > b.result.confidence;
                  });

        // Try loading with each handler in confidence order
        std::string last_error;
        for (const auto& entry : detections) {
            auto result = entry.handler->load(data, source_filepath);
            if (result.success()) {
                return result;
            }
            last_error = std::move(result.error);
        }

        return {nullptr, "Failed to load disc image: " + last_error,
                DiscLoadErrorKind::LoadFailed};
    }

    // Access registered handlers.
    const std::vector<std::unique_ptr<DiscFormatHandler>>& handlers() const {
        return handlers_;
    }

private:
    std::vector<std::unique_ptr<DiscFormatHandler>> handlers_;
};

} // namespace beebium
