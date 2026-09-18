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

#include <beebium/disc/formats/SsdFormatHandler.hpp>
#include <beebium/disc/DiscFormatRegistry.hpp>
#include <beebium/disc/TrackDecoder.hpp>
#include <beebium/disc/TrackBuilder.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace beebium;
using namespace beebium::ibm_disc_format;

// Helper: create an in-memory SSD image with known sector data.
// Each sector is filled with (track * 10 + sector) & 0xFF.
static std::vector<uint8_t> make_ssd_image(uint8_t num_tracks) {
    std::vector<uint8_t> data(num_tracks * 10 * 256);
    for (uint8_t t = 0; t < num_tracks; ++t) {
        for (uint8_t s = 0; s < 10; ++s) {
            size_t offset = (t * 10 + s) * 256;
            uint8_t fill = static_cast<uint8_t>((t * 10 + s) & 0xFF);
            std::fill_n(data.data() + offset, 256, fill);
        }
    }
    return data;
}

// Helper: create an in-memory DSD image with known sector data.
static std::vector<uint8_t> make_dsd_image(uint8_t num_tracks) {
    std::vector<uint8_t> data(num_tracks * 2 * 10 * 256);
    for (uint8_t t = 0; t < num_tracks; ++t) {
        for (uint8_t side = 0; side < 2; ++side) {
            for (uint8_t s = 0; s < 10; ++s) {
                size_t offset = (static_cast<size_t>(t) * 2 + side) * 10 * 256 + s * 256;
                uint8_t fill = static_cast<uint8_t>((t * 20 + side * 10 + s) & 0xFF);
                std::fill_n(data.data() + offset, 256, fill);
            }
        }
    }
    return data;
}

// =============================================================================
// Detection Tests
// =============================================================================

TEST_CASE("SsdFormatHandler detects standard SSD 80-track", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    auto data = make_ssd_image(80);
    auto result = handler.detect(data, ".ssd");

    CHECK(result.detected);
    CHECK(result.confidence >= 90);
    CHECK(result.format_name == "SSD 80-track");
}

TEST_CASE("SsdFormatHandler detects standard SSD 40-track", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    auto data = make_ssd_image(40);
    auto result = handler.detect(data, ".ssd");

    CHECK(result.detected);
    CHECK(result.confidence >= 90);
    CHECK(result.format_name == "SSD 40-track");
}

TEST_CASE("SsdFormatHandler detects standard DSD 80-track", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    auto data = make_dsd_image(80);
    auto result = handler.detect(data, ".dsd");

    CHECK(result.detected);
    CHECK(result.confidence >= 90);
}

TEST_CASE("SsdFormatHandler detects truncated SSD", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    // 3 tracks of data (7680 bytes) - truncated
    auto data = make_ssd_image(3);
    auto result = handler.detect(data, ".ssd");

    CHECK(result.detected);
    CHECK(result.confidence >= 70);
    CHECK(result.confidence < 90);  // Lower than standard
    CHECK(result.format_name == "SSD (truncated)");
}

TEST_CASE("SsdFormatHandler rejects wrong extension", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    auto data = make_ssd_image(80);
    auto result = handler.detect(data, ".hfe");

    CHECK_FALSE(result.detected);
}

TEST_CASE("SsdFormatHandler rejects non-sector-aligned size", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    std::vector<uint8_t> data(1000);  // Not a multiple of 256
    auto result = handler.detect(data, ".ssd");

    CHECK_FALSE(result.detected);
}

TEST_CASE("SsdFormatHandler rejects empty file", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    std::vector<uint8_t> data;
    auto result = handler.detect(data, ".ssd");

    CHECK_FALSE(result.detected);
}

TEST_CASE("SsdFormatHandler rejects oversized SSD", "[disc][ssd][detect]") {
    SsdFormatHandler handler;
    std::vector<uint8_t> data(81 * 10 * 256);  // 81 tracks > max 80
    auto result = handler.detect(data, ".ssd");

    CHECK_FALSE(result.detected);
}

// =============================================================================
// Loading Tests
// =============================================================================

