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

// Tests for the Intel 8271 floppy disc controller.
//
// The host-protocol expectations marked "hardware" are those beebjit's test
// ROM (make_8271_rom.c) asserted on a real Model B. Tests named "...records
// an assumption" pin a behaviour the evidence does not settle (see
// docs/discussion/intel-8271-study.md, section 5); a later hardware finding
// should change that one test.

#include <beebium/disc/Intel8271.hpp>
#include <beebium/disc/DiscDrive.hpp>
#include <beebium/disc/TrackBuilder.hpp>
#include <beebium/disc/TrackDecoder.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <vector>

using namespace beebium;

namespace {

constexpr uint8_t STATUS_BUSY = 0x80;
constexpr uint8_t STATUS_RESULT_READY = 0x10;
constexpr uint8_t STATUS_INT = 0x08;
constexpr uint8_t STATUS_DATA_REQUEST = 0x04;

constexpr int TICKS_PER_MS = 1000;
constexpr int TICKS_PER_BYTE = 64;
constexpr int TICKS_PER_REV = 3125 * TICKS_PER_BYTE;

// Byte stored at offset i of a sector on the standard test disc.
uint8_t sector_byte(int track, int sector, int i) {
    return static_cast<uint8_t>(track * 10 + sector + i);
}

struct SectorSpec {
    std::optional<uint8_t> id_track;   // default: the physical track
    uint8_t id_sector = 0;
    uint8_t size_code = 1;
    bool deleted = false;
    bool bad_id_crc = false;
    bool bad_data_crc = false;
    bool bad_data_mark = false;
};

// Build an Acorn-layout FM track of ten 256-byte sectors (unless specs say
// otherwise): GAP1 16 x FF, then per sector 6 x 00, ID, 11 x FF, 6 x 00,
// data, 21 x FF.
void build_track(DiscTrack& track, int physical_track, std::vector<SectorSpec> specs = {}) {
    if (specs.empty()) {
        for (uint8_t s = 0; s < 10; ++s) {
            SectorSpec spec;
            spec.id_sector = s;
            specs.push_back(spec);
        }
    }
    track.clear();
    TrackBuilder b(track);
    b.reset();
    b.append_fm_bytes(0xFF, 16);
    for (const auto& spec : specs) {
        b.append_fm_bytes(0x00, 6);
        b.reset_crc(false);
        b.append_fm_data_and_clocks(ibm_disc_format::k_id_mark_data_pattern,
                                    ibm_disc_format::k_mark_clock_pattern);
        b.append_fm_byte(spec.id_track.value_or(static_cast<uint8_t>(physical_track)));
        b.append_fm_byte(0);
        b.append_fm_byte(spec.id_sector);
        b.append_fm_byte(spec.size_code);
        if (spec.bad_id_crc) {
            b.append_fm_data_and_clocks(0x12, 0xFF);
            b.append_fm_data_and_clocks(0x34, 0xFF);
        } else {
            b.append_crc_fm();
        }
        b.append_fm_bytes(0xFF, 11);
        b.append_fm_bytes(0x00, 6);
        b.reset_crc(false);
        uint8_t mark = spec.deleted ? ibm_disc_format::k_deleted_data_mark_data_pattern
                                    : ibm_disc_format::k_data_mark_data_pattern;
        b.append_fm_data_and_clocks(mark, spec.bad_data_mark ? 0xFF : ibm_disc_format::k_mark_clock_pattern);
        int size = 128 << spec.size_code;
        for (int i = 0; i < size; ++i) {
            b.append_fm_byte(sector_byte(physical_track, spec.id_sector, i));
        }
        if (spec.bad_data_crc) {
            b.append_fm_data_and_clocks(0x56, 0xFF);
            b.append_fm_data_and_clocks(0x78, 0xFF);
        } else {
            b.append_crc_fm();
        }
        b.append_fm_bytes(0xFF, 21);
    }
    b.fill_fm_byte(0xFF);
    track.set_dirty(false);
}

std::unique_ptr<Disc> make_disc() {
    auto disc = std::make_unique<Disc>();
    for (int t = 0; t < 80; ++t) {
        build_track(disc->track(false, t), t);
    }
    return disc;
}

// A controller with two drives, a formatted disc in drive 0, and a host
// that services the controller's interrupts as Acorn DFS does: a data
// request moves one byte, anything else is a completion.
struct Rig {
    Intel8271 fdc;
    DiscDrive drive_0;
    DiscDrive drive_1;

