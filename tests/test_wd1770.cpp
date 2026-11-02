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

#include <beebium/disc/WD1770.hpp>
#include <beebium/disc/DiscDrive.hpp>
#include <beebium/disc/formats/AdfsFormatHandler.hpp>
#include <beebium/disc/formats/SsdFormatHandler.hpp>
#include <beebium/disc/TrackDecoder.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using namespace beebium;

// Helper: create an SSD disc with known sector data.
// Each sector byte = (track * 10 + sector + byte_index) & 0xFF
static std::unique_ptr<Disc> make_test_disc() {
    std::vector<uint8_t> data(80 * 10 * 256);
    for (int t = 0; t < 80; ++t) {
        for (int s = 0; s < 10; ++s) {
            for (int i = 0; i < 256; ++i) {
                size_t offset = (t * 10 + s) * 256 + i;
                data[offset] = static_cast<uint8_t>((t * 10 + s + i) & 0xFF);
            }
        }
    }
    SsdFormatHandler handler;
    auto result = handler.load(data, "/tmp/test.ssd");
    return std::move(result.disc);
}

// Helper: set up a WD1770 + drive with a test disc, spin-up delay disabled.
struct TestRig {
    WD1770 fdc;
    DiscDrive drive;

    TestRig() {
        fdc.set_spin_up_delay_enabled(false);
        fdc.attach_drive(0, &drive);
        fdc.set_drive(0);
        fdc.set_side(0);
        fdc.set_density(false);  // FM (single density)
        drive.insert(make_test_disc());
        drive.set_side(0);
    }

    // Tick until not busy or timeout.
    void run_until_complete(int max_ticks = 500'000) {
        for (int i = 0; i < max_ticks; ++i) {
            fdc.tick();
            if (!fdc.busy()) break;
        }
    }

    // Tick until DRQ asserts or timeout.
    bool wait_for_drq(int max_ticks = 500'000) {
        for (int i = 0; i < max_ticks; ++i) {
            fdc.tick();
            if (fdc.drq()) return true;
            if (!fdc.busy()) return false;  // Command completed without DRQ
        }
        return false;
    }

    // Read a byte from the data register (clears DRQ).
    uint8_t read_data() {
        return fdc.read(3);
    }

    // Read status register (clears INTRQ).
    uint8_t read_status() {
        return fdc.read(0);
    }

    // Write to command register.
    void write_command(uint8_t cmd) {
        fdc.write(0, cmd);
    }

    // Write to sector register.
    void write_sector_reg(uint8_t sec) {
        fdc.write(2, sec);
    }

    // Write to data register.
    void write_data(uint8_t val) {
        fdc.write(3, val);
    }

    // Write to track register.
    void write_track_reg(uint8_t trk) {
        fdc.write(1, trk);
    }
};

// =============================================================================
// Basic State
// =============================================================================

TEST_CASE("WD1770 initial state", "[disc][wd1770p]") {
    WD1770 fdc;
    CHECK_FALSE(fdc.busy());
    CHECK_FALSE(fdc.drq());
    CHECK_FALSE(fdc.intrq());
    CHECK(fdc.read(1) == 0);   // Track register = 0
    CHECK(fdc.read(2) == 1);   // Sector register = 1
}

// =============================================================================
// Type I: Restore
// =============================================================================

TEST_CASE("WD1770 Restore seeks to track 0", "[disc][wd1770p][restore]") {
    TestRig rig;
    rig.drive.seek(40);

    rig.write_command(0x00);  // Restore, 6ms step rate
    rig.run_until_complete();

    CHECK(rig.drive.at_track_0());
    CHECK(rig.fdc.read(1) == 0);  // Track register = 0
    CHECK(rig.fdc.intrq());
}

// =============================================================================
// Type I: Seek
// =============================================================================

TEST_CASE("WD1770 Seek moves to target track", "[disc][wd1770p][seek]") {
    TestRig rig;

    // Seek to track 20
    rig.write_data(20);           // Data register = target track
    rig.write_command(0x10);      // Seek
    rig.run_until_complete();

    CHECK(rig.drive.current_track() == 20);
    CHECK(rig.fdc.read(1) == 20);
    CHECK(rig.fdc.intrq());
}