TEST_CASE("SsdFormatHandler loads standard SSD 80-track", "[disc][ssd][load]") {
    SsdFormatHandler handler;
    auto data = make_ssd_image(80);
    auto result = handler.load(data, "/tmp/test.ssd");

    REQUIRE(result.success());
    CHECK(result.disc->name() == "test.ssd");
    CHECK_FALSE(result.disc->is_double_sided());
    CHECK(result.disc->format_name() == "SSD");
}

TEST_CASE("SsdFormatHandler loads DSD and marks as double-sided", "[disc][ssd][load]") {
    SsdFormatHandler handler;
    auto data = make_dsd_image(80);
    auto result = handler.load(data, "/tmp/test.dsd");

    REQUIRE(result.success());
    CHECK(result.disc->is_double_sided());
    CHECK(result.disc->format_name() == "DSD");
}

TEST_CASE("Loaded SSD has correct sector data via TrackDecoder", "[disc][ssd][load][roundtrip]") {
    SsdFormatHandler handler;
    auto data = make_ssd_image(80);
    auto result = handler.load(data, "/tmp/test.ssd");
    REQUIRE(result.success());

    // Verify sectors on several tracks
    for (uint8_t t : {0, 1, 39, 79}) {
        TrackDecoder decoder(result.disc->track(false, t));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() == 10);

        for (uint8_t s = 0; s < 10; ++s) {
            CHECK(sectors[s].track == t);
            CHECK(sectors[s].side == 0);
            CHECK(sectors[s].sector == s);
            CHECK_FALSE(sectors[s].has_header_crc_error);
            CHECK_FALSE(sectors[s].has_data_crc_error);

            std::array<uint8_t, 256> buffer{};
            decoder.read_sector_data(sectors[s], buffer);

            uint8_t expected = static_cast<uint8_t>((t * 10 + s) & 0xFF);
            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == expected);
            }
        }
    }
}

TEST_CASE("Loaded DSD has correct sector data on both sides", "[disc][ssd][load][roundtrip]") {
    SsdFormatHandler handler;
    auto data = make_dsd_image(40);
    auto result = handler.load(data, "/tmp/test.dsd");
    REQUIRE(result.success());

    for (uint8_t side = 0; side < 2; ++side) {
        TrackDecoder decoder(result.disc->track(side == 1, 0));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() == 10);

        for (uint8_t s = 0; s < 10; ++s) {
            CHECK(sectors[s].side == side);

            std::array<uint8_t, 256> buffer{};
            decoder.read_sector_data(sectors[s], buffer);

            uint8_t expected = static_cast<uint8_t>((0 * 20 + side * 10 + s) & 0xFF);
            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == expected);
            }
        }
    }
}

// =============================================================================
// Truncated Image Tests
// =============================================================================

TEST_CASE("Truncated SSD loads successfully with zero-filled missing sectors", "[disc][ssd][truncated]") {
    SsdFormatHandler handler;
    // Only 3 tracks of data (beebasm-style truncated image)
    auto data = make_ssd_image(3);
    auto result = handler.load(data, "/tmp/truncated.ssd");
    REQUIRE(result.success());

    // Verify the 3 tracks that have data
    for (uint8_t t = 0; t < 3; ++t) {
        TrackDecoder decoder(result.disc->track(false, t));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() == 10);

        for (uint8_t s = 0; s < 10; ++s) {
            std::array<uint8_t, 256> buffer{};
            decoder.read_sector_data(sectors[s], buffer);

            uint8_t expected = static_cast<uint8_t>((t * 10 + s) & 0xFF);
            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == expected);
            }
        }
    }

    // Verify track 3 onwards has zero-filled sectors (blank formatted)
    {
        TrackDecoder decoder(result.disc->track(false, 3));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() == 10);

        for (uint8_t s = 0; s < 10; ++s) {
            std::array<uint8_t, 256> buffer{};
            buffer.fill(0xFF);  // Pre-fill to detect zero-fill
            decoder.read_sector_data(sectors[s], buffer);

            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == 0x00);
            }
        }
    }
}