    // Host side of the transfer
    std::vector<uint8_t> received;          // bytes read from the data register
    std::vector<uint8_t> to_send;           // bytes to write to the data register
    size_t send_index = 0;
    int data_interrupts = 0;
    int completion_interrupts = 0;
    int interrupt_edges = 0;
    std::vector<uint64_t> data_interrupt_ticks;
    bool service_data = true;               // false: ignore data requests
    uint64_t ticks = 0;
    bool last_interrupt = false;

    explicit Rig(bool with_disc = true) {
        if (with_disc) drive_0.insert(make_disc());
        fdc.attach_drive(0, &drive_0);
        fdc.attach_drive(1, &drive_1);
        fdc.reset(ResetCause::PowerOn);
    }

    uint8_t status() { return fdc.read_register(0); }
    uint8_t result() { return fdc.read_register(1); }

    void write_command(uint8_t command) { fdc.write_register(0, command); }
    void write_parameter(uint8_t parameter) { fdc.write_register(1, parameter); }

    void issue(uint8_t command, std::initializer_list<uint8_t> parameters) {
        write_command(command);
        for (uint8_t p : parameters) write_parameter(p);
    }

    // One 1MHz tick, then service any new interrupt edge as an NMI handler would.
    void tick() {
        fdc.tick();
        ++ticks;
        bool interrupt = fdc.interrupt();
        if (interrupt && !last_interrupt) {
            ++interrupt_edges;
            uint8_t s = status();
            if (s & STATUS_DATA_REQUEST) {
                ++data_interrupts;
                data_interrupt_ticks.push_back(ticks);
                if (service_data) {
                    if (send_index < to_send.size()) {
                        fdc.write_data(to_send[send_index++]);
                    } else if (!to_send.empty()) {
                        fdc.write_data(0);
                    } else {
                        received.push_back(fdc.read_data());
                    }
                }
            } else {
                ++completion_interrupts;
            }
        }
        last_interrupt = fdc.interrupt();
    }

    void run(uint64_t n) {
        for (uint64_t i = 0; i < n; ++i) tick();
    }

    // Run until the controller is no longer busy; returns false on timeout.
    bool run_until_idle(uint64_t max_ticks = 5'000'000) {
        for (uint64_t i = 0; i < max_ticks; ++i) {
            if (!(status() & STATUS_BUSY)) return true;
            tick();
        }
        return false;
    }

    // The DFS initialisation: step 6ms, settle 16ms, no head load time,
    // unload after 12 index pulses; no bad tracks; non-DMA mode.
    void dfs_init(uint8_t head_load = 0xC0) {
        issue(0x35, {0x0D, 0x03, 0x08, head_load});
        issue(0x35, {0x10, 0xFF, 0xFF, 0x00});
        issue(0x35, {0x18, 0xFF, 0xFF, 0x00});
        issue(0x3A, {0x17, 0xC1});
    }

    // Select drive 0 and load its head, then wait until it reports ready.
    bool go_ready(uint8_t output = 0x48) {
        issue(0x3A, {0x23, output});
        for (int i = 0; i < 100; ++i) {
            issue(0x6C, {});
            if (result() & 0x04) return true;
            run(1000);
        }
        return false;
    }

    // Read a special register with drive 0's select bits, as DFS does, so
    // the command does not deselect the drive.
    uint8_t read_special(uint8_t reg) {
        issue(0x7D, {reg});
        return result();
    }
};

}  // namespace

// =============================================================================
// Host protocol (hardware-verified sequences)
// =============================================================================

TEST_CASE("8271 power-on registers read zero", "[disc][i8271][hardware]") {
    Rig rig;
    CHECK(rig.status() == 0x00);
    CHECK(rig.result() == 0x00);
    CHECK(rig.fdc.read_data() == 0x00);
    rig.fdc.write_data(0x41);
    CHECK(rig.fdc.read_data() == 0x41);
}

TEST_CASE("8271 READ DRIVE STATUS with no drive selected", "[disc][i8271][hardware]") {
    Rig rig;
    rig.write_command(0x2C);
    CHECK(rig.status() == STATUS_RESULT_READY);
    CHECK(rig.result() == 0x81);
    CHECK(rig.status() == 0x00);
}

TEST_CASE("8271 status across WRITE SPECIAL REGISTER", "[disc][i8271][hardware]") {
    Rig rig;
    rig.write_command(0x3A);
    CHECK(rig.status() == (STATUS_BUSY | STATUS_RESULT_READY));
    rig.write_parameter(0x00);
    CHECK(rig.status() == STATUS_BUSY);
    rig.write_parameter(0x00);
    CHECK(rig.status() == 0x00);
}

TEST_CASE("8271 status after writing the mode register", "[disc][i8271][hardware]") {
    Rig rig;
    rig.issue(0x3A, {0x17, 0xC1});
    CHECK(rig.status() == 0x00);
    CHECK(rig.result() == 0x00);
}

