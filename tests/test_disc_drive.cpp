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

#include <beebium/disc/DiscDrive.hpp>
#include <beebium/disc/formats/SsdFormatHandler.hpp>
#include <beebium/disc/TrackBuilder.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

using namespace beebium;
using namespace beebium::ibm_disc_format;

// Helper: load a small SSD image into a Disc.
static std::unique_ptr<Disc> make_test_disc() {
    SsdFormatHandler handler;
    std::vector<uint8_t> data(80 * 10 * 256, 0);
    // Put known data in track 0 sector 0
    for (int i = 0; i < 256; ++i) {
        data[i] = static_cast<uint8_t>(i);
    }
    auto result = handler.load(data, "/tmp/test.ssd");
    return std::move(result.disc);
}

// =============================================================================
// Construction and State
// =============================================================================

TEST_CASE("DiscDrive default construction is empty", "[disc][pulsedrive]") {
    DiscDrive drive;
    CHECK(drive.state() == DriveState::Empty);
    CHECK_FALSE(drive.has_disc());
    CHECK(drive.current_track() == 0);
    CHECK_FALSE(drive.motor_on());
}

// =============================================================================
// Disc Insertion / Ejection
// =============================================================================

TEST_CASE("DiscDrive insert transitions to Loaded", "[disc][pulsedrive]") {
    DiscDrive drive;
    drive.insert(make_test_disc());

    CHECK(drive.state() == DriveState::Loaded);
    CHECK(drive.has_disc());
    CHECK(drive.disc() != nullptr);
}

TEST_CASE("DiscDrive eject_immediate returns disc", "[disc][pulsedrive]") {
    DiscDrive drive;
    drive.insert(make_test_disc());

    auto disc = drive.eject_immediate();
    CHECK(disc != nullptr);
    CHECK(drive.state() == DriveState::Empty);
    CHECK_FALSE(drive.has_disc());
}

// =============================================================================
// Head Positioning
// =============================================================================

TEST_CASE("DiscDrive step_in and step_out", "[disc][pulsedrive]") {
    DiscDrive drive;
    CHECK(drive.current_track() == 0);
    CHECK(drive.at_track_0());

    drive.step_in();
    CHECK(drive.current_track() == 1);
    CHECK_FALSE(drive.at_track_0());

    drive.step_out();
    CHECK(drive.current_track() == 0);
    CHECK(drive.at_track_0());
}

TEST_CASE("DiscDrive step_out at track 0 stays at 0", "[disc][pulsedrive]") {
    DiscDrive drive;
    drive.step_out();
    CHECK(drive.current_track() == 0);
}

TEST_CASE("DiscDrive step_in clamped at MAX_TRACK", "[disc][pulsedrive]") {
    DiscDrive drive;
    for (int i = 0; i < 100; ++i) drive.step_in();
    CHECK(drive.current_track() == DiscDrive::MAX_TRACK);
}

TEST_CASE("DiscDrive seek", "[disc][pulsedrive]") {
    DiscDrive drive;
    drive.seek(40);
    CHECK(drive.current_track() == 40);

    drive.seek(0);
    CHECK(drive.at_track_0());
}

// =============================================================================
// Motor Control
// =============================================================================

TEST_CASE("DiscDrive motor control", "[disc][pulsedrive]") {
    DiscDrive drive;
    CHECK_FALSE(drive.motor_on());

    drive.spin_up();
    CHECK(drive.motor_on());

    drive.spin_down();
    CHECK_FALSE(drive.motor_on());
}

// =============================================================================
// Pulse-Level Access
// =============================================================================

TEST_CASE("DiscDrive read_pulses returns data from disc", "[disc][pulsedrive][pulses]") {
    DiscDrive drive;
    drive.insert(make_test_disc());
    drive.set_side(0);

    // Track 0 should have non-zero pulse data (FM-encoded DFS sectors)
    uint32_t pulses = drive.read_pulses();
    // After SSD load, first pulse word should be FM-encoded 0xFF (gap bytes)
    CHECK(pulses != 0);
}

TEST_CASE("DiscDrive read_pulses returns quasi-random with no disc", "[disc][pulsedrive][pulses]") {
    DiscDrive drive;
    drive.set_side(0);

    // With no disc, should still return something (quasi-random pulses)
    uint32_t p1 = drive.read_pulses();
    // Just verify it doesn't crash
    (void)p1;
}

