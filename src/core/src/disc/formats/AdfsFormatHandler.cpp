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

#include "beebium/disc/formats/AdfsFormatHandler.hpp"
#include "beebium/disc/IbmDiscFormat.hpp"
#include "beebium/disc/TrackBuilder.hpp"
#include "beebium/disc/TrackDecoder.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

using namespace ibm_disc_format;

// ADFS constants
static constexpr uint8_t k_adfs_sectors_per_track = 16;
static constexpr uint16_t k_adfs_sector_size = 256;
static constexpr uint8_t k_adfs_sector_size_code = 1;  // 256 bytes
static constexpr size_t k_adfs_bytes_per_track = k_adfs_sectors_per_track * k_adfs_sector_size;  // 4096

// MFM gap sizes following the WD177x datasheet recommendations (from beebjit disc_adl.c)
static constexpr uint32_t k_adfs_gap1_4Es = 60;     // Post-index gap
static constexpr uint32_t k_adfs_sync_00s = 12;     // Pre-mark sync
static constexpr uint32_t k_adfs_gap2_4Es = 22;     // ID-to-data gap (4E portion)
static constexpr uint32_t k_adfs_gap3_4Es = 24;     // Inter-sector gap

// Standard ADFS file sizes
static constexpr size_t k_ads_size = 40 * 1 * k_adfs_bytes_per_track;   // 163840 (ADFS-S)
static constexpr size_t k_adm_size = 80 * 1 * k_adfs_bytes_per_track;   // 327680 (ADFS-M)
static constexpr size_t k_adl_size = 80 * 2 * k_adfs_bytes_per_track;   // 655360 (ADFS-L)

// ADFS geometry
struct AdfsGeometry {
    uint8_t tracks;
    uint8_t sides;
    std::string_view format_label;
};

// Build one MFM-encoded ADFS track from sector data.
static void build_mfm_track(DiscTrack& track, uint8_t track_num,
                              const uint8_t* sector_data[k_adfs_sectors_per_track],
                              const uint8_t* zero_sector) {
    TrackBuilder builder(track);

    // GAP 1: post-index gap (0x4E fill)
    builder.append_mfm_bytes(0x4E, k_adfs_gap1_4Es);

    for (uint8_t sector = 0; sector < k_adfs_sectors_per_track; ++sector) {
        // Sync
        builder.append_mfm_bytes(0x00, k_adfs_sync_00s);

        // ID field: 3xA1 sync + ID mark
        builder.append_mfm_3x_a1_sync();
        builder.append_mfm_byte(k_id_mark_data_pattern);
        builder.append_mfm_byte(track_num);
        builder.append_mfm_byte(0);  // Side byte is 0 per Acorn convention (from beebjit)
        builder.append_mfm_byte(sector);
        builder.append_mfm_byte(k_adfs_sector_size_code);
        builder.append_crc_mfm();

        // GAP 2: ID-to-data gap
        builder.append_mfm_bytes(0x4E, k_adfs_gap2_4Es);
        builder.append_mfm_bytes(0x00, k_adfs_sync_00s);

        // Data field: 3xA1 sync + data mark
        builder.append_mfm_3x_a1_sync();
        builder.append_mfm_byte(k_data_mark_data_pattern);
        const uint8_t* data = sector_data[sector] ? sector_data[sector] : zero_sector;
        builder.append_mfm_chunk(std::span<const uint8_t>(data, k_adfs_sector_size));
        builder.append_crc_mfm();

        // GAP 3: inter-sector gap
        builder.append_mfm_bytes(0x4E, k_adfs_gap3_4Es);
    }

    // GAP 4: fill to end of track with 0x4E
    builder.fill_mfm_byte(0x4E);
    builder.finalize();
}

// Write-back callback for ADFS format.
static void adfs_write_track_callback(Disc& disc, bool upper_side, uint32_t track_number) {
    const auto& filepath = disc.source_filepath();
    if (filepath.empty()) return;

    const DiscTrack& track = disc.track(upper_side, track_number);
    TrackDecoder decoder(track);
    auto sectors = decoder.find_sectors();

    std::fstream file(filepath, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) return;

    uint8_t side = upper_side ? 1 : 0;

    for (const auto& sec : sectors) {
        if (sec.sector >= k_adfs_sectors_per_track) continue;

        // Persist a sector only if it decoded cleanly: a trustworthy ID, a
        // present data field with a good CRC, and a complete sector's worth of
        // bytes. A bad-CRC or short/incomplete sector must leave the host image's
        // existing bytes untouched, never overwritten with corrupt data or
        // zero-padding the guest did not write (#88).
        if (sec.has_header_crc_error) continue;
        if (!sec.has_data_field) continue;
        if (sec.has_data_crc_error) continue;

        std::array<uint8_t, k_adfs_sector_size> buffer{};
        if (decoder.read_sector_data(sec, buffer) != k_adfs_sector_size) continue;

        // ADL interleaved layout: track 0 side 0, track 0 side 1, track 1 side 0, ...
        size_t offset = (static_cast<size_t>(track_number) * (disc.is_double_sided() ? 2 : 1) + side)
                        * k_adfs_bytes_per_track
                        + static_cast<size_t>(sec.sector) * k_adfs_sector_size;

        file.seekp(static_cast<std::streamoff>(offset));
        file.write(reinterpret_cast<const char*>(buffer.data()), k_adfs_sector_size);
    }
}