TEST_CASE("8271 READ SPECIAL REGISTER of R7 returns the last parameter", "[disc][i8271][hardware]") {
    Rig rig;
    rig.issue(0x3D, {0x07});
    CHECK(rig.status() == STATUS_RESULT_READY);
    CHECK(rig.result() == 0x07);
}

TEST_CASE("8271 SEEK from cold reports not ready, latched until READ DRIVE STATUS",
          "[disc][i8271][hardware][ready]") {
    Rig rig;
    rig.issue(0x3A, {0x23, 0x00});   // deselect everything
    rig.issue(0x3A, {0x0F, 0x20});   // unload after two index pulses
    rig.issue(0x69, {0x00});         // seek drive 0 to track 0
    REQUIRE(rig.run_until_idle());
    rig.run(10);
    CHECK(rig.status() == (STATUS_RESULT_READY | STATUS_INT));
    CHECK(rig.result() == 0x10);
    CHECK(rig.completion_interrupts == 1);

    rig.issue(0x6C, {});
    uint8_t first = rig.result();
    CHECK((first & 0x02) != 0);      // track 0
    CHECK((first & 0x04) == 0);      // still latched not ready

    rig.issue(0x6C, {});
    CHECK((rig.result() & 0x04) != 0);
}

TEST_CASE("8271 status bits 1 and 0 always read zero", "[disc][i8271]") {
    // Acorn DFS 1.20 decides an 8271 is fitted from these two bits alone.
    Rig rig;
    rig.dfs_init();
    CHECK((rig.status() & 0x03) == 0);
    rig.issue(0x6C, {});
    CHECK((rig.status() & 0x03) == 0);
}

TEST_CASE("8271 parameter count follows the command's low bits", "[disc][i8271]") {
    Rig rig;
    rig.dfs_init();

    SECTION("&3D takes one parameter") {
        rig.write_command(0x3D);
        CHECK((rig.status() & STATUS_BUSY) != 0);
        rig.write_parameter(0x06);
        CHECK((rig.status() & STATUS_BUSY) == 0);
    }
    SECTION("&3A takes two") {
        rig.write_command(0x3A);
        rig.write_parameter(0x06);
        CHECK((rig.status() & STATUS_BUSY) != 0);
        rig.write_parameter(0x00);
        CHECK((rig.status() & STATUS_BUSY) == 0);
    }
    SECTION("&2C takes none") {
        rig.write_command(0x2C);
        CHECK((rig.status() & STATUS_BUSY) == 0);
    }
    SECTION("&3E, one more than a one-parameter opcode, takes two") {
        // The write-up: incrementing a one-parameter command byte makes the
        // chip demand two parameters.
        rig.write_command(0x3E);
        rig.write_parameter(0x06);
        CHECK((rig.status() & STATUS_BUSY) != 0);
        rig.write_parameter(0x00);
        CHECK((rig.status() & STATUS_BUSY) == 0);
    }
}

TEST_CASE("8271 parameters land in R7 downwards", "[disc][i8271]") {
    Rig rig;
    rig.issue(0x3A, {0x11, 0x22});   // writes R17 = 0x22
    CHECK(rig.read_special(0x07) == 0x07);   // this command's own parameter
    CHECK(rig.read_special(0x11) == 0x22);
}

TEST_CASE("8271 reads at offsets 2 and 3 give the byte counter", "[disc][i8271]") {
    Rig rig;
    rig.issue(0x3A, {0x14, 0xAB});   // count MSB
    rig.issue(0x3A, {0x13, 0xCD});   // count LSB
    CHECK(rig.fdc.read_register(2) == 0xAB);
    CHECK(rig.fdc.read_register(3) == 0xCD);
}

TEST_CASE("8271 command written while busy is accepted", "[disc][i8271]") {
    // The chip starts the new command without cancelling the old one's
    // disc activity (The Sentinel relies on this).
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.issue(0x69, {40});           // a long seek
    rig.run(100);
    REQUIRE((rig.status() & STATUS_BUSY) != 0);
    rig.write_command(0x7D);
    CHECK(rig.fdc.internal_register(0x1F) == 0x7D);
    CHECK((rig.status() & STATUS_BUSY) != 0);
}

// =============================================================================
// Seek
// =============================================================================

