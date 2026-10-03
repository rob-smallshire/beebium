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

#include <catch2/catch_test_macros.hpp>
#include <ScsiHardDisc.hpp>
#include <ScsiConstants.hpp>
#include <HardDiskImage.hpp>
#include "test_temp_paths.hpp"

#include <filesystem>

using namespace beebium;
using namespace beebium::scsi;

namespace {

struct TempDisc {
    beebium::test::ScopedTempDir dir{"beebium_test_shd_"};
    const std::filesystem::path& dirpath = dir.path();

    std::filesystem::path dat_filepath() const { return dirpath / "test.dat"; }

    // Create a small disc with known content: 2 cyl, 1 head = 66 sectors
    std::unique_ptr<ScsiHardDisc> create_disc() {
        auto image = HardDiskImage::create(dat_filepath(), 2, 1);
        // Write patterned data to first few sectors
        for (uint32_t s = 0; s < 10; ++s) {
            std::array<uint8_t, ACORN_BLOCK_SIZE> sector;
            sector.fill(static_cast<uint8_t>(s + 1));
            image->write_sector(sector.data(), s);
        }
        return std::make_unique<ScsiHardDisc>(std::move(image));
    }
};

}  // namespace

TEST_CASE("ScsiHardDisc TEST UNIT READY returns GOOD", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    uint8_t cdb[] = {OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_GOOD);
}

TEST_CASE("ScsiHardDisc is_present returns true", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();
    REQUIRE(disc->is_present());
    REQUIRE(disc->device_type() == "hard-disc");
}

TEST_CASE("ScsiHardDisc REQUEST SENSE returns no error initially", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    uint8_t cdb[] = {OP_REQUEST_SENSE, 0, 0, 0, 18, 0};
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 18);
    REQUIRE(result.data_in[0] == 0x70);      // Current errors
    REQUIRE(result.data_in[2] == SENSE_NO_SENSE);
}

TEST_CASE("ScsiHardDisc INQUIRY returns direct-access device", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    uint8_t cdb[] = {OP_INQUIRY, 0, 0, 0, 36, 0};
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 36);
    REQUIRE(result.data_in[0] == 0x00);  // Direct access device
    REQUIRE(result.data_in[2] == 0x01);  // SCSI-1
}

TEST_CASE("ScsiHardDisc READ CAPACITY returns correct values", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    uint8_t cdb[] = {OP_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 8);

    // Last LBA = 65 (66 sectors - 1), big-endian
    uint32_t last_lba = (static_cast<uint32_t>(result.data_in[0]) << 24)
                      | (static_cast<uint32_t>(result.data_in[1]) << 16)
                      | (static_cast<uint32_t>(result.data_in[2]) << 8)
                      | result.data_in[3];
    REQUIRE(last_lba == 65);

    // Block size = 256
    uint32_t block_size = (static_cast<uint32_t>(result.data_in[4]) << 24)
                        | (static_cast<uint32_t>(result.data_in[5]) << 16)
                        | (static_cast<uint32_t>(result.data_in[6]) << 8)
                        | result.data_in[7];
    REQUIRE(block_size == 256);
}

TEST_CASE("ScsiHardDisc READ(6) single sector", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    // Read sector 0 (filled with 0x01)
    uint8_t cdb[] = {OP_READ_6, 0, 0, 0, 1, 0};
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 256);
    REQUIRE(result.data_in[0] == 0x01);
    REQUIRE(result.data_in[255] == 0x01);
}

TEST_CASE("ScsiHardDisc READ(6) multi-sector", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    // Read 3 sectors from LBA 0
    uint8_t cdb[] = {OP_READ_6, 0, 0, 0, 3, 0};
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 768);
    REQUIRE(result.data_in[0] == 0x01);    // sector 0
    REQUIRE(result.data_in[256] == 0x02);  // sector 1
    REQUIRE(result.data_in[512] == 0x03);  // sector 2
}

TEST_CASE("ScsiHardDisc READ(6) invalid LBA returns CHECK CONDITION", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    // LBA 100 is beyond 66 sectors
    uint8_t cdb[] = {OP_READ_6, 0, 0, 100, 1, 0};
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_CHECK_CONDITION);
}

TEST_CASE("ScsiHardDisc WRITE(6) single sector", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    // First call: expect data
    uint8_t cdb[] = {OP_WRITE_6, 0, 0, 20, 1, 0};
    auto result1 = disc->execute(cdb, {});
    REQUIRE(result1.data_out_expected == 256);

    // Second call: with data
    std::vector<uint8_t> data(256, 0xBB);
    auto result2 = disc->execute(cdb, data);
    REQUIRE(result2.status == STATUS_GOOD);

    // Read back
    uint8_t read_cdb[] = {OP_READ_6, 0, 0, 20, 1, 0};
    auto read_result = disc->execute(read_cdb, {});
    REQUIRE(read_result.data_in[0] == 0xBB);
}