TEST_CASE("DiscDrive advance_head increments position", "[disc][pulsedrive][pulses]") {
    DiscDrive drive;
    drive.insert(make_test_disc());
    drive.set_side(0);

    CHECK(drive.head_position() == 0);

    bool index = drive.advance_head();
    CHECK(drive.head_position() == 1);
    CHECK_FALSE(index);  // Not at index yet
}

TEST_CASE("DiscDrive advance_head wraps at track end", "[disc][pulsedrive][pulses]") {
    DiscDrive drive;
    drive.insert(make_test_disc());
    drive.set_side(0);

    // Advance to just before the end
    uint32_t track_len = drive.track_length();
    REQUIRE(track_len > 0);

    for (uint32_t i = 0; i < track_len - 1; ++i) {
        bool index = drive.advance_head();
        CHECK_FALSE(index);
    }

    // This advance should wrap and trigger index
    bool index = drive.advance_head();
    CHECK(index);
    CHECK(drive.head_position() == 0);
}

TEST_CASE("DiscDrive advance_head_half increments by half-word", "[disc][pulsedrive][pulses]") {
    DiscDrive drive;
    drive.insert(make_test_disc());
    drive.set_side(0);

    CHECK(drive.head_position() == 0);
    CHECK(drive.pulse_sub_position() == 0);

    bool index = drive.advance_head_half();
    CHECK_FALSE(index);
    CHECK(drive.head_position() == 0);
    CHECK(drive.pulse_sub_position() == 16);

    index = drive.advance_head_half();
    CHECK_FALSE(index);
    CHECK(drive.head_position() == 1);
    CHECK(drive.pulse_sub_position() == 0);
}

// =============================================================================
// Write Pulses
// =============================================================================

TEST_CASE("DiscDrive write_pulses modifies track", "[disc][pulsedrive][pulses]") {
    DiscDrive drive;
    drive.insert(make_test_disc());
    drive.set_side(0);

    drive.write_pulses(0xDEADBEEF);
    CHECK(drive.read_pulses() == 0xDEADBEEF);
}

TEST_CASE("DiscDrive write marks disc dirty", "[disc][pulsedrive][pulses]") {
    DiscDrive drive;
    auto disc = make_test_disc();
    auto* disc_ptr = disc.get();
    drive.insert(std::move(disc));
    drive.set_side(0);

    CHECK_FALSE(disc_ptr->has_dirty_tracks());
    drive.write_pulses(0x12345678);
    CHECK(disc_ptr->has_dirty_tracks());
}

TEST_CASE("DiscDrive flush_if_dirty writes back every dirty track",
          "[disc][pulsedrive][flush]") {
    // flush_if_dirty() is the primitive used by the step-away path, the
    // controller's write-inactivity timer, and the server shutdown backstop.
    DiscDrive drive;
    auto disc = make_test_disc();
    auto* disc_ptr = disc.get();

    std::vector<uint32_t> flushed_tracks;
    disc_ptr->set_write_track_callback(
        [&](Disc&, bool /*upper_side*/, uint32_t track_number) {
            flushed_tracks.push_back(track_number);
        });

    drive.insert(std::move(disc));
    drive.set_side(0);
    drive.seek(7);
    drive.write_pulses(0x12345678);  // dirties track 7
    REQUIRE(disc_ptr->has_dirty_tracks());

    drive.flush_if_dirty();

    CHECK_FALSE(disc_ptr->has_dirty_tracks());
    REQUIRE(flushed_tracks.size() == 1);
    CHECK(flushed_tracks[0] == 7);

    // A second flush with nothing dirty is a no-op.
    drive.flush_if_dirty();
    CHECK(flushed_tracks.size() == 1);
}

TEST_CASE("DiscDrive flushes a dirty track when the head steps away",
          "[disc][pulsedrive][flush]") {
    // Sustained multi-track writes must persist continuously: a track that has
    // been written should be flushed as soon as the head steps to another
    // track, not held in memory until the motor eventually spins down.
    DiscDrive drive;
    auto disc = make_test_disc();
    auto* disc_ptr = disc.get();

    std::vector<uint32_t> flushed_tracks;
    disc_ptr->set_write_track_callback(
        [&](Disc&, bool, uint32_t track_number) {
            flushed_tracks.push_back(track_number);
        });

    drive.insert(std::move(disc));
    drive.set_side(0);
    drive.set_motor(true);

    drive.seek(5);
    drive.write_pulses(0xCAFEF00D);  // dirties track 5
    CHECK(flushed_tracks.empty());

    drive.step_in();  // leave track 5 -> must flush it now
    REQUIRE(flushed_tracks.size() == 1);
    CHECK(flushed_tracks[0] == 5);
    CHECK_FALSE(disc_ptr->has_dirty_tracks());

    // Seeking to the same track must not trigger a redundant flush.
    drive.write_pulses(0x0BADF00D);  // dirties track 6 (current)
    flushed_tracks.clear();
    drive.seek(drive.current_track());
    CHECK(flushed_tracks.empty());
}