TEST_CASE("8271 seek steps at the specified rate and updates the track register",
          "[disc][i8271][seek]") {
    Rig rig;
    rig.dfs_init();                  // step rate 3 = 6ms
    REQUIRE(rig.go_ready());
    uint64_t start = rig.ticks;
    rig.issue(0x69, {10});
    REQUIRE(rig.run_until_idle());
    uint64_t elapsed = rig.ticks - start;
    rig.run(2);
    CHECK(rig.result() == 0x00);
    CHECK(rig.drive_0.current_track() == 10);
    CHECK(rig.read_special(0x12) == 10);
    // Ten steps of 6ms, then 16ms of settle with the head already loaded.
    CHECK(elapsed >= 10 * 6 * TICKS_PER_MS + 16 * TICKS_PER_MS);
    CHECK(elapsed < 10 * 6 * TICKS_PER_MS + 16 * TICKS_PER_MS + 1000);
}

TEST_CASE("8271 step rate 0 steps every 3ms (records an assumption)",
          "[disc][i8271][seek][assumption]") {
    Rig rig;
    rig.issue(0x35, {0x0D, 0x00, 0x00, 0xF0});
    rig.issue(0x35, {0x10, 0xFF, 0xFF, 0x00});
    rig.issue(0x3A, {0x17, 0xC1});
    REQUIRE(rig.go_ready());
    uint64_t start = rig.ticks;
    rig.issue(0x69, {5});
    REQUIRE(rig.run_until_idle());
    uint64_t elapsed = rig.ticks - start;
    CHECK(elapsed >= 5 * 3 * TICKS_PER_MS);
    CHECK(elapsed < 5 * 3 * TICKS_PER_MS + 1000);
}

TEST_CASE("8271 bad tracks shift the physical track", "[disc][i8271][seek]") {
    Rig rig;
    rig.dfs_init();
    rig.issue(0x35, {0x10, 0x05, 0xFF, 0x00});   // drive 0 bad track 5
    REQUIRE(rig.go_ready());
    rig.issue(0x69, {10});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.drive_0.current_track() == 11);
    CHECK(rig.read_special(0x12) == 11);
}

TEST_CASE("8271 bad-track registers are zero at power-on", "[disc][i8271][seek]") {
    // Both bad tracks at 0 push every non-zero seek on by two.
    Rig rig;
    rig.issue(0x35, {0x0D, 0x03, 0x08, 0xF0});
    rig.issue(0x3A, {0x17, 0xC1});
    REQUIRE(rig.go_ready());
    rig.issue(0x69, {10});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.drive_0.current_track() == 12);
}

TEST_CASE("8271 seek to track 0 always steps until TRK0", "[disc][i8271][seek]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.drive_0.seek(5);             // the register still says track 0
    rig.issue(0x69, {0});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.drive_0.current_track() == 0);
}

TEST_CASE("8271 head load time applies when the head is unloaded", "[disc][i8271][seek]") {
    Rig rig;
    rig.dfs_init(0xC4);              // head load 4 x 4ms
    rig.issue(0x3A, {0x23, 0x40});   // drive 0 selected, head not loaded
    uint64_t start = rig.ticks;
    rig.issue(0x69, {0});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.ticks - start >= 16 * TICKS_PER_MS);
    CHECK(rig.ticks - start < 16 * TICKS_PER_MS + 100);
}

TEST_CASE("8271 sets LOW HEAD CURRENT from track 43", "[disc][i8271][seek]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.issue(0x69, {42});
    REQUIRE(rig.run_until_idle());
    CHECK((rig.read_special(0x23) & 0x10) == 0);
    rig.issue(0x69, {43});
    REQUIRE(rig.run_until_idle());
    CHECK((rig.read_special(0x23) & 0x10) != 0);
}

// =============================================================================
// Read
// =============================================================================

TEST_CASE("8271 reads a sector with one interrupt per byte", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.issue(0x53, {0, 3, 0x21});   // track 0, sector 3, one 256-byte sector
    REQUIRE(rig.run_until_idle());
    rig.run(2);
    CHECK(rig.result() == 0x00);
    CHECK(rig.completion_interrupts == 1);
    REQUIRE(rig.received.size() == 256);
    for (int i = 0; i < 256; ++i) {
        CHECK(rig.received[static_cast<size_t>(i)] == sector_byte(0, 3, i));
    }
    // Bytes arrive at the FM byte rate.
    for (size_t i = 1; i < rig.data_interrupt_ticks.size(); ++i) {
        CHECK(rig.data_interrupt_ticks[i] - rig.data_interrupt_ticks[i - 1] == TICKS_PER_BYTE);
    }
}

TEST_CASE("8271 reads consecutive sectors", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.issue(0x69, {2});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.result() == 0x00);
    rig.issue(0x53, {2, 8, 0x22});   // sectors 8 and 9
    REQUIRE(rig.run_until_idle());
    rig.run(2);
    CHECK(rig.result() == 0x00);
    REQUIRE(rig.received.size() == 512);
    CHECK(rig.received[0] == sector_byte(2, 8, 0));
    CHECK(rig.received[256] == sector_byte(2, 9, 0));
}