TEST_CASE("Truncated SSD with partial track loads correctly", "[disc][ssd][truncated]") {
    SsdFormatHandler handler;
    // 2 full tracks + 5 sectors (2 * 2560 + 5 * 256 = 6400 bytes)
    std::vector<uint8_t> data(6400);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    auto result = handler.load(data, "/tmp/partial.ssd");
    REQUIRE(result.success());

    // Track 2 should have 5 sectors with data and 5 zero-filled
    TrackDecoder decoder(result.disc->track(false, 2));
    auto sectors = decoder.find_sectors();
    REQUIRE(sectors.size() == 10);

    // First 5 sectors have data from the file
    for (uint8_t s = 0; s < 5; ++s) {
        std::array<uint8_t, 256> buffer{};
        decoder.read_sector_data(sectors[s], buffer);
        // Verify first byte matches expected offset
        size_t expected_offset = (2 * 10 + s) * 256;
        CHECK(buffer[0] == static_cast<uint8_t>(expected_offset & 0xFF));
    }

    // Last 5 sectors should be zero-filled
    for (uint8_t s = 5; s < 10; ++s) {
        std::array<uint8_t, 256> buffer{};
        buffer.fill(0xFF);
        decoder.read_sector_data(sectors[s], buffer);
        for (int i = 0; i < 256; ++i) {
            CHECK(buffer[i] == 0x00);
        }
    }
}

TEST_CASE("Sub-track truncated SSD loads with valid sectors on all tracks", "[disc][ssd][truncated]") {
    SsdFormatHandler handler;
    // 4 sectors = 1024 bytes (less than one full track of 2560 bytes).
    // This is the size beebasm produces for a minimal disc image.
    std::vector<uint8_t> data(1024);
    // Fill with recognisable pattern
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    auto result = handler.load(data, "/tmp/subtracks.ssd");
    REQUIRE(result.success());

    // Track 0: sectors 0-3 should have file data, sectors 4-9 zero-filled
    {
        TrackDecoder decoder(result.disc->track(false, 0));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() == 10);

        // Sectors with data
        for (uint8_t s = 0; s < 4; ++s) {
            CHECK(sectors[s].sector == s);
            CHECK(sectors[s].has_data_field);
            std::array<uint8_t, 256> buffer{};
            decoder.read_sector_data(sectors[s], buffer);
            CHECK(buffer[0] == static_cast<uint8_t>((s * 256) & 0xFF));
        }

        // Zero-filled sectors
        for (uint8_t s = 4; s < 10; ++s) {
            CHECK(sectors[s].sector == s);
            CHECK(sectors[s].has_data_field);
            std::array<uint8_t, 256> buffer{};
            buffer.fill(0xFF);
            decoder.read_sector_data(sectors[s], buffer);
            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == 0x00);
            }
        }
    }

    // Track 1: all 10 sectors should be zero-filled with valid ID fields
    {
        TrackDecoder decoder(result.disc->track(false, 1));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() == 10);

        for (uint8_t s = 0; s < 10; ++s) {
            CHECK(sectors[s].track == 1);
            CHECK(sectors[s].sector == s);
            CHECK(sectors[s].has_data_field);
            std::array<uint8_t, 256> buffer{};
            buffer.fill(0xFF);
            decoder.read_sector_data(sectors[s], buffer);
            for (int i = 0; i < 256; ++i) {
                CHECK(buffer[i] == 0x00);
            }
        }
    }

    // Track 79: should also have valid sectors
    {
        TrackDecoder decoder(result.disc->track(false, 79));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() == 10);
        CHECK(sectors[0].track == 79);
        CHECK(sectors[0].has_data_field);
    }
}

// =============================================================================
// Registry Integration Tests
// =============================================================================

TEST_CASE("DiscFormatRegistry loads SSD via auto-detection", "[disc][registry]") {
    DiscFormatRegistry registry;
    registry.register_handler(std::make_unique<SsdFormatHandler>());

    auto data = make_ssd_image(80);
    auto result = registry.load_from_data(data, ".ssd", "/tmp/test.ssd");

    REQUIRE(result.success());
    CHECK(result.disc->format_name() == "SSD");
}

TEST_CASE("DiscFormatRegistry rejects unknown format", "[disc][registry]") {
    DiscFormatRegistry registry;
    registry.register_handler(std::make_unique<SsdFormatHandler>());

    std::vector<uint8_t> data(1024);
    auto result = registry.load_from_data(data, ".xyz", "/tmp/test.xyz");

    CHECK_FALSE(result.success());
}