// =============================================================================
// Side Selection
// =============================================================================

TEST_CASE("DiscDrive side selection affects which surface is read", "[disc][pulsedrive][side]") {
    DiscDrive drive;

    SsdFormatHandler handler;
    // Make a DSD (double-sided) image with distinct data on each side
    std::vector<uint8_t> data(80 * 2 * 10 * 256, 0);
    // Fill side 0 track 0 sector 0 with 0xAA, side 1 track 0 sector 0 with 0x55
    for (int i = 0; i < 256; ++i) {
        data[i] = 0xAA;              // Side 0, track 0, sector 0
        data[10 * 256 + i] = 0x55;   // Side 1, track 0, sector 0
    }
    auto result = handler.load(data, "/tmp/test.dsd");
    REQUIRE(result.success());

    drive.insert(std::move(result.disc));

    // Advance past GAP1 + sync (22 positions) + ID field (7) + GAP2 (17) + data mark (1) = 47
    // to reach sector 0 data where the sides differ
    auto advance_to = [&](uint32_t pos) {
        drive.seek(0);
        // Reset head position by ejecting and reinserting... simpler: just advance
        for (uint32_t i = 0; i < pos; ++i) {
            drive.advance_head();
        }
    };

    // The sector data starts at GAP1(16) + sync(6) + IDmark(1) + ID(4) + CRC(2) + GAP2_FF(11)
    // + sync(6) + datamark(1) = 47 positions from start
    uint32_t data_start = 16 + 6 + 1 + 4 + 2 + 11 + 6 + 1;

    drive.set_side(0);
    advance_to(data_start);
    uint32_t p0 = drive.read_pulses();

    // Reset position for side 1
    // Eject and reinsert to reset head position (simpler than tracking)
    auto disc = drive.eject_immediate();
    drive.insert(std::move(disc));

    drive.set_side(1);
    advance_to(data_start);
    uint32_t p1 = drive.read_pulses();

    // The sector data is different (0xAA vs 0x55), so pulses should differ
    CHECK(p0 != p1);
}

// =============================================================================
// Write Protection
// =============================================================================

TEST_CASE("DiscDrive is_write_protected delegates to disc", "[disc][pulsedrive]") {
    DiscDrive drive;
    auto disc = make_test_disc();
    disc->set_write_protected(true);
    drive.insert(std::move(disc));

    CHECK(drive.is_write_protected());
}

TEST_CASE("DiscDrive is_write_protected false with no disc", "[disc][pulsedrive]") {
    DiscDrive drive;
    CHECK_FALSE(drive.is_write_protected());
}

// =============================================================================
// Safe eject and change reporting
// =============================================================================

namespace {

// Collects the types a drive reports, in order.
std::vector<DiscDriveEventType> record_events(DiscDrive& drive,
                                              std::vector<DiscDriveEventType>& into) {
    drive.set_observer([&into](const DiscDriveEvent& event) {
        into.push_back(event.type);
    });
    return into;
}

} // namespace

TEST_CASE("DiscDrive reports an insert as it happens", "[disc][pulsedrive][events]") {
    DiscDrive drive;
    std::vector<DiscDriveEventType> seen;
    record_events(drive, seen);

    drive.insert(make_test_disc(), "file:///discs/elite.ssd");

    REQUIRE(seen.size() == 1);
    CHECK(seen[0] == DiscDriveEventType::Inserted);
}

TEST_CASE("DiscDrive insert event carries the disc it is announcing",
          "[disc][pulsedrive][events]") {
    // The observer runs while the drive is mid-change and may be on another
    // thread, so the event has to describe the disc itself rather than leave
    // the observer to go and look.
    DiscDrive drive;
    DiscDriveEvent captured{DiscDriveEventType::MotorOff};
    drive.set_observer([&captured](const DiscDriveEvent& event) { captured = event; });

    drive.insert(make_test_disc(), "file:///discs/elite.ssd");

    CHECK(captured.type == DiscDriveEventType::Inserted);
    CHECK(captured.source_url == "file:///discs/elite.ssd");
    CHECK_FALSE(captured.format.empty());
}