// =============================================================================
// Type II: Read Sector
// =============================================================================

TEST_CASE("WD1770 Read Sector reads correct data", "[disc][wd1770p][read]") {
    TestRig rig;

    // Seek to track 0 first (already there)
    rig.write_sector_reg(0);      // Read sector 0
    rig.write_command(0x80);      // Read Sector

    // Read all 256 bytes via DRQ
    std::array<uint8_t, 256> buffer{};
    for (int i = 0; i < 256; ++i) {
        REQUIRE(rig.wait_for_drq());
        buffer[i] = rig.read_data();
    }

    // Wait for command completion
    rig.run_until_complete();

    // Verify data matches what we put in the SSD
    // Sector 0 on track 0: byte[i] = (0*10 + 0 + i) & 0xFF = i
    for (int i = 0; i < 256; ++i) {
        CHECK(buffer[i] == static_cast<uint8_t>(i & 0xFF));
    }

    CHECK(rig.fdc.intrq());
    CHECK_FALSE(rig.fdc.busy());
}

TEST_CASE("WD1770 Read Sector on different sectors", "[disc][wd1770p][read]") {
    TestRig rig;

    for (uint8_t sector = 0; sector < 10; ++sector) {
        rig.write_sector_reg(sector);
        rig.write_command(0x80);  // Read Sector

        std::array<uint8_t, 256> buffer{};
        for (int i = 0; i < 256; ++i) {
            REQUIRE(rig.wait_for_drq());
            buffer[i] = rig.read_data();
        }
        rig.run_until_complete();

        // Verify: byte[i] = (0*10 + sector + i) & 0xFF
        for (int i = 0; i < 256; ++i) {
            CHECK(buffer[i] == static_cast<uint8_t>((sector + i) & 0xFF));
        }
    }
}

TEST_CASE("WD1770 Read Sector after seek", "[disc][wd1770p][read]") {
    TestRig rig;

    // Seek to track 5
    rig.write_data(5);
    rig.write_command(0x10);  // Seek
    rig.run_until_complete();

    // Read sector 3
    rig.write_sector_reg(3);
    rig.write_command(0x80);

    std::array<uint8_t, 256> buffer{};
    for (int i = 0; i < 256; ++i) {
        REQUIRE(rig.wait_for_drq());
        buffer[i] = rig.read_data();
    }
    rig.run_until_complete();

    // Verify: byte[i] = (5*10 + 3 + i) & 0xFF = (53 + i) & 0xFF
    for (int i = 0; i < 256; ++i) {
        CHECK(buffer[i] == static_cast<uint8_t>((53 + i) & 0xFF));
    }
}

TEST_CASE("WD1770 Read Sector sets RNF for invalid sector", "[disc][wd1770p][read]") {
    TestRig rig;

    // Try to read sector 15 (DFS only has sectors 0-9)
    rig.write_sector_reg(15);
    rig.write_command(0x80);

    // Should eventually timeout with RNF
    rig.run_until_complete(10'000'000);  // Long timeout for 5-revolution search

    uint8_t status = rig.read_status();
    CHECK((status & 0x10) != 0);  // RNF bit set
}

// =============================================================================
// Type II: Write Sector
// =============================================================================

TEST_CASE("WD1770 Write Sector then Read back", "[disc][wd1770p][write]") {
    TestRig rig;

    // Write sector 0 with test pattern
    rig.write_sector_reg(0);
    rig.write_command(0xA0);  // Write Sector

    for (int i = 0; i < 256; ++i) {
        REQUIRE(rig.wait_for_drq());
        rig.write_data(static_cast<uint8_t>(0xFF - i));
    }
    rig.run_until_complete();
    CHECK(rig.fdc.intrq());

    // Now read it back
    rig.write_sector_reg(0);
    rig.write_command(0x80);  // Read Sector

    std::array<uint8_t, 256> buffer{};
    for (int i = 0; i < 256; ++i) {
        REQUIRE(rig.wait_for_drq());
        buffer[i] = rig.read_data();
    }
    rig.run_until_complete();

    for (int i = 0; i < 256; ++i) {
        CHECK(buffer[i] == static_cast<uint8_t>(0xFF - i));
    }
}