TEST_CASE("DiscFormatRegistry reports error for empty handler list", "[disc][registry]") {
    DiscFormatRegistry registry;  // No handlers registered

    std::vector<uint8_t> data(1024);
    auto result = registry.load_from_data(data, ".ssd", "/tmp/test.ssd");

    CHECK_FALSE(result.success());
}

// =============================================================================
// Write-back safety (issue #88): a disc image must not be silently damaged.
//
// SsdFormatHandler installs ssd_write_track_callback, which flushes a dirty
// track's sectors back to the host image. It must persist a sector ONLY if it
// decoded cleanly -- good ID CRC, good data CRC, and a complete (full length)
// data field. A bad-CRC or short/incomplete sector must leave the host image's
// existing bytes untouched, and must never grow the file with zero-padding the
// guest did not write.
// =============================================================================

namespace {

// Write bytes to a real, writable temp file (write-back needs a source_filepath
// that exists on disk). Returns the path; the caller removes it.
std::filesystem::path write_temp_ssd(const std::vector<uint8_t>& bytes,
                                     const std::string& suffix = ".ssd") {
    // Unique per call AND per process: catch_discover_tests runs each test case
    // in its own process and ctest runs them in parallel, so a shared name would
    // collide in the temp directory.
    static int counter = 0;
    std::random_device rd;
    auto token = std::to_string(rd()) + "_" + std::to_string(++counter);
    auto path = std::filesystem::temp_directory_path() /
                ("beebium_writeback_" + token + suffix);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return path;
}

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Build one FM track holding three sectors that exercise the write-back guard:
//   sector 0: good ID CRC, good data CRC, full 256 bytes of `good_fill`
//   sector 1: good ID CRC, but a deliberately WRONG data CRC (data = bad_fill)
//   sector 2: a data field that is cut off short (the track ends mid-data)
// The track ends inside sector 2, exactly as a truncated image's partial track
// would, so read_sector_data cannot return a full sector for it.
void build_good_bad_short_track(DiscTrack& track, uint8_t id_track, uint8_t good_fill,
                                uint8_t bad_fill, uint8_t short_fill) {
    TrackBuilder b(track);
    b.append_fm_bytes(0xFF, k_std_gap1_FFs);
    b.append_fm_bytes(0x00, k_std_sync_00s);

    auto id_field = [&](uint8_t sector) {
        b.reset_crc(false);
        b.append_fm_data_and_clocks(k_id_mark_data_pattern, k_mark_clock_pattern);
        b.append_fm_byte(id_track);  // track
        b.append_fm_byte(0);        // side
        b.append_fm_byte(sector);   // sector id
        b.append_fm_byte(0x01);     // 256-byte sectors
        b.append_crc_fm();
        b.append_fm_bytes(0xFF, k_std_gap2_FFs);
        b.append_fm_bytes(0x00, k_std_sync_00s);
    };
    auto gap3 = [&]() {
        b.append_fm_bytes(0xFF, k_std_10_sector_gap3_FFs);
        b.append_fm_bytes(0x00, k_std_sync_00s);
    };

    // Sector 0: valid.
    id_field(0);
    b.reset_crc(false);
    b.append_fm_data_and_clocks(k_data_mark_data_pattern, k_mark_clock_pattern);
    for (int i = 0; i < 256; ++i) b.append_fm_byte(good_fill);
    b.append_crc_fm();
    gap3();

    // Sector 1: complete data field, but the stored data CRC is wrong.
    id_field(1);
    b.reset_crc(false);
    b.append_fm_data_and_clocks(k_data_mark_data_pattern, k_mark_clock_pattern);
    for (int i = 0; i < 256; ++i) b.append_fm_byte(bad_fill);
    b.append_fm_byte(0x00);  // wrong CRC (not the computed one)
    b.append_fm_byte(0x00);
    gap3();

    // Sector 2: data field cut short -- the track ends before the sector does.
    id_field(2);
    b.reset_crc(false);
    b.append_fm_data_and_clocks(k_data_mark_data_pattern, k_mark_clock_pattern);
    for (int i = 0; i < 128; ++i) b.append_fm_byte(short_fill);
    b.finalize();
}

}  // namespace

