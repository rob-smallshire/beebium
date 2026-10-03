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
#include <HardDiskImage.hpp>
#include <ScsiConstants.hpp>
#include "test_temp_paths.hpp"

#include <array>
#include <cstdio>
#include <filesystem>

using namespace beebium;
using namespace beebium::scsi;

namespace {

// RAII temporary directory for test files
struct TempDir {
    beebium::test::ScopedTempDir dir{"beebium_test_hdi_"};
    const std::filesystem::path& dirpath = dir.path();

    std::filesystem::path dat_filepath() const { return dirpath / "test.dat"; }
};

// Create a test DAT file with patterned sectors
void create_test_dat(const std::filesystem::path& filepath, uint32_t num_sectors) {
    FILE* f = std::fopen(filepath.string().c_str(), "wb");
    REQUIRE(f != nullptr);
    for (uint32_t s = 0; s < num_sectors; ++s) {
        std::array<uint8_t, ACORN_BLOCK_SIZE> sector;
        sector.fill(static_cast<uint8_t>(s & 0xFF));
        std::fwrite(sector.data(), 1, ACORN_BLOCK_SIZE, f);
    }
    std::fclose(f);
}

// Create a test DSC file with known geometry
void create_test_dsc(const std::filesystem::path& filepath,
                     uint16_t cylinders, uint8_t heads) {
    std::array<uint8_t, 22> dsc{};
    dsc[3] = 0x80;
    dsc[10] = 1;
    dsc[12] = 1;
    dsc[13] = static_cast<uint8_t>((cylinders >> 8) & 0xFF);
    dsc[14] = static_cast<uint8_t>(cylinders & 0xFF);
    dsc[15] = heads;
    dsc[17] = 0x80;
    dsc[19] = 0x80;

    FILE* f = std::fopen(filepath.string().c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fwrite(dsc.data(), 1, 22, f);
    std::fclose(f);
}

}  // namespace

TEST_CASE("HardDiskImage create blank image", "[scsi][hdi]") {
    TempDir tmp;
    auto image = HardDiskImage::create(tmp.dat_filepath(), 4, 2);

    REQUIRE(image != nullptr);
    REQUIRE(image->cylinders() == 4);
    REQUIRE(image->heads() == 2);
    REQUIRE(image->total_sectors() == 4 * 2 * 33);
    REQUIRE_FALSE(image->is_write_protected());
}

TEST_CASE("HardDiskImage created image is all zeros", "[scsi][hdi]") {
    TempDir tmp;
    auto image = HardDiskImage::create(tmp.dat_filepath(), 2, 1);

    std::array<uint8_t, ACORN_BLOCK_SIZE> sector;
    REQUIRE(image->read_sector(sector.data(), 0));
    for (auto byte : sector) {
        REQUIRE(byte == 0x00);
    }
}

TEST_CASE("HardDiskImage created image has correct DSC", "[scsi][hdi]") {
    TempDir tmp;
    auto image = HardDiskImage::create(tmp.dat_filepath(), 100, 4);

    auto params = image->device_parameters();
    REQUIRE(params.size() == 22);
    // Cylinders big-endian at [13-14]
    REQUIRE(params[13] == 0x00);
    REQUIRE(params[14] == 100);
    // Heads at [15]
    REQUIRE(params[15] == 4);
}

TEST_CASE("HardDiskImage open existing DAT+DSC pair", "[scsi][hdi]") {
    TempDir tmp;
    // 2 cylinders, 1 head = 66 sectors
    create_test_dat(tmp.dat_filepath(), 66);
    auto dsc_filepath = tmp.dat_filepath();
    dsc_filepath.replace_extension(".dsc");
    create_test_dsc(dsc_filepath, 2, 1);

    auto image = HardDiskImage::open(tmp.dat_filepath());
    REQUIRE(image != nullptr);
    REQUIRE(image->cylinders() == 2);
    REQUIRE(image->heads() == 1);
    REQUIRE(image->total_sectors() == 66);
}

TEST_CASE("HardDiskImage open DAT with missing DSC auto-generates geometry", "[scsi][hdi]") {
    TempDir tmp;
    // 330 sectors = 10 heads * 1 cylinder * 33 SPT
    create_test_dat(tmp.dat_filepath(), 330);

    auto image = HardDiskImage::open(tmp.dat_filepath());
    REQUIRE(image != nullptr);
    REQUIRE(image->heads() == 10);  // default
    REQUIRE(image->cylinders() == 1);
    REQUIRE(image->total_sectors() == 330);
}

TEST_CASE("HardDiskImage read sector at LBA 0", "[scsi][hdi]") {
    TempDir tmp;
    create_test_dat(tmp.dat_filepath(), 66);
    auto dsc_filepath = tmp.dat_filepath();
    dsc_filepath.replace_extension(".dsc");
    create_test_dsc(dsc_filepath, 2, 1);

    auto image = HardDiskImage::open(tmp.dat_filepath());
    std::array<uint8_t, ACORN_BLOCK_SIZE> sector;
    REQUIRE(image->read_sector(sector.data(), 0));
    // First sector filled with 0x00 (sector index & 0xFF)
    REQUIRE(sector[0] == 0x00);
}

TEST_CASE("HardDiskImage read sector at last valid LBA", "[scsi][hdi]") {
    TempDir tmp;
    create_test_dat(tmp.dat_filepath(), 66);
    auto dsc_filepath = tmp.dat_filepath();
    dsc_filepath.replace_extension(".dsc");
    create_test_dsc(dsc_filepath, 2, 1);

    auto image = HardDiskImage::open(tmp.dat_filepath());
    std::array<uint8_t, ACORN_BLOCK_SIZE> sector;
    REQUIRE(image->read_sector(sector.data(), 65));
    REQUIRE(sector[0] == 65);  // sector 65 filled with 65
}

TEST_CASE("HardDiskImage read sector at invalid LBA returns false", "[scsi][hdi]") {
    TempDir tmp;
    create_test_dat(tmp.dat_filepath(), 66);
    auto dsc_filepath = tmp.dat_filepath();
    dsc_filepath.replace_extension(".dsc");
    create_test_dsc(dsc_filepath, 2, 1);

    auto image = HardDiskImage::open(tmp.dat_filepath());
    std::array<uint8_t, ACORN_BLOCK_SIZE> sector;
    REQUIRE_FALSE(image->read_sector(sector.data(), 66));  // one past end
    REQUIRE_FALSE(image->read_sector(sector.data(), 1000));
}

TEST_CASE("HardDiskImage write sector and read back", "[scsi][hdi]") {
    TempDir tmp;
    auto image = HardDiskImage::create(tmp.dat_filepath(), 2, 1);

    std::array<uint8_t, ACORN_BLOCK_SIZE> write_data;
    write_data.fill(0xAB);
    REQUIRE(image->write_sector(write_data.data(), 10));

    std::array<uint8_t, ACORN_BLOCK_SIZE> read_data;
    REQUIRE(image->read_sector(read_data.data(), 10));
    REQUIRE(read_data == write_data);

    // Adjacent sector should still be zeros
    REQUIRE(image->read_sector(read_data.data(), 11));
    REQUIRE(read_data[0] == 0x00);
}

TEST_CASE("HardDiskImage device parameters match DSC", "[scsi][hdi]") {
    TempDir tmp;
    create_test_dat(tmp.dat_filepath(), 66);
    auto dsc_filepath = tmp.dat_filepath();
    dsc_filepath.replace_extension(".dsc");
    create_test_dsc(dsc_filepath, 2, 1);

    auto image = HardDiskImage::open(tmp.dat_filepath());
    auto params = image->device_parameters();
    REQUIRE(params[13] == 0x00);  // cylinders high
    REQUIRE(params[14] == 0x02);  // cylinders low = 2
    REQUIRE(params[15] == 0x01);  // heads = 1
}

TEST_CASE("HardDiskImage set_device_parameters updates geometry", "[scsi][hdi]") {
    TempDir tmp;
    auto image = HardDiskImage::create(tmp.dat_filepath(), 4, 2);
    REQUIRE(image->cylinders() == 4);

    // Update geometry via MODE SELECT data
    std::array<uint8_t, 22> new_params{};
    new_params[13] = 0x00;
    new_params[14] = 8;  // 8 cylinders
    new_params[15] = 3;  // 3 heads
    image->set_device_parameters(new_params);

    REQUIRE(image->cylinders() == 8);
    REQUIRE(image->heads() == 3);
}

TEST_CASE("HardDiskImage format zeros all sectors", "[scsi][hdi]") {
    TempDir tmp;
    auto image = HardDiskImage::create(tmp.dat_filepath(), 2, 1);

    // Write some data
    std::array<uint8_t, ACORN_BLOCK_SIZE> data;
    data.fill(0xCC);
    image->write_sector(data.data(), 0);
    image->write_sector(data.data(), 32);

    // Format
    image->format();

    // All sectors should be zeros
    std::array<uint8_t, ACORN_BLOCK_SIZE> sector;
    image->read_sector(sector.data(), 0);
    REQUIRE(sector[0] == 0x00);
    image->read_sector(sector.data(), 32);
    REQUIRE(sector[0] == 0x00);

    // Geometry should be preserved
    REQUIRE(image->cylinders() == 2);
    REQUIRE(image->heads() == 1);
}

TEST_CASE("HardDiskImage total_sectors matches geometry", "[scsi][hdi]") {
    TempDir tmp;
    auto image = HardDiskImage::create(tmp.dat_filepath(), 10, 4);
    REQUIRE(image->total_sectors() == 10 * 4 * 33);
}

TEST_CASE("HardDiskImage is_valid_lba boundary", "[scsi][hdi]") {
    TempDir tmp;
    auto image = HardDiskImage::create(tmp.dat_filepath(), 2, 1);  // 66 sectors

    REQUIRE(image->is_valid_lba(0));
    REQUIRE(image->is_valid_lba(65));
    REQUIRE_FALSE(image->is_valid_lba(66));
    REQUIRE_FALSE(image->is_valid_lba(UINT32_MAX));
}