TEST_CASE("WD1770 MFM Write Sector then Read back on both sides of an ADL disc",
          "[disc][wd1770p][write][mfm]") {
    // ADFS writes in double density (#246). A written sector must keep the
    // 3xA1 sync before its data mark, or the controller cannot find its data
    // again and ADFS fails on the next read of that sector.
    WD1770 fdc;
    DiscDrive drive;
    fdc.set_spin_up_delay_enabled(false);
    fdc.attach_drive(0, &drive);
    fdc.set_drive(0);
    fdc.set_density(true);  // MFM
    AdfsFormatHandler handler;
    auto result = handler.load(std::vector<uint8_t>(80 * 2 * 16 * 256), "wd1770-mfm-test.adl");
    REQUIRE(result.disc);
    drive.insert(std::move(result.disc));

    auto run_until_complete = [&] {
        for (int i = 0; i < 500'000 && fdc.busy(); ++i) fdc.tick();
    };
    auto wait_for_drq = [&] {
        for (int i = 0; i < 500'000; ++i) {
            fdc.tick();
            if (fdc.drq()) return true;
            if (!fdc.busy()) return false;
        }
        return false;
    };

    for (const uint8_t side : {0, 1}) {
        CAPTURE(side);
        fdc.set_side(side);
        drive.set_side(side);
        const uint8_t sector = side == 0 ? 2 : 5;

        fdc.write(2, sector);
        fdc.write(0, 0xA0);  // Write Sector
        for (int i = 0; i < 256; ++i) {
            REQUIRE(wait_for_drq());
            fdc.write(3, static_cast<uint8_t>(i ^ (side ? 0x5A : 0xA5)));
        }
        run_until_complete();
        CHECK((fdc.read(0) & 0x5C) == 0);  // no WP, RNF, CRC or lost data

        fdc.write(2, sector);
        fdc.write(0, 0x80);  // Read Sector
        std::array<uint8_t, 256> buffer{};
        for (int i = 0; i < 256; ++i) {
            REQUIRE(wait_for_drq());
            buffer[i] = fdc.read(3);
        }
        run_until_complete();
        CHECK((fdc.read(0) & 0x1C) == 0);  // no RNF, CRC or lost data
        for (int i = 0; i < 256; ++i) {
            CHECK(buffer[i] == static_cast<uint8_t>(i ^ (side ? 0x5A : 0xA5)));
        }

        // The decoder that writes the image back must see a sound sector too.
        TrackDecoder decoder(drive.disc()->track(side == 1, 0));
        bool persisted = false;
        for (const auto& sec : decoder.find_sectors()) {
            if (sec.sector == sector) {
                persisted = sec.has_data_field && !sec.has_data_crc_error;
            }
        }
        CHECK(persisted);
    }
}

namespace {

// A sector's contents in the format tests. Write Track reads 0xF5 to 0xFE
// as control bytes, so a formatter's data stays below them.
uint8_t sector_byte(uint8_t sector, int i) {
    return static_cast<uint8_t>((sector * 7 + i) & 0x7F);
}

// The byte stream a formatter gives Write Track for one track, laid out as
// the WD1770 datasheet recommends.
std::vector<uint8_t> format_stream(bool mfm, uint8_t track, uint8_t sectors) {
    std::vector<uint8_t> out;
    auto put = [&](uint8_t byte, size_t count) { out.insert(out.end(), count, byte); };
    const uint8_t gap = mfm ? 0x4E : 0xFF;
    const size_t sync = mfm ? 12 : 6;
    auto marks = [&] {
        if (mfm) put(0xF5, 3);
    };
    put(gap, mfm ? 60 : 16);
    for (uint8_t sector = 0; sector < sectors; ++sector) {
        put(0x00, sync);
        marks();
        out.insert(out.end(), {0xFE, track, 0x00, sector, 0x01, 0xF7});
        put(gap, mfm ? 22 : 11);
        put(0x00, sync);
        marks();
        put(0xFB, 1);
        for (int i = 0; i < 256; ++i) put(sector_byte(sector, i), 1);
        put(0xF7, 1);
        put(gap, mfm ? 24 : 21);  // FM: Acorn DFS's gap3, which fills the track
    }
    return out;
}

// Format track 0 side 0 through Write Track, then read every sector back
// through Read Sector and through the decoder the image write-back uses.
void check_format_reads_back(WD1770& fdc, DiscDrive& drive, bool mfm, uint8_t sectors) {
    auto run_until_complete = [&] {
        for (int i = 0; i < 2'000'000 && fdc.busy(); ++i) fdc.tick();
    };
    auto wait_for_drq = [&] {
        for (int i = 0; i < 500'000; ++i) {
            fdc.tick();
            if (fdc.drq()) return true;
            if (!fdc.busy()) return false;
        }
        return false;
    };

    const auto stream = format_stream(mfm, 0, sectors);
    fdc.write(0, 0xF0);  // Write Track
    size_t next = 0;
    while (wait_for_drq()) {
        fdc.write(3, next < stream.size() ? stream[next] : (mfm ? 0x4E : 0xFF));
        ++next;
    }
    run_until_complete();
    REQUIRE(next > stream.size());  // the whole format fitted the track
    CHECK((fdc.read(0) & 0x5C) == 0);

    for (uint8_t sector = 0; sector < sectors; ++sector) {
        CAPTURE(sector);
        fdc.write(2, sector);
        fdc.write(0, 0x80);  // Read Sector
        for (int i = 0; i < 256; ++i) {
            REQUIRE(wait_for_drq());
            CAPTURE(i);
            REQUIRE(int(fdc.read(3)) == int(sector_byte(sector, i)));
        }
        run_until_complete();
        CHECK((fdc.read(0) & 0x1C) == 0);
    }

    TrackDecoder decoder(drive.disc()->track(false, 0));
    size_t sound = 0;
    for (const auto& sec : decoder.find_sectors()) {
        if (!sec.has_header_crc_error && sec.has_data_field && !sec.has_data_crc_error) ++sound;
    }
    CHECK(sound == sectors);
}

}  // namespace

TEST_CASE("WD1770 MFM Write Track formats sectors that read back", "[disc][wd1770p][format][mfm]") {
    // ADFS *FORMAT writes 16 sectors a track in MFM, each ID and data mark
    // after three F5 bytes, which the controller writes as A1 syncs (#246).
    WD1770 fdc;
    DiscDrive drive;
    fdc.set_spin_up_delay_enabled(false);
    fdc.attach_drive(0, &drive);
    fdc.set_drive(0);
    fdc.set_side(0);
    fdc.set_density(true);
    AdfsFormatHandler handler;
    auto result = handler.load(std::vector<uint8_t>(80 * 2 * 16 * 256), "wd1770-format-test.adl");
    REQUIRE(result.disc);
    drive.insert(std::move(result.disc));
    drive.set_side(0);
    check_format_reads_back(fdc, drive, true, 16);
}

TEST_CASE("WD1770 FM Write Track formats sectors that read back", "[disc][wd1770p][format]") {
    // In FM the controller writes 0xFE and 0xFB as address marks with the
    // 0xC7 clock, and starts each mark's CRC afresh.
    TestRig rig;
    check_format_reads_back(rig.fdc, rig.drive, false, 10);
}

TEST_CASE("WD1770 flushes dirty tracks after write inactivity",
          "[disc][wd1770p][write][flush]") {
    // After disc writes stop, the controller must flush dirty tracks to the
    // host image on its own short inactivity timer -- without waiting for the
    // ~2s motor spin-down, and decoupled from the eject path.
    TestRig rig;

    int flushes = 0;
    rig.drive.disc()->set_write_track_callback(
        [&](Disc&, bool, uint32_t) { ++flushes; });

    // Write a sector to dirty the current track.
    rig.write_sector_reg(0);
    rig.write_command(0xA0);  // Write Sector
    for (int i = 0; i < 256; ++i) {
        REQUIRE(rig.wait_for_drq());
        rig.write_data(static_cast<uint8_t>(i));
    }
    rig.run_until_complete();
    REQUIRE(rig.drive.disc()->has_dirty_tracks());

    int flushes_after_write = flushes;

    // Idle below the inactivity threshold (250'000 ticks): still unflushed.
    for (int i = 0; i < 150'000; ++i) rig.fdc.tick();
    CHECK(rig.drive.disc()->has_dirty_tracks());

    // Idle past the threshold: the controller flushes the dirty track.
    for (int i = 0; i < 250'000; ++i) rig.fdc.tick();
    CHECK_FALSE(rig.drive.disc()->has_dirty_tracks());
    CHECK(flushes > flushes_after_write);
}

// =============================================================================
// Type III: Read Address
// =============================================================================

TEST_CASE("WD1770 Read Address returns ID field", "[disc][wd1770p][readaddr]") {
    TestRig rig;

    rig.write_command(0xC0);  // Read Address

    // Should return 6 bytes: track, side, sector, size, CRC_hi, CRC_lo
    std::array<uint8_t, 6> id{};
    for (int i = 0; i < 6; ++i) {
        REQUIRE(rig.wait_for_drq());
        id[i] = rig.read_data();
    }
    rig.run_until_complete();

    CHECK(id[0] == 0);   // Track 0
    CHECK(id[1] == 0);   // Side 0
    // Sector number depends on where head is positioned
    CHECK(id[3] == 1);   // Size code 1 = 256 bytes
    CHECK(rig.fdc.intrq());
}

// =============================================================================
// Force Interrupt
// =============================================================================

TEST_CASE("WD1770 Force Interrupt terminates command", "[disc][wd1770p][forceint]") {
    TestRig rig;

    // Start a read sector
    rig.write_sector_reg(0);
    rig.write_command(0x80);

    // Let it run a few ticks
    for (int i = 0; i < 100; ++i) rig.fdc.tick();
    CHECK(rig.fdc.busy());

    // Force interrupt with no flags (0xD0) = terminate without interrupt
    rig.write_command(0xD0);
    CHECK_FALSE(rig.fdc.busy());
    CHECK_FALSE(rig.fdc.intrq());
}

TEST_CASE("WD1770 Force Interrupt with I3 generates INTRQ", "[disc][wd1770p][forceint]") {
    TestRig rig;

    rig.write_command(0xD8);  // Force interrupt, I3 = immediate
    CHECK(rig.fdc.intrq());
}

// =============================================================================
// Motor Control
// =============================================================================

TEST_CASE("WD1770 motor spins up on command", "[disc][wd1770p][motor]") {
    TestRig rig;

    CHECK_FALSE(rig.drive.motor_on());

    rig.write_command(0x80);  // Read Sector starts motor

    CHECK(rig.drive.motor_on());
}

// =============================================================================
// Write Protection
// =============================================================================

TEST_CASE("WD1770 Write Sector rejects write-protected disc", "[disc][wd1770p][wp]") {
    TestRig rig;
    rig.drive.disc()->set_write_protected(true);

    rig.write_sector_reg(0);
    rig.write_command(0xA0);  // Write Sector
    rig.run_until_complete();

    uint8_t status = rig.read_status();
    CHECK((status & 0x40) != 0);  // WRITE_PROT bit set
}

// =============================================================================
// NMI Signalling
// =============================================================================

TEST_CASE("WD1770 nmi_pending combines DRQ and INTRQ", "[disc][wd1770p][nmi]") {
    TestRig rig;

    CHECK_FALSE(rig.fdc.nmi_pending());

    // Start read - DRQ should assert for first byte
    rig.write_sector_reg(0);
    rig.write_command(0x80);
    rig.wait_for_drq();

    CHECK(rig.fdc.nmi_pending());  // DRQ is set
    CHECK(rig.fdc.drq());

    // Read the byte (clears DRQ)
    rig.read_data();
    CHECK_FALSE(rig.fdc.drq());
}