TEST_CASE("Write-back persists only fully-valid sectors (#88)",
          "[disc][ssd][writeback]") {
    SsdFormatHandler handler;
    // A single-track image pre-filled with 0xEE, so any sector the callback
    // leaves alone is still 0xEE and any it rewrites is visibly different.
    std::vector<uint8_t> original(2560, 0xEE);
    auto path = write_temp_ssd(original);

    auto result = handler.load(original, path.string());
    REQUIRE(result.success());

    build_good_bad_short_track(result.disc->track(false, 0), /*id_track*/ 0,
                               /*good*/ 0xA0, /*bad*/ 0xA1, /*short*/ 0xA2);
    result.disc->track(false, 0).set_dirty(true);

    // The crafted track really does present all three sectors to the decoder.
    {
        TrackDecoder decoder(result.disc->track(false, 0));
        auto sectors = decoder.find_sectors();
        REQUIRE(sectors.size() >= 3);
    }

    result.disc->flush_track(false, 0);

    auto after = read_file(path);
    std::filesystem::remove(path);

    REQUIRE(after.size() == original.size());  // no growth from short/invalid sectors
    for (int i = 0; i < 256; ++i) {
        CHECK(after[i] == 0xA0);          // sector 0: valid -> rewritten
        CHECK(after[256 + i] == 0xEE);    // sector 1: bad data CRC -> untouched
        CHECK(after[512 + i] == 0xEE);    // sector 2: short data field -> untouched
    }
}

TEST_CASE("Write-back to a DSD upper side persists only valid sectors (#88)",
          "[disc][ssd][writeback]") {
    SsdFormatHandler handler;
    // One track per side (2 * 2560 bytes), prefilled 0xEE.
    std::vector<uint8_t> original(2 * 2560, 0xEE);
    auto path = write_temp_ssd(original, ".dsd");

    auto result = handler.load(original, path.string());
    REQUIRE(result.success());
    REQUIRE(result.disc->is_double_sided());

    build_good_bad_short_track(result.disc->track(true, 0), /*id_track*/ 0,
                               /*good*/ 0xB0, /*bad*/ 0xB1, /*short*/ 0xB2);
    result.disc->track(true, 0).set_dirty(true);
    result.disc->flush_track(true, 0);

    auto after = read_file(path);
    std::filesystem::remove(path);

    REQUIRE(after.size() == original.size());
    // DSD interleave: track 0 side 1 begins at offset 2560.
    const size_t base = 2560;
    for (int i = 0; i < 256; ++i) {
        CHECK(after[base + i] == 0xB0);          // good -> written
        CHECK(after[base + 256 + i] == 0xEE);    // bad CRC -> untouched
        CHECK(after[base + 512 + i] == 0xEE);    // short -> untouched
    }
    // Side 0 of track 0 (offsets 0..2560) is untouched.
    for (int i = 0; i < 2560; ++i) CHECK(after[i] == 0xEE);
}

TEST_CASE("Write-back extends the image only for a valid sector past EOF (#88)",
          "[disc][ssd][writeback]") {
    SsdFormatHandler handler;
    // A single-track image; track 1 lies entirely past the file's extent.
    std::vector<uint8_t> original(2560, 0xEE);
    auto path = write_temp_ssd(original);

    auto result = handler.load(original, path.string());
    REQUIRE(result.success());

    // Track 1 holds one valid sector (0, at offset 2560 == old EOF) plus a
    // bad-CRC sector (1) and a short sector (2). Only the valid sector may be
    // written, so the file grows by exactly one sector; the gap is nil here
    // because the valid sector sits at the old end-of-file. (Were a valid
    // sector to sit beyond a gap, seekp would extend the file and the OS
    // zero-fills the skipped span -- but no invalid sector ever triggers that.)
    build_good_bad_short_track(result.disc->track(false, 1), /*id_track*/ 1,
                               /*good*/ 0xC0, /*bad*/ 0xC1, /*short*/ 0xC2);
    result.disc->track(false, 1).set_dirty(true);
    result.disc->flush_track(false, 1);

    auto after = read_file(path);
    std::filesystem::remove(path);

    // Grew by exactly the one valid sector, not by the bad or short ones.
    REQUIRE(after.size() == 2560 + 256);
    for (int i = 0; i < 256; ++i) CHECK(after[2560 + i] == 0xC0);
    // The original track 0 is untouched.
    for (int i = 0; i < 2560; ++i) CHECK(after[i] == 0xEE);
}