TEST_CASE("8271 status during a read", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.service_data = false;
    rig.issue(0x53, {0, 0, 0x21});
    while (!rig.fdc.interrupt()) rig.tick();
    CHECK(rig.status() == (STATUS_BUSY | STATUS_INT | STATUS_DATA_REQUEST));
}

TEST_CASE("8271 reports late DMA when a byte is not taken in time", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.service_data = false;
    rig.issue(0x53, {0, 0, 0x21});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.result() == 0x0A);
}

TEST_CASE("8271 completion interrupt after late DMA is a fresh edge", "[disc][i8271][read]") {
    // The data request is still asserted when late DMA aborts the command, so
    // the line must drop before the completion interrupt rises.
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.service_data = false;
    rig.issue(0x53, {0, 0, 0x21});
    REQUIRE(rig.run_until_idle());
    rig.run(4);
    // One edge for the unserviced data request, one for the completion.
    CHECK(rig.interrupt_edges == 2);
    CHECK(rig.result() == 0x0A);
}

TEST_CASE("8271 refuses transfers in DMA mode", "[disc][i8271][read]") {
    Rig rig;
    rig.issue(0x35, {0x0D, 0x03, 0x08, 0xF0});
    rig.issue(0x35, {0x10, 0xFF, 0xFF, 0x00});
    rig.issue(0x3A, {0x17, 0xC0});   // DMA mode
    REQUIRE(rig.go_ready());
    rig.issue(0x53, {0, 0, 0x21});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.result() == 0x0A);
    CHECK(rig.received.empty());
}

TEST_CASE("8271 scan fails on a BBC, which has no DMA", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.issue(0x40, {0, 0, 0x21, 0x00, 0x01});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.result() == 0x0A);
}

TEST_CASE("8271 sector not found after three index pulses", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    uint64_t start = rig.ticks;
    rig.issue(0x53, {0, 12, 0x21});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.result() == 0x18);
    CHECK(rig.ticks - start > 2 * TICKS_PER_REV - TICKS_PER_REV / 10);
    CHECK(rig.ticks - start <= 3 * TICKS_PER_REV + TICKS_PER_REV / 10);
}

TEST_CASE("8271 tries the next two tracks when the ID track differs", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    // Physical track 4 carries IDs for track 3, as if the disc had been
    // formatted with track 3 bad; logical track 3 is found one step on.
    std::vector<SectorSpec> specs;
    for (uint8_t s = 0; s < 10; ++s) {
        SectorSpec spec;
        spec.id_track = 3;
        spec.id_sector = s;
        specs.push_back(spec);
    }
    build_track(rig.drive_0.disc()->track(false, 4), 4, specs);
    for (uint8_t s = 0; s < 10; ++s) specs[s].id_track = 99;
    build_track(rig.drive_0.disc()->track(false, 3), 3, specs);
    REQUIRE(rig.go_ready());
    rig.issue(0x53, {3, 0, 0x21});
    REQUIRE(rig.run_until_idle());
    rig.run(2);
    CHECK(rig.result() == 0x00);
    CHECK(rig.drive_0.current_track() == 4);
    REQUIRE(rig.received.size() == 256);
    CHECK(rig.received[0] == sector_byte(4, 0, 0));
}

TEST_CASE("8271 gives up after three mismatched tracks", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    for (int t = 3; t <= 5; ++t) {
        std::vector<SectorSpec> specs;
        for (uint8_t s = 0; s < 10; ++s) {
            SectorSpec spec;
            spec.id_track = 99;
            spec.id_sector = s;
            specs.push_back(spec);
        }
        build_track(rig.drive_0.disc()->track(false, static_cast<uint32_t>(t)), t, specs);
    }
    REQUIRE(rig.go_ready());
    rig.issue(0x53, {3, 0, 0x21});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.result() == 0x18);
}

TEST_CASE("8271 READ DATA skips a deleted sector but flags it (records an assumption)",
          "[disc][i8271][read][assumption]") {
    Rig rig;
    rig.dfs_init();
    std::vector<SectorSpec> specs;
    for (uint8_t s = 0; s < 10; ++s) {
        SectorSpec spec;
        spec.id_sector = s;
        spec.deleted = (s == 1);
        specs.push_back(spec);
    }
    build_track(rig.drive_0.disc()->track(false, 0), 0, specs);
    REQUIRE(rig.go_ready());
    rig.issue(0x53, {0, 0, 0x23});   // sectors 0, 1 (deleted), 2
    REQUIRE(rig.run_until_idle());
    rig.run(2);
    CHECK(rig.result() == 0x20);
    REQUIRE(rig.received.size() == 512);
    CHECK(rig.received[0] == sector_byte(0, 0, 0));
    CHECK(rig.received[256] == sector_byte(0, 2, 0));
}