TEST_CASE("DiscDrive reports motor transitions", "[disc][pulsedrive][events]") {
    DiscDrive drive;
    drive.insert(make_test_disc());

    std::vector<DiscDriveEventType> seen;
    record_events(drive, seen);

    drive.spin_up();
    drive.spin_up();  // no change, no event
    drive.spin_down();

    REQUIRE(seen.size() == 2);
    CHECK(seen[0] == DiscDriveEventType::MotorOn);
    CHECK(seen[1] == DiscDriveEventType::MotorOff);
}

TEST_CASE("DiscDrive safe eject waits for the motor to stop",
          "[disc][pulsedrive][eject]") {
    DiscDrive drive;
    drive.insert(make_test_disc());
    drive.spin_up();

    EjectOptions opts;
    opts.quiescence_duration = std::chrono::milliseconds(0);

    REQUIRE(drive.request_eject(opts));
    CHECK(drive.state() == DriveState::Ejecting);

    // Motor still running: the disc stays put however often it is ticked.
    for (int i = 0; i < 5; ++i) {
        CHECK(drive.tick_eject() == nullptr);
        CHECK(drive.state() == DriveState::Ejecting);
    }

    drive.spin_down();
    CHECK(drive.tick_eject() != nullptr);
    CHECK(drive.state() == DriveState::Empty);
}

TEST_CASE("DiscDrive never forces an eject on its own",
          "[disc][pulsedrive][eject]") {
    // Giving up on waiting is the caller's decision. A drive that decided for
    // itself would pull a disc out from under a running machine, and would do
    // it silently.
    DiscDrive drive;
    drive.insert(make_test_disc());
    drive.spin_up();

    REQUIRE(drive.request_eject());

    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(300)) {
        CHECK(drive.tick_eject() == nullptr);
    }
    CHECK(drive.state() == DriveState::Ejecting);

    // The caller forcing it is what ends the wait.
    CHECK(drive.eject_immediate() != nullptr);
    CHECK(drive.state() == DriveState::Empty);
}

TEST_CASE("DiscDrive reports a forced eject as forced", "[disc][pulsedrive][eject]") {
    DiscDrive drive;
    drive.insert(make_test_disc());
    drive.spin_up();

    std::vector<DiscDriveEventType> seen;
    record_events(drive, seen);

    drive.eject_immediate();

    REQUIRE(seen.size() == 1);
    CHECK(seen[0] == DiscDriveEventType::ForceEjected);
    CHECK(drive.was_forced_eject());
}

TEST_CASE("DiscDrive reports a quiescent eject as graceful",
          "[disc][pulsedrive][eject]") {
    DiscDrive drive;
    drive.insert(make_test_disc());

    std::vector<DiscDriveEventType> seen;
    record_events(drive, seen);

    EjectOptions opts;
    opts.quiescence_duration = std::chrono::milliseconds(0);
    REQUIRE(drive.request_eject(opts));
    REQUIRE(drive.tick_eject() != nullptr);

    REQUIRE(seen.size() == 2);
    CHECK(seen[0] == DiscDriveEventType::EjectRequested);
    CHECK(seen[1] == DiscDriveEventType::Ejected);
    CHECK_FALSE(drive.was_forced_eject());
}

TEST_CASE("DiscDrive cancel_eject returns the disc to rest",
          "[disc][pulsedrive][eject]") {
    DiscDrive drive;
    drive.insert(make_test_disc());
    drive.spin_up();
    REQUIRE(drive.request_eject());

    std::vector<DiscDriveEventType> seen;
    record_events(drive, seen);

    CHECK(drive.cancel_eject());
    CHECK(drive.state() == DriveState::Loaded);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0] == DiscDriveEventType::EjectCancelled);

    // Nothing to cancel the second time.
    CHECK_FALSE(drive.cancel_eject());
}

TEST_CASE("DiscDrive cancel_eject does nothing to a drive at rest",
          "[disc][pulsedrive][eject]") {
    DiscDrive drive;
    drive.insert(make_test_disc());

    CHECK_FALSE(drive.cancel_eject());
    CHECK(drive.state() == DriveState::Loaded);
}
