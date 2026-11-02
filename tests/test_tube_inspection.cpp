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

// Verifies the Step 1b invariant that GetTubeState reports identical Tube
// state whether the socket carries its own in-process TubeUla (the enable()
// path used by tests) or a TubeUla installed as an extension-owned backend
// (the path a coprocessor plugin uses). Both reach the same read-only
// diagnostic surface, TubeInspection, through TubeHostBackend::inspection().

#include <catch2/catch_test_macros.hpp>

#include <beebium/service/DeviceInspectionService.hpp>
#include <beebium/tube/TubeSocket.hpp>
#include <beebium/tube/TubeUla.hpp>

#include "debugger.pb.h"

using namespace beebium;

namespace {

// Drive an identical sequence of host- and coprocessor-side register traffic so
// the two backends end in the same state: control flags, data in every
// register direction, some drained, exercising flags, counters and the trace.
void drive_traffic(TubeSocket& socket, TubeUla& ula) {
    socket.write(0, TubeInspection::FLAG_S | TubeInspection::FLAG_Q |
                    TubeInspection::FLAG_I | TubeInspection::FLAG_V);
    socket.write(1, 0xAA);   // R1 H-to-P latch
    socket.write(3, 0xBB);   // R2 H-to-P latch
    socket.write(5, 0xCC);   // R3 H-to-P
    socket.write(7, 0xDD);   // R4 H-to-P latch

    ula.coprocessor_write(1, 0x11);  // R1 P-to-H FIFO
    ula.coprocessor_write(1, 0x22);
    ula.coprocessor_write(3, 0x33);  // R2 P-to-H
    ula.coprocessor_write(5, 0x44);  // R3 P-to-H
    ula.coprocessor_write(7, 0x55);  // R4 P-to-H

    // Coprocessor drains a couple of host-written registers, host drains one.
    (void)ula.coprocessor_read(1);   // R1 H-to-P
    (void)ula.coprocessor_read(3);   // R2 H-to-P
    (void)socket.read(1);         // R1 P-to-H (host side)
}

}  // namespace

TEST_CASE("GetTubeState: identical output for owned ULA and installed backend",
          "[tube][inspection]") {
    // Owned in-process ULA (the enable() path).
    TubeSocket owned_socket;
    owned_socket.enable();
    TubeUla* owned_ula = owned_socket.tube_ula();
    REQUIRE(owned_ula != nullptr);

    // Extension-installed backend (the coprocessor plugin path). The socket
    // never casts an installed backend to TubeUla.
    TubeSocket installed_socket;
    TubeUla installed_ula;
    installed_socket.install_backend(&installed_ula);
    REQUIRE(installed_socket.tube_ula() == nullptr);

    // Both sockets expose a diagnostic surface.
    REQUIRE(owned_socket.tube_inspection() != nullptr);
    REQUIRE(installed_socket.tube_inspection() != nullptr);

    drive_traffic(owned_socket, *owned_ula);
    drive_traffic(installed_socket, installed_ula);

    TubeState owned_state;
    TubeState installed_state;
    service::fill_tube_state_from_inspection(*owned_socket.tube_inspection(), &owned_state);
    service::fill_tube_state_from_inspection(*installed_socket.tube_inspection(), &installed_state);

    // Field-for-field identical.
    CHECK(owned_state.SerializeAsString() == installed_state.SerializeAsString());

    // And not vacuous: the traffic produced observable state.
    CHECK(owned_state.enabled());
    CHECK(owned_state.control_flags().q());
    CHECK(owned_state.counters().r1_h2p_writes() > 0);
    CHECK(owned_state.trace_total_count() > 0);
}

TEST_CASE("GetTubeState: an empty socket reports disabled with no inspection surface",
          "[tube][inspection]") {
    TubeSocket socket;
    CHECK(socket.tube_inspection() == nullptr);  // EmptyTubeBackend offers none
}

TEST_CASE("TubeInspection: fifo_snapshot reports the whole R1 P-to-H FIFO in order",
          "[tube][inspection]") {
    TubeUla ula;
    ula.reset();

    // Fill the 24-byte P-to-H FIFO from the coprocessor side.
    for (int i = 0; i < 24; ++i) {
        ula.coprocessor_write(1, static_cast<uint8_t>(i));
    }

    uint8_t buf[24] = {};
    size_t count = ula.fifo_snapshot(TubeInspection::Fifo::R1_P2H, buf, sizeof(buf));
    REQUIRE(count == 24);
    for (int i = 0; i < 24; ++i) {
        CHECK(buf[i] == static_cast<uint8_t>(i));  // head-to-tail read order
    }

    // Draining from the head then writing more wraps the ring; the snapshot
    // must still read head-to-tail, not from index 0 of the backing array.
    for (int i = 0; i < 4; ++i) {
        (void)ula.host_read(1);  // drains values 0,1,2,3; head advances to 4
    }
    for (int i = 0; i < 4; ++i) {
        ula.coprocessor_write(1, static_cast<uint8_t>(100 + i));  // wraps the tail
    }
    count = ula.fifo_snapshot(TubeInspection::Fifo::R1_P2H, buf, sizeof(buf));
    REQUIRE(count == 24);
    CHECK(buf[0] == 4);     // new head is the old fifth byte
    CHECK(buf[19] == 23);   // last of the originals
    CHECK(buf[20] == 100);  // first of the wrapped-in bytes
    CHECK(buf[23] == 103);

    // A short buffer takes only what fits but still reports the true count.
    uint8_t small[8] = {};
    count = ula.fifo_snapshot(TubeInspection::Fifo::R1_P2H, small, sizeof(small));
    CHECK(count == 24);
    CHECK(small[0] == 4);
    CHECK(small[7] == 11);
}

TEST_CASE("TubeInspection: fifo_snapshot reports the two-byte R3 registers",
          "[tube][inspection]") {
    TubeUla ula;
    ula.reset();
    ula.host_write(0, TubeInspection::FLAG_S | TubeInspection::FLAG_V);  // two-byte R3

    // Reset seeds R3 P-to-H with one dummy byte (to suppress a spurious PNMI);
    // drain it so the two bytes below are the whole of the register. Draining
    // then refilling also leaves the ring head wrapped, so the snapshot must
    // read head-to-tail rather than from index 0.
    (void)ula.host_read(5);

    ula.coprocessor_write(5, 0xAA);
    ula.coprocessor_write(5, 0xBB);

    uint8_t buf[2] = {};
    size_t count = ula.fifo_snapshot(TubeInspection::Fifo::R3_P2H, buf, sizeof(buf));
    REQUIRE(count == 2);
    CHECK(buf[0] == 0xAA);
    CHECK(buf[1] == 0xBB);

    // The other direction is empty.
    CHECK(ula.fifo_snapshot(TubeInspection::Fifo::R3_H2P, buf, sizeof(buf)) == 0);
}