TEST_CASE("8271 READ DATA AND DELETED transfers a deleted sector", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    std::vector<SectorSpec> specs;
    for (uint8_t s = 0; s < 10; ++s) {
        SectorSpec spec;
        spec.id_sector = s;
        spec.deleted = (s == 1);
        specs.push_back(spec);
    }
    build_track(rig.drive_0.disc()->track(false, 0), 0, specs);
    REQUIRE(rig.go_ready());
    rig.issue(0x57, {0, 1, 0x21});
    REQUIRE(rig.run_until_idle());
    rig.run(2);
    CHECK(rig.result() == 0x20);
    REQUIRE(rig.received.size() == 256);
    CHECK(rig.received[0] == sector_byte(0, 1, 0));
}

TEST_CASE("8271 VERIFY checks without transferring", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.issue(0x5F, {0, 8, 0x22});
    REQUIRE(rig.run_until_idle());
    rig.run(2);
    CHECK(rig.result() == 0x00);
    CHECK(rig.data_interrupts == 0);
    CHECK(rig.completion_interrupts == 1);
}

TEST_CASE("8271 reports read errors", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    std::vector<SectorSpec> specs;
    for (uint8_t s = 0; s < 10; ++s) {
        SectorSpec spec;
        spec.id_sector = s;
        specs.push_back(spec);
    }
    uint8_t expected = 0;
    SECTION("bad data CRC") {
        specs[0].bad_data_crc = true;
        expected = 0x0E;
    }
    SECTION("bad ID CRC") {
        specs[0].bad_id_crc = true;
        expected = 0x0C;
    }
    SECTION("bad data mark is a clock error") {
        specs[0].bad_data_mark = true;
        expected = 0x08;
    }
    build_track(rig.drive_0.disc()->track(false, 0), 0, specs);
    REQUIRE(rig.go_ready());
    rig.issue(0x53, {0, 0, 0x21});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.result() == expected);
}

TEST_CASE("8271 READ ID delivers four bytes per ID", "[disc][i8271][read]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.issue(0x69, {2});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.result() == 0x00);
    rig.issue(0x5B, {2, 0, 3});
    REQUIRE(rig.run_until_idle());
    rig.run(2);
    CHECK(rig.result() == 0x00);
    REQUIRE(rig.received.size() == 12);
    // READ ID waits for the index, so the first ID is the first on the track.
    CHECK(rig.received == std::vector<uint8_t>{2, 0, 0, 1, 2, 0, 1, 1, 2, 0, 2, 1});
}

// =============================================================================
// Write and format
// =============================================================================

TEST_CASE("8271 writes a sector the decoder reads back", "[disc][i8271][write]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    for (int i = 0; i < 256; ++i) rig.to_send.push_back(static_cast<uint8_t>(0xA5 ^ i));
    rig.issue(0x4B, {0, 4, 0x21});
    REQUIRE(rig.run_until_idle());
    rig.run(2);
    CHECK(rig.result() == 0x00);
    CHECK(rig.data_interrupts == 256);

    auto sectors = TrackDecoder(rig.drive_0.disc()->track(false, 0)).find_fm_sectors();
    REQUIRE(sectors.size() == 10);
    const auto& s4 = sectors[4];
    CHECK(s4.sector == 4);
    CHECK_FALSE(s4.is_deleted);
    CHECK_FALSE(s4.has_data_crc_error);
    // Neighbours are intact.
    CHECK_FALSE(sectors[3].has_data_crc_error);
    CHECK_FALSE(sectors[5].has_data_crc_error);

    // And the controller reads back what it wrote.
    rig.to_send.clear();
    rig.received.clear();
    rig.issue(0x53, {0, 4, 0x21});
    REQUIRE(rig.run_until_idle());
    REQUIRE(rig.received.size() == 256);
    for (int i = 0; i < 256; ++i) {
        CHECK(rig.received[static_cast<size_t>(i)] == static_cast<uint8_t>(0xA5 ^ i));
    }
}

TEST_CASE("8271 writes a deleted data mark", "[disc][i8271][write]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.to_send.assign(256, 0x5A);
    rig.issue(0x4F, {0, 2, 0x21});
    REQUIRE(rig.run_until_idle());
    auto sectors = TrackDecoder(rig.drive_0.disc()->track(false, 0)).find_fm_sectors();
    REQUIRE(sectors.size() == 10);
    CHECK(sectors[2].is_deleted);
    CHECK_FALSE(sectors[2].has_data_crc_error);
}

