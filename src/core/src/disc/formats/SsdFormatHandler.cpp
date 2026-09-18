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

#include "beebium/disc/formats/SsdFormatHandler.hpp"
#include "beebium/disc/IbmDiscFormat.hpp"
#include "beebium/disc/TrackBuilder.hpp"
#include "beebium/disc/TrackDecoder.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

using namespace ibm_disc_format;

// DFS constants
static constexpr uint8_t k_sectors_per_track = 10;
static constexpr uint16_t k_sector_size = 256;
static constexpr uint8_t k_sector_size_code = 1;  // 256 bytes
static constexpr size_t k_bytes_per_track_per_side = k_sectors_per_track * k_sector_size;  // 2560

// Standard SSD/DSD file sizes
static constexpr size_t k_ssd_40_size = 40 * k_bytes_per_track_per_side;    // 102400
static constexpr size_t k_ssd_80_size = 80 * k_bytes_per_track_per_side;    // 204800
static constexpr size_t k_dsd_40_size = 2 * 40 * k_bytes_per_track_per_side;  // 204800
static constexpr size_t k_dsd_80_size = 2 * 80 * k_bytes_per_track_per_side;  // 409600

// Build one FM-encoded DFS track from sector data.
static void build_fm_track(DiscTrack& track, uint8_t track_num, uint8_t side,
                            const uint8_t* sector_data[k_sectors_per_track],
                            const uint8_t* zero_sector) {
    TrackBuilder builder(track);

    // GAP 1: post-index gap
    builder.append_fm_bytes(0xFF, k_std_gap1_FFs);
    builder.append_fm_bytes(0x00, k_std_sync_00s);

    for (uint8_t sector = 0; sector < k_sectors_per_track; ++sector) {
        // ID field
        builder.reset_crc(false);
        builder.append_fm_data_and_clocks(k_id_mark_data_pattern, k_mark_clock_pattern);
        builder.append_fm_byte(track_num);
        builder.append_fm_byte(side);
        builder.append_fm_byte(sector);
        builder.append_fm_byte(k_sector_size_code);
        builder.append_crc_fm();

        // GAP 2: ID to data gap
        builder.append_fm_bytes(0xFF, k_std_gap2_FFs);
        builder.append_fm_bytes(0x00, k_std_sync_00s);

        // Data field
        builder.reset_crc(false);
        builder.append_fm_data_and_clocks(k_data_mark_data_pattern, k_mark_clock_pattern);
        const uint8_t* data = sector_data[sector] ? sector_data[sector] : zero_sector;
        builder.append_fm_chunk(std::span<const uint8_t>(data, k_sector_size));
        builder.append_crc_fm();

        // GAP 3: inter-sector gap (except after last sector)
        if (sector < k_sectors_per_track - 1) {
            builder.append_fm_bytes(0xFF, k_std_10_sector_gap3_FFs);
            builder.append_fm_bytes(0x00, k_std_sync_00s);
        }
    }

    // GAP 4: fill to end of track
    builder.fill_fm_byte(0xFF);
    builder.finalize();
}

// Write-back callback: extract sector data from pulse track and write to SSD/DSD file.
static void ssd_write_track_callback(Disc& disc, bool upper_side, uint32_t track_number) {
    const auto& filepath = disc.source_filepath();
    if (filepath.empty()) return;

    const DiscTrack& track = disc.track(upper_side, track_number);
    TrackDecoder decoder(track);
    auto sectors = decoder.find_sectors();

    // Open file for in-place update
    std::fstream file(filepath, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) return;

    bool is_dsd = disc.is_double_sided();
    uint8_t side = upper_side ? 1 : 0;

    for (const auto& sec : sectors) {
        if (sec.sector >= k_sectors_per_track) continue;

        // Persist a sector only if it decoded cleanly: a trustworthy ID (so the
        // file offset is right), a present data field with a good CRC, and a
        // complete sector's worth of bytes. A bad-CRC or short/incomplete sector
        // must leave the host image's existing bytes untouched -- never overwrite
        // them with corrupt data or zero-padding the guest did not write (#88).
        if (sec.has_header_crc_error) continue;
        if (!sec.has_data_field) continue;
        if (sec.has_data_crc_error) continue;

        std::array<uint8_t, k_sector_size> buffer{};
        if (decoder.read_sector_data(sec, buffer) != k_sector_size) continue;

        // Calculate file offset
        size_t offset;
        if (is_dsd) {
            // DSD interleaved: track N side 0, track N side 1, track N+1 side 0, ...
            offset = (static_cast<size_t>(track_number) * 2 + side) * k_bytes_per_track_per_side
                     + static_cast<size_t>(sec.sector) * k_sector_size;
        } else {
            // SSD linear: track 0 sectors, track 1 sectors, ...
            offset = static_cast<size_t>(track_number) * k_bytes_per_track_per_side
                     + static_cast<size_t>(sec.sector) * k_sector_size;
        }

        file.seekp(static_cast<std::streamoff>(offset));
        file.write(reinterpret_cast<const char*>(buffer.data()), k_sector_size);
    }
}

// Determine whether an extension matches SSD or DSD.
enum class SsdType { None, SSD, DSD };