// Classify extension to ADFS geometry.
static std::optional<AdfsGeometry> classify_adfs_extension(std::string_view ext, size_t file_size) {
    if (ext == ".adl") {
        return AdfsGeometry{80, 2, "ADL"};
    }
    if (ext == ".adm") {
        return AdfsGeometry{80, 1, "ADM"};
    }
    if (ext == ".ads") {
        return AdfsGeometry{40, 1, "ADS"};
    }
    if (ext == ".adf") {
        // Auto-detect geometry from file size
        if (file_size <= k_ads_size) {
            return AdfsGeometry{40, 1, "ADF (ADFS-S)"};
        }
        if (file_size <= k_adm_size) {
            return AdfsGeometry{80, 1, "ADF (ADFS-M)"};
        }
        if (file_size <= k_adl_size) {
            return AdfsGeometry{80, 2, "ADF (ADFS-L)"};
        }
        return std::nullopt;  // Too large
    }
    return std::nullopt;
}

std::string_view AdfsFormatHandler::format_name() const {
    return "ADFS (ADF/ADL/ADM/ADS)";
}

std::string_view AdfsFormatHandler::format_description() const {
    return "Advanced Disc Filing System sector image";
}

std::vector<std::string_view> AdfsFormatHandler::file_extensions() const {
    return {".adf", ".adl", ".adm", ".ads"};
}

FormatDetectionResult AdfsFormatHandler::detect(std::span<const uint8_t> file_data,
                                                  std::string_view extension) const {
    auto geom = classify_adfs_extension(extension, file_data.size());
    if (!geom) return {};

    size_t size = file_data.size();
    if (size == 0) return {};

    // Must be sector-aligned
    if (size % k_adfs_sector_size != 0) return {};

    size_t expected = static_cast<size_t>(geom->tracks) * geom->sides * k_adfs_bytes_per_track;

    // Exact match = high confidence
    if (size == expected) {
        return {true, 95, std::string(geom->format_label)};
    }

    // Within bounds but not exact = lower confidence (truncated)
    if (size < expected) {
        return {true, 70, std::string(geom->format_label) + " (truncated)"};
    }

    return {};
}

DiscLoadResult AdfsFormatHandler::load(std::span<const uint8_t> file_data,
                                         const std::filesystem::path& source_filepath) const {
    std::string ext = source_filepath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto geom = classify_adfs_extension(ext, file_data.size());
    if (!geom) {
        return {nullptr, "Not an ADFS disc image: " + source_filepath.string()};
    }

    size_t size = file_data.size();
    if (size == 0 || size % k_adfs_sector_size != 0) {
        return {nullptr, "Invalid ADFS image: size must be a non-zero multiple of 256"};
    }

    auto disc = std::make_unique<Disc>();
    disc->set_name(source_filepath.filename().string());
    disc->set_double_sided(geom->sides == 2);
    disc->set_source_filepath(source_filepath);
    disc->set_format_name(std::string(geom->format_label));

    // Check write protection
    std::error_code ec;
    auto perms = std::filesystem::status(source_filepath, ec).permissions();
    if (!ec && (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none) {
        disc->set_write_protected(true);
    }

    std::array<uint8_t, k_adfs_sector_size> zero_sector{};

    for (uint8_t t = 0; t < geom->tracks; ++t) {
        for (uint8_t s = 0; s < geom->sides; ++s) {
            const uint8_t* sector_ptrs[k_adfs_sectors_per_track];

            for (uint8_t sec = 0; sec < k_adfs_sectors_per_track; ++sec) {
                size_t offset = (static_cast<size_t>(t) * geom->sides + s)
                                * k_adfs_bytes_per_track
                                + static_cast<size_t>(sec) * k_adfs_sector_size;

                if (offset + k_adfs_sector_size <= size) {
                    sector_ptrs[sec] = file_data.data() + offset;
                } else {
                    sector_ptrs[sec] = nullptr;
                }
            }

            bool upper_side = (s == 1);
            build_mfm_track(disc->track(upper_side, t), t,
                           sector_ptrs, zero_sector.data());
        }
    }

    // Set write-back callback
    if (!disc->is_write_protected()) {
        disc->set_write_track_callback(adfs_write_track_callback);
    }

    return {std::move(disc), ""};
}

bool AdfsFormatHandler::supports_write() const {
    return true;
}

} // namespace beebium