TEST_CASE("8271 refuses to write a protected disc", "[disc][i8271][write]") {
    Rig rig;
    rig.dfs_init();
    rig.drive_0.disc()->set_write_protected(true);
    REQUIRE(rig.go_ready());
    rig.to_send.assign(256, 0x5A);
    rig.issue(0x4B, {0, 2, 0x21});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.result() == 0x12);
    CHECK(rig.data_interrupts == 0);
}

TEST_CASE("8271 formats a track from the IDs the host supplies", "[disc][i8271][format]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.issue(0x69, {10});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.result() == 0x00);
    // Interleaved sector numbers, to show the IDs come from the host.
    const uint8_t order[10] = {0, 3, 6, 9, 2, 5, 8, 1, 4, 7};
    for (uint8_t s : order) {
        rig.to_send.insert(rig.to_send.end(), {10, 0, s, 1});
    }
    rig.issue(0x63, {10, 21, 0x2A, 0, 16});
    REQUIRE(rig.run_until_idle(2'000'000));
    rig.run(2);
    CHECK(rig.result() == 0x00);
    CHECK(rig.data_interrupts == 40);

    auto sectors = TrackDecoder(rig.drive_0.disc()->track(false, 10)).find_fm_sectors();
    REQUIRE(sectors.size() == 10);
    for (size_t i = 0; i < 10; ++i) {
        CHECK(sectors[i].track == 10);
        CHECK(sectors[i].sector == order[i]);
        CHECK(sectors[i].size_code == 1);
        CHECK(sectors[i].has_data_field);
        CHECK_FALSE(sectors[i].has_header_crc_error);
        CHECK_FALSE(sectors[i].has_data_crc_error);
    }

    // The formatted sectors read back as E5 fill.
    rig.to_send.clear();
    rig.received.clear();
    rig.issue(0x53, {10, 6, 0x21});
    REQUIRE(rig.run_until_idle());
    REQUIRE(rig.received.size() == 256);
    CHECK(rig.received[0] == 0xE5);
    CHECK(rig.received[255] == 0xE5);
}

TEST_CASE("8271 format writes the documented gaps", "[disc][i8271][format]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    for (uint8_t s = 0; s < 10; ++s) rig.to_send.insert(rig.to_send.end(), {0, 0, s, 1});
    rig.issue(0x63, {0, 21, 0x2A, 0, 16});
    REQUIRE(rig.run_until_idle(2'000'000));

    // Decode the bytes after the index: GAP1 of 16 x FF, then 6 x 00 and the
    // ID mark; after the ID's CRC, 11 x FF and 6 x 00 before the data mark.
    const DiscTrack& track = rig.drive_0.disc()->track(false, 0);
    auto fm = [&](uint32_t pos) { return ibm_disc_format::fm_from_2us_pulses(track.read_pulses(pos)); };
    uint32_t pos = 0;
    for (; pos < 16; ++pos) CHECK(fm(pos).data == 0xFF);
    for (; pos < 22; ++pos) CHECK(fm(pos).data == 0x00);
    CHECK(fm(pos).clocks == ibm_disc_format::k_mark_clock_pattern);
    CHECK(fm(pos).data == ibm_disc_format::k_id_mark_data_pattern);
    pos += 1 + 4 + 2;
    for (uint32_t end = pos + 11; pos < end; ++pos) CHECK(fm(pos).data == 0xFF);
    for (uint32_t end = pos + 6; pos < end; ++pos) CHECK(fm(pos).data == 0x00);
    CHECK(fm(pos).data == ibm_disc_format::k_data_mark_data_pattern);
    pos += 1 + 256 + 2;
    // GAP3 of 21 x FF, then the next sector's 6 x 00.
    for (uint32_t end = pos + 21; pos < end; ++pos) CHECK(fm(pos).data == 0xFF);
    for (uint32_t end = pos + 6; pos < end; ++pos) CHECK(fm(pos).data == 0x00);
    CHECK(fm(pos).data == ibm_disc_format::k_id_mark_data_pattern);
}

// =============================================================================
// Drive status, ready, unload
// =============================================================================

TEST_CASE("8271 drive status bits", "[disc][i8271][drive]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.issue(0x6C, {});
    uint8_t s = rig.result();
    CHECK((s & 0x81) == 0x81);       // always set
    CHECK((s & 0x02) != 0);          // track 0
    CHECK((s & 0x04) != 0);          // drive 0 ready
    CHECK((s & 0x40) == 0);          // drive 1 not ready
    CHECK((s & 0x08) == 0);          // not write protected

    rig.drive_0.disc()->set_write_protected(true);
    rig.issue(0x6C, {});
    CHECK((rig.result() & 0x08) != 0);
}