static SsdType classify_extension(std::string_view ext) {
    if (ext == ".ssd") return SsdType::SSD;
    if (ext == ".dsd") return SsdType::DSD;
    return SsdType::None;
}

std::string_view SsdFormatHandler::format_name() const {
    return "SSD/DSD (DFS)";
}

std::string_view SsdFormatHandler::format_description() const {
    return "Acorn Disc Filing System sector image";
}

std::vector<std::string_view> SsdFormatHandler::file_extensions() const {
    return {".ssd", ".dsd"};
}

FormatDetectionResult SsdFormatHandler::detect(std::span<const uint8_t> file_data,
                                                std::string_view extension) const {
    auto type = classify_extension(extension);
    if (type == SsdType::None) return {};

    size_t size = file_data.size();

    // Must be a multiple of sector size and non-empty
    if (size == 0 || (size % k_sector_size) != 0) return {};

    if (type == SsdType::SSD) {
        if (size > k_ssd_80_size) return {};

        // Exact standard sizes get high confidence
        if (size == k_ssd_40_size || size == k_ssd_80_size) {
            return {true, 95, size == k_ssd_40_size ? "SSD 40-track" : "SSD 80-track"};
        }

        // Truncated image: valid multiple of 256, within bounds
        return {true, 75, "SSD (truncated)"};
    }

    // DSD
    if (size > k_dsd_80_size) return {};

    // DSD must have even number of track-sides worth of data
    // (or be truncated but still sector-aligned)
    if (size == k_dsd_40_size) {
        return {true, 95, "DSD 40-track"};
    }
    if (size == k_dsd_80_size) {
        return {true, 95, "DSD 80-track"};
    }

    // Truncated DSD
    return {true, 70, "DSD (truncated)"};
}

DiscLoadResult SsdFormatHandler::load(std::span<const uint8_t> file_data,
                                       const std::filesystem::path& source_filepath) const {
    size_t size = file_data.size();
    if (size == 0 || (size % k_sector_size) != 0) {
        return {nullptr, "Invalid SSD/DSD: size must be a non-zero multiple of 256"};
    }

    // Determine extension
    std::string ext = source_filepath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto type = classify_extension(ext);
    if (type == SsdType::None) {
        return {nullptr, "Not an SSD/DSD file: " + source_filepath.string()};
    }

    bool is_dsd = (type == SsdType::DSD);
    uint8_t sides = is_dsd ? 2 : 1;

    // Calculate number of tracks from file size
    size_t bytes_per_track_all_sides = k_bytes_per_track_per_side * sides;
    uint8_t num_tracks = static_cast<uint8_t>(
        std::min(static_cast<size_t>(80), (size + bytes_per_track_all_sides - 1) / bytes_per_track_all_sides));

    // Create disc
    auto disc = std::make_unique<Disc>();
    disc->set_name(source_filepath.filename().string());
    disc->set_double_sided(is_dsd);
    disc->set_source_filepath(source_filepath);
    disc->set_format_name(is_dsd ? "DSD" : "SSD");

    // Check write protection (file permissions)
    std::error_code ec;
    auto perms = std::filesystem::status(source_filepath, ec).permissions();
    if (!ec && (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none) {
        disc->set_write_protected(true);
    }

    // Zero sector for filling truncated images
    std::array<uint8_t, k_sector_size> zero_sector{};

    // Build pulse tracks from sector data
    for (uint8_t t = 0; t < num_tracks; ++t) {
        for (uint8_t s = 0; s < sides; ++s) {
            const uint8_t* sector_ptrs[k_sectors_per_track];

            for (uint8_t sec = 0; sec < k_sectors_per_track; ++sec) {
                size_t offset;
                if (is_dsd) {
                    offset = (static_cast<size_t>(t) * 2 + s) * k_bytes_per_track_per_side
                             + static_cast<size_t>(sec) * k_sector_size;
                } else {
                    offset = static_cast<size_t>(t) * k_bytes_per_track_per_side
                             + static_cast<size_t>(sec) * k_sector_size;
                }

                if (offset + k_sector_size <= size) {
                    sector_ptrs[sec] = file_data.data() + offset;
                } else {
                    sector_ptrs[sec] = nullptr;  // Will use zero_sector
                }
            }

            bool upper_side = (s == 1);
            build_fm_track(disc->track(upper_side, t), t, s,
                          sector_ptrs, zero_sector.data());
        }
    }

    // Fill remaining tracks (beyond file data) with blank formatted tracks
    for (uint8_t t = num_tracks; t < 80; ++t) {
        for (uint8_t s = 0; s < sides; ++s) {
            const uint8_t* sector_ptrs[k_sectors_per_track];
            for (uint8_t sec = 0; sec < k_sectors_per_track; ++sec) {
                sector_ptrs[sec] = nullptr;
            }
            bool upper_side = (s == 1);
            build_fm_track(disc->track(upper_side, t), t, s,
                          sector_ptrs, zero_sector.data());
        }
    }

    // Set write-back callback
    if (!disc->is_write_protected()) {
        disc->set_write_track_callback(ssd_write_track_callback);
    }

    return {std::move(disc), ""};
}

bool SsdFormatHandler::supports_write() const {
    return true;
}

} // namespace beebium