TEST_CASE("ScsiHardDisc MODE SENSE returns 22-byte geometry", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    uint8_t cdb[] = {OP_MODE_SENSE_6, 0, 0, 0, 22, 0};
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 22);
    // Cylinders = 2, heads = 1
    REQUIRE(result.data_in[13] == 0x00);
    REQUIRE(result.data_in[14] == 0x02);
    REQUIRE(result.data_in[15] == 0x01);
}

TEST_CASE("ScsiHardDisc MODE SELECT updates geometry", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    // First call: expect parameter data
    uint8_t cdb[] = {OP_MODE_SELECT_6, 0, 0, 0, 22, 0};
    auto result1 = disc->execute(cdb, {});
    REQUIRE(result1.data_out_expected == 22);

    // Second call: with new geometry
    std::vector<uint8_t> params(22, 0);
    params[13] = 0x00;
    params[14] = 4;  // 4 cylinders
    params[15] = 2;  // 2 heads
    auto result2 = disc->execute(cdb, params);
    REQUIRE(result2.status == STATUS_GOOD);

    // Verify via MODE SENSE
    uint8_t sense_cdb[] = {OP_MODE_SENSE_6, 0, 0, 0, 22, 0};
    auto sense_result = disc->execute(sense_cdb, {});
    REQUIRE(sense_result.data_in[14] == 4);
    REQUIRE(sense_result.data_in[15] == 2);
}

TEST_CASE("ScsiHardDisc TRANSLATE returns LBA in little-endian", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    uint8_t cdb[] = {OP_TRANSLATE, 0x01, 0x23, 0x45, 0, 0};
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_GOOD);
    REQUIRE(result.data_in.size() == 4);
    REQUIRE(result.data_in[0] == 0x45);
    REQUIRE(result.data_in[1] == 0x23);
    REQUIRE(result.data_in[2] == 0x01);
    REQUIRE(result.data_in[3] == 0x00);
}

TEST_CASE("ScsiHardDisc FORMAT UNIT zeros disc", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    // Sector 0 should have data
    uint8_t read_cdb[] = {OP_READ_6, 0, 0, 0, 1, 0};
    auto before = disc->execute(read_cdb, {});
    REQUIRE(before.data_in[0] == 0x01);

    // Format
    uint8_t fmt_cdb[] = {OP_FORMAT_UNIT, 0, 0, 0, 0, 0};
    auto fmt_result = disc->execute(fmt_cdb, {});
    REQUIRE(fmt_result.status == STATUS_GOOD);

    // Sector 0 should now be zeros
    auto after = disc->execute(read_cdb, {});
    REQUIRE(after.data_in[0] == 0x00);
}

TEST_CASE("ScsiHardDisc VERIFY valid range succeeds", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    uint8_t cdb[] = {OP_VERIFY_10, 0, 0, 0, 0, 0, 0, 0, 10, 0};
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_GOOD);
}

TEST_CASE("ScsiHardDisc VERIFY invalid range fails", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    // Verify 100 sectors starting at LBA 0 (only 66 exist)
    uint8_t cdb[] = {OP_VERIFY_10, 0, 0, 0, 0, 0, 0, 0, 100, 0};
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_CHECK_CONDITION);
}

TEST_CASE("ScsiHardDisc unsupported command returns ILLEGAL REQUEST", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    uint8_t cdb[] = {0xFF, 0, 0, 0, 0, 0};  // Unknown opcode
    auto result = disc->execute(cdb, {});
    REQUIRE(result.status == STATUS_CHECK_CONDITION);

    // REQUEST SENSE should report ILLEGAL REQUEST
    uint8_t sense_cdb[] = {OP_REQUEST_SENSE, 0, 0, 0, 18, 0};
    auto sense = disc->execute(sense_cdb, {});
    REQUIRE(sense.data_in[2] == SENSE_ILLEGAL_REQUEST);
}

TEST_CASE("ScsiHardDisc REQUEST SENSE clears after reading", "[scsi][hard-disc]") {
    TempDisc tmp;
    auto disc = tmp.create_disc();

    // Trigger an error
    uint8_t bad_cdb[] = {0xFF, 0, 0, 0, 0, 0};
    disc->execute(bad_cdb, {});

    // First REQUEST SENSE: should have error
    uint8_t sense_cdb[] = {OP_REQUEST_SENSE, 0, 0, 0, 18, 0};
    auto sense1 = disc->execute(sense_cdb, {});
    REQUIRE(sense1.data_in[2] == SENSE_ILLEGAL_REQUEST);

    // Second REQUEST SENSE: should be cleared
    auto sense2 = disc->execute(sense_cdb, {});
    REQUIRE(sense2.data_in[2] == SENSE_NO_SENSE);
}