TEST_CASE("8271 drive status shows the index pulse", "[disc][i8271][drive]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    int with_index = 0;
    for (int i = 0; i < 100; ++i) {
        rig.issue(0x6C, {});
        if (rig.result() & 0x10) ++with_index;
        rig.run(TICKS_PER_REV / 100);
    }
    // The index is about 4ms in each 200ms revolution.
    CHECK(with_index >= 1);
    CHECK(with_index <= 4);
}

TEST_CASE("8271 an empty drive is never ready (records an assumption)",
          "[disc][i8271][drive][ready][assumption]") {
    // A drive's READY needs a disc turning: with no disc the index sensor
    // never gives an edge. This departs from beebjit, which counts a
    // selected, spinning, empty drive as ready.
    Rig rig(/*with_disc=*/false);
    rig.dfs_init();
    CHECK_FALSE(rig.go_ready());
    rig.issue(0x53, {0, 0, 0x21});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.result() == 0x10);
}

TEST_CASE("8271 unloads the head after the specified index pulses", "[disc][i8271][drive]") {
    Rig rig;
    rig.dfs_init(0x20);              // unload after 2 index pulses
    REQUIRE(rig.go_ready());
    rig.issue(0x69, {0});
    REQUIRE(rig.run_until_idle());
    CHECK(rig.drive_0.motor_on());
    rig.run(3 * TICKS_PER_REV);
    CHECK_FALSE(rig.drive_0.motor_on());
    CHECK((rig.fdc.drive_output() & 0xC8) == 0);
}

TEST_CASE("8271 unload count F never unloads", "[disc][i8271][drive]") {
    Rig rig;
    rig.dfs_init(0xF0);
    REQUIRE(rig.go_ready());
    rig.issue(0x69, {0});
    REQUIRE(rig.run_until_idle());
    rig.run(5 * TICKS_PER_REV);
    CHECK(rig.drive_0.motor_on());
}

TEST_CASE("8271 LOAD HEAD spins both drives but selects one", "[disc][i8271][drive]") {
    // On the shared cable MOTOR ON is common to both drives.
    Rig rig;
    rig.issue(0x3A, {0x23, 0x48});
    CHECK(rig.drive_0.motor_on());
    CHECK(rig.drive_1.motor_on());
    CHECK(rig.drive_0.selected());
    CHECK_FALSE(rig.drive_1.selected());
}

TEST_CASE("8271 side select comes from the drive output", "[disc][i8271][drive]") {
    Rig rig;
    rig.issue(0x3A, {0x23, 0x68});
    CHECK(rig.drive_0.selected_side() == 1);
    CHECK(rig.drive_1.selected_side() == 1);
}

// =============================================================================
// Reset
// =============================================================================

TEST_CASE("8271 Break reset clears status but keeps result and data", "[disc][i8271][reset]") {
    Rig rig;
    rig.dfs_init();
    rig.fdc.write_data(0x77);
    REQUIRE(rig.go_ready());
    uint8_t result_before = rig.fdc.peek_register(1);
    rig.fdc.reset(ResetCause::Break);
    CHECK((rig.status() & ~STATUS_RESULT_READY) == 0);
    CHECK(rig.fdc.peek_register(1) == result_before);
    CHECK(rig.fdc.read_data() == 0x77);
    CHECK_FALSE(rig.drive_0.motor_on());
    CHECK((rig.read_special(0x17) & 0x01) == 0);   // mode cleared with status
    CHECK(rig.read_special(0x10) == 0xFF);         // register file survives
}

TEST_CASE("8271 power-on reset clears the register file", "[disc][i8271][reset]") {
    Rig rig;
    rig.dfs_init();
    rig.fdc.reset(ResetCause::PowerOn);
    CHECK(rig.read_special(0x10) == 0x00);
    CHECK(rig.read_special(0x0D) == 0x00);
}

TEST_CASE("8271 writing 1 to the reset register resets the chip", "[disc][i8271][reset]") {
    Rig rig;
    rig.dfs_init();
    REQUIRE(rig.go_ready());
    rig.issue(0x69, {40});
    rig.run(100);
    REQUIRE((rig.status() & STATUS_BUSY) != 0);
    rig.fdc.write_register(2, 1);
    rig.fdc.write_register(2, 0);
    CHECK((rig.status() & STATUS_BUSY) == 0);
    CHECK_FALSE(rig.drive_0.motor_on());
}
