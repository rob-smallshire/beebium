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

#include <beebium/tube/TubeUla.hpp>

#include <vector>

using namespace beebium;

//////////////////////////////////////////////////////////////////////////////
// Register 1: H-to-P latch, P-to-H 24-byte FIFO
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("R1 host-to-coprocessor latch", "[tube][fifo][r1]") {
    TubeUla tube;

    SECTION("Initial state: not full on host side, empty on coprocessor side") {
        uint8_t host_stat = tube.host_read(0);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) != 0);  // host can write

        uint8_t para_stat = tube.coprocessor_read(0);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);  // no data for coprocessor
    }

    SECTION("Host write followed by coprocessor read returns same byte") {
        tube.host_write(1, 0xAB);
        REQUIRE(tube.coprocessor_read(1) == 0xAB);
    }

    SECTION("Host write sets coprocessor data-available flag") {
        tube.host_write(1, 0x42);
        uint8_t para_stat = tube.coprocessor_read(0);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) != 0);
    }

    SECTION("Coprocessor read clears data-available flag") {
        tube.host_write(1, 0x42);
        tube.coprocessor_read(1);  // consume
        uint8_t para_stat = tube.coprocessor_read(0);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);
    }

    SECTION("Host write sets host not-full (cannot write again)") {
        tube.host_write(1, 0x42);
        uint8_t host_stat = tube.host_read(0);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) == 0);  // host cannot write
    }

    SECTION("Coprocessor read restores host not-full flag") {
        tube.host_write(1, 0x42);
        tube.coprocessor_read(1);
        uint8_t host_stat = tube.host_read(0);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) != 0);  // host can write again
    }

    // A host write to a full latch overwrites it and never stalls; see the
    // "Writes to a full register" cases below.
}

TEST_CASE("R1 coprocessor-to-host 24-byte FIFO", "[tube][fifo][r1]") {
    TubeUla tube;

    SECTION("Initial state: empty on host side, not full on coprocessor side") {
        uint8_t host_stat = tube.host_read(0);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) == 0);

        uint8_t para_stat = tube.coprocessor_read(0);
        REQUIRE((para_stat & TubeUla::SPACE_AVAILABLE) != 0);
    }

    SECTION("Single byte round-trip: coprocessor write, host read") {
        tube.coprocessor_write(1, 0xCD);
        REQUIRE(tube.host_read(1) == 0xCD);
    }

    SECTION("FIFO ordering: 3 bytes written are read in same order") {
        tube.coprocessor_write(1, 0x11);
        tube.coprocessor_write(1, 0x22);
        tube.coprocessor_write(1, 0x33);
        REQUIRE(tube.host_read(1) == 0x11);
        REQUIRE(tube.host_read(1) == 0x22);
        REQUIRE(tube.host_read(1) == 0x33);
    }

    SECTION("Fill to 24 bytes: not-full flag clears on 24th write") {
        for (int i = 0; i < 23; i++) {
            tube.coprocessor_write(1, static_cast<uint8_t>(i));
            uint8_t stat = tube.coprocessor_read(0);
            REQUIRE((stat & TubeUla::SPACE_AVAILABLE) != 0);
        }
        tube.coprocessor_write(1, 23);
        uint8_t stat = tube.coprocessor_read(0);
        REQUIRE((stat & TubeUla::SPACE_AVAILABLE) == 0);  // now full
    }

    SECTION("Host data-available set after first coprocessor write") {
        tube.coprocessor_write(1, 0x42);
        uint8_t host_stat = tube.host_read(0);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) != 0);
    }

    SECTION("Host data-available clears when FIFO drained") {
        tube.coprocessor_write(1, 0x42);
        tube.host_read(1);  // drain
        uint8_t host_stat = tube.host_read(0);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) == 0);
    }

    SECTION("Write when full: byte is dropped, FIFO contents unchanged") {
        for (int i = 0; i < 24; i++)
            tube.coprocessor_write(1, static_cast<uint8_t>(i));

        tube.coprocessor_write(1, 0xFF);  // should be dropped

        for (int i = 0; i < 24; i++)
            REQUIRE(tube.host_read(1) == static_cast<uint8_t>(i));
    }

    SECTION("Read when empty returns the coprocessor's data bus latch") {
        // Nothing driven since reset: the latch is clear.
        REQUIRE(tube.host_read(1) == 0);

        // A coprocessor write to any address, even the write-ignored status
        // address, leaves its value on the coprocessor side's data bus.
        tube.coprocessor_write(0, 0x55);
        REQUIRE(tube.host_read(1) == 0x55);

        // Data that went through the FIFO is returned first; once drained,
        // the empty read shows the last byte the coprocessor drove.
        tube.coprocessor_write(1, 0xAA);
        REQUIRE(tube.host_read(1) == 0xAA);
        REQUIRE(tube.host_read(1) == 0xAA);
    }

    SECTION("Drain: each host read advances FIFO, empty flag sets on last") {
        for (int i = 0; i < 5; i++)
            tube.coprocessor_write(1, static_cast<uint8_t>(i + 1));

        for (int i = 0; i < 4; i++) {
            REQUIRE(tube.host_read(1) == static_cast<uint8_t>(i + 1));
            uint8_t stat = tube.host_read(0);
            REQUIRE((stat & TubeUla::DATA_AVAILABLE) != 0);  // still has data
        }
        REQUIRE(tube.host_read(1) == 5);
        uint8_t stat = tube.host_read(0);
        REQUIRE((stat & TubeUla::DATA_AVAILABLE) == 0);  // now empty
    }

    SECTION("Fill and drain cycle: repeatable without state leakage") {
        for (int cycle = 0; cycle < 3; cycle++) {
            for (int i = 0; i < 24; i++)
                tube.coprocessor_write(1, static_cast<uint8_t>(i + cycle));
            for (int i = 0; i < 24; i++)
                REQUIRE(tube.host_read(1) == static_cast<uint8_t>(i + cycle));
        }
    }

    SECTION("Partial fill/drain interleaving") {
        tube.coprocessor_write(1, 0xA0);
        tube.coprocessor_write(1, 0xA1);
        REQUIRE(tube.host_read(1) == 0xA0);

        tube.coprocessor_write(1, 0xA2);
        REQUIRE(tube.host_read(1) == 0xA1);
        REQUIRE(tube.host_read(1) == 0xA2);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Register 2: 1-byte latches in each direction
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("R2 single-byte latches", "[tube][fifo][r2]") {
    TubeUla tube;

    SECTION("Host-to-coprocessor: write then read") {
        tube.host_write(3, 0xDE);
        REQUIRE(tube.coprocessor_read(3) == 0xDE);
    }

    SECTION("Coprocessor-to-host: write then read") {
        tube.coprocessor_write(3, 0xEF);
        REQUIRE(tube.host_read(3) == 0xEF);
    }

    SECTION("Overwrite before read replaces value") {
        tube.host_write(3, 0xAA);
        tube.host_write(3, 0xBB);
        REQUIRE(tube.coprocessor_read(3) == 0xBB);
    }

    SECTION("Status flags: H-to-P") {
        // Initially: no data for coprocessor, space for host.
        uint8_t para_stat = tube.coprocessor_read(2);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);
        uint8_t host_stat = tube.host_read(2);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) != 0);

        // After host write: data available for coprocessor, host full.
        tube.host_write(3, 0x42);
        para_stat = tube.coprocessor_read(2);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) != 0);
        host_stat = tube.host_read(2);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) == 0);

        // After coprocessor read: back to initial.
        tube.coprocessor_read(3);
        para_stat = tube.coprocessor_read(2);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);
        host_stat = tube.host_read(2);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) != 0);
    }

    SECTION("Status flags: P-to-H") {
        // Initially: no data for host, space for coprocessor.
        uint8_t host_stat = tube.host_read(2);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) == 0);
        uint8_t para_stat = tube.coprocessor_read(2);
        REQUIRE((para_stat & TubeUla::SPACE_AVAILABLE) != 0);

        // After coprocessor write: data available for host, coprocessor full.
        tube.coprocessor_write(3, 0x42);
        host_stat = tube.host_read(2);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) != 0);
        para_stat = tube.coprocessor_read(2);
        REQUIRE((para_stat & TubeUla::SPACE_AVAILABLE) == 0);

        // After host read: back to initial.
        tube.host_read(3);
        host_stat = tube.host_read(2);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) == 0);
        para_stat = tube.coprocessor_read(2);
        REQUIRE((para_stat & TubeUla::SPACE_AVAILABLE) != 0);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Register 3: 2-byte FIFO, configurable as 1 or 2-byte mode
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("R3 one-byte mode", "[tube][fifo][r3]") {
    TubeUla tube;

    // V flag defaults to 0 after reset (one-byte mode).

    SECTION("Host-to-coprocessor: single byte") {
        tube.host_write(5, 0x77);

        // Status: data available to coprocessor.
        uint8_t para_stat = tube.coprocessor_read(4);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) != 0);

        REQUIRE(tube.coprocessor_read(5) == 0x77);

        // After read: data not available.
        para_stat = tube.coprocessor_read(4);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);
    }

    SECTION("Coprocessor-to-host: single byte") {
        // After reset, R3 P-to-H has 1 dummy byte. Read it off first.
        tube.host_read(5);

        tube.coprocessor_write(5, 0x88);

        uint8_t host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) != 0);

        REQUIRE(tube.host_read(5) == 0x88);
    }

    SECTION("One byte fills the register in one-byte mode") {
        tube.host_write(5, 0x42);
        uint8_t host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) == 0);  // full
    }

    SECTION("Reading one byte empties the register in one-byte mode") {
        tube.host_write(5, 0x42);
        tube.coprocessor_read(5);  // consume
        uint8_t host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) != 0);  // space again
    }
}

TEST_CASE("R3 two-byte mode", "[tube][fifo][r3]") {
    TubeUla tube;

    // Enable two-byte mode: set V flag.
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_V);

    SECTION("Host-to-coprocessor: two bytes required before data-available set") {
        tube.host_write(5, 0xAA);
        uint8_t para_stat = tube.coprocessor_read(4);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);  // not yet

        tube.host_write(5, 0xBB);
        para_stat = tube.coprocessor_read(4);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) != 0);  // now available
    }

    SECTION("Coprocessor reads both bytes in order") {
        tube.host_write(5, 0xAA);
        tube.host_write(5, 0xBB);
        REQUIRE(tube.coprocessor_read(5) == 0xAA);
        REQUIRE(tube.coprocessor_read(5) == 0xBB);
    }

    SECTION("Not-full only after both bytes read") {
        tube.host_write(5, 0xAA);
        tube.host_write(5, 0xBB);
        uint8_t host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) == 0);  // full

        tube.coprocessor_read(5);  // read first byte
        host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) == 0);  // still full

        tube.coprocessor_read(5);  // read second byte
        host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) != 0);  // now space
    }

    SECTION("Coprocessor-to-host: two bytes required") {
        // Drain the reset dummy byte first.
        tube.host_read(5);

        tube.coprocessor_write(5, 0xCC);
        uint8_t host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) == 0);

        tube.coprocessor_write(5, 0xDD);
        host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) != 0);

        REQUIRE(tube.host_read(5) == 0xCC);
        REQUIRE(tube.host_read(5) == 0xDD);
    }

    SECTION("Space available between bytes of H-to-P two-byte transfer") {
        tube.host_write(5, 0xAA);  // first byte
        uint8_t host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) != 0);  // can still write second byte

        tube.host_write(5, 0xBB);  // second byte
        host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::SPACE_AVAILABLE) == 0);  // now full
    }

    SECTION("Space available between bytes of P-to-H two-byte transfer") {
        // Drain the reset dummy byte.
        tube.host_read(5);

        tube.coprocessor_write(5, 0xCC);  // first byte
        uint8_t para_stat = tube.coprocessor_read(4);
        REQUIRE((para_stat & TubeUla::SPACE_AVAILABLE) != 0);  // can still write second byte

        tube.coprocessor_write(5, 0xDD);  // second byte
        para_stat = tube.coprocessor_read(4);
        REQUIRE((para_stat & TubeUla::SPACE_AVAILABLE) == 0);  // now full
    }

    SECTION("Data available persists while draining two-byte H-to-P transfer") {
        tube.host_write(5, 0xAA);
        tube.host_write(5, 0xBB);

        // Data available after first coprocessor read (one byte still to go).
        tube.coprocessor_read(5);
        uint8_t para_stat = tube.coprocessor_read(4);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) != 0);

        // Data not available after second read.
        tube.coprocessor_read(5);
        para_stat = tube.coprocessor_read(4);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);
    }

    SECTION("Data available persists while draining two-byte P-to-H transfer") {
        // Drain dummy byte.
        tube.host_read(5);

        tube.coprocessor_write(5, 0xCC);
        tube.coprocessor_write(5, 0xDD);

        // Data available after first host read.
        tube.host_read(5);
        uint8_t host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) != 0);

        // Data not available after second read.
        tube.host_read(5);
        host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) == 0);
    }

    SECTION("Switch from two-byte to one-byte mode mid-transfer") {
        tube.host_write(5, 0xAA);  // first byte, no data-available yet

        // Clear V flag (switch to one-byte mode).
        tube.host_write(0, TubeUla::FLAG_V);  // S=0, clear V

        // In one-byte mode, one byte is enough for data-available.
        uint8_t para_stat = tube.coprocessor_read(4);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) != 0);
        REQUIRE(tube.coprocessor_read(5) == 0xAA);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Register 4: 1-byte latches in each direction
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("R4 single-byte latches", "[tube][fifo][r4]") {
    TubeUla tube;

    SECTION("Host-to-coprocessor: write then read") {
        tube.host_write(7, 0x99);
        REQUIRE(tube.coprocessor_read(7) == 0x99);
    }

    SECTION("Coprocessor-to-host: write then read") {
        tube.coprocessor_write(7, 0x66);
        REQUIRE(tube.host_read(7) == 0x66);
    }

    SECTION("Status flags: H-to-P") {
        // No data initially.
        uint8_t para_stat = tube.coprocessor_read(6);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);

        tube.host_write(7, 0x42);
        para_stat = tube.coprocessor_read(6);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) != 0);

        tube.coprocessor_read(7);
        para_stat = tube.coprocessor_read(6);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);
    }

    SECTION("Status flags: P-to-H") {
        uint8_t host_stat = tube.host_read(6);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) == 0);

        tube.coprocessor_write(7, 0x42);
        host_stat = tube.host_read(6);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) != 0);

        tube.host_read(7);
        host_stat = tube.host_read(6);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) == 0);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Control flags
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Control register flag manipulation", "[tube][flags]") {
    TubeUla tube;

    SECTION("S=1 sets bits specified by D0-D5") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_Q) != 0);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_I) != 0);
    }

    SECTION("S=0 clears bits specified by D0-D5") {
        // First set Q and I.
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I);

        // Then clear just Q.
        tube.host_write(0, TubeUla::FLAG_Q);  // S=0
        REQUIRE((tube.control_flags() & TubeUla::FLAG_Q) == 0);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_I) != 0);  // unchanged
    }

    SECTION("Individual flag set/clear for each flag") {
        uint8_t flags[] = {
            TubeUla::FLAG_Q, TubeUla::FLAG_I, TubeUla::FLAG_J,
            TubeUla::FLAG_M, TubeUla::FLAG_V, TubeUla::FLAG_P
        };
        for (auto flag : flags) {
            tube.reset();
            tube.host_write(0, TubeUla::FLAG_S | flag);
            REQUIRE((tube.control_flags() & flag) != 0);

            tube.host_write(0, flag);  // S=0, clear
            REQUIRE((tube.control_flags() & flag) == 0);
        }
    }

    SECTION("Multiple flags set in one write") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_M | TubeUla::FLAG_V);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_Q) != 0);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_M) != 0);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_V) != 0);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_I) == 0);
    }

    SECTION("Multiple flags cleared in one write") {
        tube.host_write(0, TubeUla::FLAG_S | 0x3F);  // set all
        tube.host_write(0, TubeUla::FLAG_Q | TubeUla::FLAG_J);  // S=0, clear Q and J
        REQUIRE((tube.control_flags() & TubeUla::FLAG_Q) == 0);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_J) == 0);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_I) != 0);  // unchanged
    }

    SECTION("Control flags readable from status register") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_M);
        uint8_t host_stat = tube.host_read(0);
        REQUIRE((host_stat & TubeUla::FLAG_Q) != 0);
        REQUIRE((host_stat & TubeUla::FLAG_M) != 0);

        uint8_t para_stat = tube.coprocessor_read(0);
        REQUIRE((para_stat & TubeUla::FLAG_Q) != 0);
        REQUIRE((para_stat & TubeUla::FLAG_M) != 0);
    }
}

TEST_CASE("V flag controls R3 FIFO mode", "[tube][flags][r3]") {
    TubeUla tube;

    SECTION("V=0: R3 operates as 1-byte latch") {
        tube.host_write(5, 0x42);
        uint8_t para_stat = tube.coprocessor_read(4);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) != 0);  // one byte is enough
    }

    SECTION("V=1: R3 operates as 2-byte FIFO") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_V);
        tube.host_write(5, 0x42);
        uint8_t para_stat = tube.coprocessor_read(4);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);  // one byte not enough
    }
}

TEST_CASE("T flag triggers soft reset", "[tube][flags]") {
    TubeUla tube;

    SECTION("Setting T clears all register data") {
        // Put data in R1 and R2.
        tube.host_write(1, 0xAA);  // R1 H-to-P
        tube.host_write(3, 0xBB);  // R2 H-to-P

        // Set T flag.
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_T);

        // R1 H-to-P should be empty.
        uint8_t para_stat = tube.coprocessor_read(0);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);

        // R2 H-to-P should be empty.
        para_stat = tube.coprocessor_read(2);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);
    }

    SECTION("T preserves other control flags") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_M);
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_T);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_Q) != 0);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_M) != 0);
    }

    SECTION("T fires every time it is written with S=1") {
        // T is not a persistent flag -- it is an action bit. Each write
        // with S=1 and T=1 triggers a soft reset. B-Em confirms: T (bit 6)
        // is not stored in the control register (only bits 0-5 are stored).
        tube.host_write(1, 0xAA);
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_T);
        REQUIRE((tube.coprocessor_read(0) & TubeUla::DATA_AVAILABLE) == 0);

        tube.host_write(1, 0xBB);
        // Writing T again clears data again.
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_T);
        REQUIRE((tube.coprocessor_read(0) & TubeUla::DATA_AVAILABLE) == 0);
    }

    SECTION("R3 P-to-H gets dummy byte after T reset") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_T);
        // In one-byte mode, R3 P-to-H should show data available (dummy byte).
        uint8_t host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) != 0);
    }
}

TEST_CASE("P flag asserts coprocessor reset", "[tube][flags]") {
    TubeUla tube;

    SECTION("Setting P makes coprocessor_reset_active() true") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_P);
        REQUIRE(tube.coprocessor_reset_active());
    }

    SECTION("Clearing P deasserts reset") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_P);
        tube.host_write(0, TubeUla::FLAG_P);  // S=0, clear P
        REQUIRE_FALSE(tube.coprocessor_reset_active());
    }
}

//////////////////////////////////////////////////////////////////////////////
// Interrupt generation: HIRQ
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("HIRQ generation", "[tube][interrupt][hirq]") {
    TubeUla tube;

    SECTION("No HIRQ when no flags enabled and no data pending") {
        REQUIRE_FALSE(tube.hirq());
    }

    SECTION("No HIRQ when Q=0 even with R4 P-to-H data") {
        tube.coprocessor_write(7, 0x42);
        REQUIRE_FALSE(tube.hirq());
    }

    SECTION("No HIRQ when Q=1 but R4 P-to-H empty") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        REQUIRE_FALSE(tube.hirq());
    }

    SECTION("HIRQ asserted when Q=1 and R4 P-to-H has data") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        tube.coprocessor_write(7, 0x42);
        REQUIRE(tube.hirq());
    }

    SECTION("HIRQ deasserted when R4 P-to-H data read by host") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        tube.coprocessor_write(7, 0x42);
        tube.host_read(7);
        REQUIRE_FALSE(tube.hirq());
    }

    SECTION("HIRQ deasserted when Q cleared") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        tube.coprocessor_write(7, 0x42);
        REQUIRE(tube.hirq());

        tube.host_write(0, TubeUla::FLAG_Q);  // S=0, clear Q
        REQUIRE_FALSE(tube.hirq());
    }

    SECTION("HIRQ not affected by R1, R2, or R3 state") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        tube.coprocessor_write(1, 0x42);  // R1 P-to-H
        REQUIRE_FALSE(tube.hirq());

        tube.coprocessor_write(3, 0x42);  // R2 P-to-H
        REQUIRE_FALSE(tube.hirq());
    }
}

//////////////////////////////////////////////////////////////////////////////
// Interrupt generation: PIRQ
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("PIRQ generation", "[tube][interrupt][pirq]") {
    TubeUla tube;

    SECTION("No PIRQ when no flags enabled") {
        tube.host_write(1, 0x42);  // R1 H-to-P
        REQUIRE_FALSE(tube.pirq());
    }

    SECTION("PIRQ when I=1 and R1 H-to-P has data") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);
        tube.host_write(1, 0x42);
        REQUIRE(tube.pirq());
    }

    SECTION("PIRQ deasserted when R1 data read by coprocessor") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);
        tube.host_write(1, 0x42);
        tube.coprocessor_read(1);
        REQUIRE_FALSE(tube.pirq());
    }

    SECTION("PIRQ when J=1 and R4 H-to-P has data") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_J);
        tube.host_write(7, 0x42);
        REQUIRE(tube.pirq());
    }

    SECTION("PIRQ deasserted when R4 data read by coprocessor") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_J);
        tube.host_write(7, 0x42);
        tube.coprocessor_read(7);
        REQUIRE_FALSE(tube.pirq());
    }

    SECTION("Both I and J active simultaneously") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I | TubeUla::FLAG_J);
        tube.host_write(1, 0xAA);  // R1 H-to-P
        tube.host_write(7, 0xBB);  // R4 H-to-P
        REQUIRE(tube.pirq());

        // Clear R1, still active via R4.
        tube.coprocessor_read(1);
        REQUIRE(tube.pirq());

        // Clear R4, now fully deasserted.
        tube.coprocessor_read(7);
        REQUIRE_FALSE(tube.pirq());
    }

    SECTION("PIRQ not affected by R2 or R3 state") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I | TubeUla::FLAG_J);
        tube.host_write(3, 0x42);  // R2 H-to-P
        tube.host_write(5, 0x42);  // R3 H-to-P
        REQUIRE_FALSE(tube.pirq());
    }
}

//////////////////////////////////////////////////////////////////////////////
// Interrupt generation: PNMI
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("PNMI generation", "[tube][interrupt][pnmi]") {
    TubeUla tube;

    SECTION("No PNMI when M=0") {
        tube.host_write(5, 0x42);  // R3 H-to-P data
        REQUIRE_FALSE(tube.pnmi());
    }

    SECTION("PNMI when M=1 and R3 H-to-P has data (one-byte mode)") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);
        tube.host_write(5, 0x42);
        REQUIRE(tube.pnmi());
    }

    SECTION("PNMI is edge-sensitive: only triggers on 0-to-1 transition") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

        // First H-to-P write triggers PNMI (edge 0->1).
        tube.host_write(5, 0x42);
        REQUIRE(tube.pnmi());

        // Coprocessor reads the data, clearing the condition.
        tube.coprocessor_read(5);

        // R3 P-to-H has dummy byte from reset, so p2h_count=1 (not 0).
        // PNMI condition depends on whether p2h is empty.
        // After reset, p2h has dummy byte (count=1), so "p2h empty" is false.
        // With h2p now also empty, PNMI should be false.
        // (PNMI = h2p has data OR p2h empty, with M=1)
    }

    SECTION("PNMI when R3 P-to-H is empty (coprocessor needs to write)") {
        // Drain the reset dummy byte from R3 P-to-H.
        tube.host_read(5);

        // Enable M.
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

        // P-to-H is now empty, so PNMI should fire.
        REQUIRE(tube.pnmi());
    }

    SECTION("PNMI clears when R3 P-to-H gets data (coprocessor writes)") {
        // Drain dummy byte.
        tube.host_read(5);

        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);
        REQUIRE(tube.pnmi());  // P-to-H empty triggers NMI

        tube.coprocessor_write(5, 0x42);  // P-to-H now has data
        // PNMI level should drop (p2h not empty, h2p also empty).
        REQUIRE_FALSE(tube.pnmi());
    }

    SECTION("New PNMI edge after clear and fresh data arrival") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

        // Write H-to-P data -> PNMI edge.
        tube.host_write(5, 0xAA);
        REQUIRE(tube.pnmi());

        // Coprocessor reads -> clears H-to-P condition.
        tube.coprocessor_read(5);
        // After read, p2h still has dummy byte, h2p empty -> PNMI level false.
        REQUIRE_FALSE(tube.pnmi());

        // Write again -> new edge.
        tube.host_write(5, 0xBB);
        REQUIRE(tube.pnmi());
    }

    SECTION("V flag interaction: in two-byte mode, PNMI after second byte only") {
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M | TubeUla::FLAG_V);

        tube.host_write(5, 0xAA);  // first byte
        // In two-byte mode, threshold is 2. H-to-P count=1 < 2, so no data trigger.
        // P-to-H has dummy byte (count=1), so p2h is not empty.
        // PNMI = (h2p >= 2) || (p2h == 0) = false || false = false.
        REQUIRE_FALSE(tube.pnmi());

        tube.host_write(5, 0xBB);  // second byte -> count=2 >= threshold
        REQUIRE(tube.pnmi());
    }
}

//////////////////////////////////////////////////////////////////////////////
// Reset behaviour
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Tube reset state", "[tube][reset]") {
    TubeUla tube;

    SECTION("All FIFOs empty after reset") {
        // R1 H-to-P: no data.
        uint8_t para_stat = tube.coprocessor_read(0);
        REQUIRE((para_stat & TubeUla::DATA_AVAILABLE) == 0);

        // R1 P-to-H: no data.
        uint8_t host_stat = tube.host_read(0);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) == 0);

        // R2: no data in either direction.
        REQUIRE((tube.coprocessor_read(2) & TubeUla::DATA_AVAILABLE) == 0);
        REQUIRE((tube.host_read(2) & TubeUla::DATA_AVAILABLE) == 0);

        // R4: no data in either direction.
        REQUIRE((tube.coprocessor_read(6) & TubeUla::DATA_AVAILABLE) == 0);
        REQUIRE((tube.host_read(6) & TubeUla::DATA_AVAILABLE) == 0);
    }

    SECTION("All control flags clear after reset") {
        REQUIRE(tube.control_flags() == 0);
    }

    SECTION("R3 coprocessor-to-host contains one dummy byte after reset") {
        // In one-byte mode (V=0), R3 P-to-H should show data available.
        uint8_t host_stat = tube.host_read(4);
        REQUIRE((host_stat & TubeUla::DATA_AVAILABLE) != 0);
    }

    SECTION("Dummy byte prevents spurious PNMI on first R3 H-to-P write") {
        // Enable M but do not drain the dummy byte.
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

        // P-to-H has the dummy byte (count=1), so it's not empty.
        // H-to-P is empty, so no data trigger either.
        // PNMI = (h2p >= 1) || (p2h == 0) = false || false = false.
        REQUIRE_FALSE(tube.pnmi());
    }

    SECTION("No HIRQ or PIRQ asserted after reset") {
        REQUIRE_FALSE(tube.hirq());
        REQUIRE_FALSE(tube.pirq());
    }

    SECTION("Reset during active transfer clears mid-transfer state") {
        // Fill up some registers.
        tube.host_write(1, 0xAA);  // R1 H-to-P
        tube.coprocessor_write(1, 0xBB);  // R1 P-to-H
        tube.host_write(3, 0xCC);  // R2 H-to-P
        tube.host_write(5, 0xDD);  // R3 H-to-P
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I);

        tube.reset();

        // Everything should be clean.
        REQUIRE(tube.control_flags() == 0);
        REQUIRE_FALSE(tube.hirq());
        REQUIRE_FALSE(tube.pirq());
        REQUIRE_FALSE(tube.pnmi());
        REQUIRE((tube.coprocessor_read(0) & TubeUla::DATA_AVAILABLE) == 0);
        REQUIRE((tube.host_read(0) & TubeUla::DATA_AVAILABLE) == 0);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Status register bit layout
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Status register bit layout", "[tube][status]") {
    TubeUla tube;

    SECTION("R1STAT host perspective: bit 7 = P-to-H not empty, bit 6 = H-to-P not full") {
        uint8_t stat = tube.host_read(0);
        // Initially: P-to-H empty (bit 7 = 0), H-to-P not full (bit 6 = 1).
        REQUIRE((stat & 0x80) == 0);
        REQUIRE((stat & 0x40) != 0);
    }

    SECTION("R1STAT coprocessor perspective: bit 7 = H-to-P data available, bit 6 = P-to-H not full") {
        uint8_t stat = tube.coprocessor_read(0);
        // Initially: H-to-P empty (bit 7 = 0), P-to-H not full (bit 6 = 1).
        REQUIRE((stat & 0x80) == 0);
        REQUIRE((stat & 0x40) != 0);
    }

    SECTION("Status bits update immediately after data write") {
        tube.host_write(1, 0x42);
        // Immediately: coprocessor sees data available.
        REQUIRE((tube.coprocessor_read(0) & 0x80) != 0);
        // Host sees not-full cleared.
        REQUIRE((tube.host_read(0) & 0x40) == 0);
    }

    SECTION("Status bits update immediately after data read") {
        tube.host_write(1, 0x42);
        tube.coprocessor_read(1);
        // Immediately: coprocessor sees no data.
        REQUIRE((tube.coprocessor_read(0) & 0x80) == 0);
        // Host sees space available again.
        REQUIRE((tube.host_read(0) & 0x40) != 0);
    }

    SECTION("R2/R3/R4 status: unused bits read as 1 (App Note 004, note 11)") {
        // R1 status has control flags in bits 0-5, so no unused bits.
        // R2 host status: bits 5-0 should be 1.
        uint8_t r2h = tube.host_read(2);
        REQUIRE((r2h & 0x3F) == 0x3F);

        // R2 coprocessor status: bits 5-0 should be 1.
        uint8_t r2p = tube.coprocessor_read(2);
        REQUIRE((r2p & 0x3F) == 0x3F);

        // R3 host status: bits 5-0 should be 1.
        uint8_t r3h = tube.host_read(4);
        REQUIRE((r3h & 0x3F) == 0x3F);

        // R3 coprocessor status: bits 5-0 should be 1 (N is bit 7, not bit 5).
        uint8_t r3p = tube.coprocessor_read(4);
        REQUIRE((r3p & 0x3F) == 0x3F);

        // R4 host status: bits 5-0 should be 1.
        uint8_t r4h = tube.host_read(6);
        REQUIRE((r4h & 0x3F) == 0x3F);

        // R4 coprocessor status: bits 5-0 should be 1.
        uint8_t r4p = tube.coprocessor_read(6);
        REQUIRE((r4p & 0x3F) == 0x3F);
    }

    SECTION("R1 status does not have unused bits (bits 0-5 are control flags)") {
        // After reset, control flags are 0, so bits 0-5 should be 0.
        uint8_t r1h = tube.host_read(0);
        REQUIRE((r1h & 0x3F) == 0x00);

        uint8_t r1p = tube.coprocessor_read(0);
        REQUIRE((r1p & 0x3F) == 0x00);
    }

    SECTION("Coprocessor R3 status: N (bit 7) reflects the register 3 action-required condition") {
        // Layout is N F3 1 1 1 1 1 1 (Application Note 004). Bit 5 is one
        // of the insignificant bits and reads as 1 regardless of N.

        // After reset: H-to-P empty, P-to-H has dummy byte (pending).
        // N = (h2p data) || (p2h space) = false || false = 0.
        uint8_t stat = tube.coprocessor_read(4);
        REQUIRE((stat & TubeUla::DATA_AVAILABLE) == 0);
        REQUIRE((stat & 0x3F) == 0x3F);

        // Drain dummy byte so P-to-H is empty (space available -> N = 1).
        tube.host_read(5);
        stat = tube.coprocessor_read(4);
        REQUIRE((stat & TubeUla::DATA_AVAILABLE) != 0);
        REQUIRE((stat & 0x3F) == 0x3F);

        // Write H-to-P data so h2p also has data (N still 1).
        tube.host_write(5, 0x42);
        stat = tube.coprocessor_read(4);
        REQUIRE((stat & TubeUla::DATA_AVAILABLE) != 0);

        // Coprocessor writes P-to-H (pending, space gone) and reads H-to-P (pending cleared).
        tube.coprocessor_write(5, 0x99);
        tube.coprocessor_read(5);  // consume H-to-P
        // Now: h2p empty (not pending), p2h pending -> N = false || false = 0.
        stat = tube.coprocessor_read(4);
        REQUIRE((stat & TubeUla::DATA_AVAILABLE) == 0);
        REQUIRE((stat & 0x3F) == 0x3F);
    }

    SECTION("N is independent of M flag") {
        // Drain dummy byte.
        tube.host_read(5);

        // N should reflect the condition even with M=0 (no NMI enabled).
        // P-to-H is empty -> space available -> N = 1.
        uint8_t stat = tube.coprocessor_read(4);
        REQUIRE((stat & TubeUla::DATA_AVAILABLE) != 0);
    }

    SECTION("All four register status bytes accessible") {
        // R1 status at offset 0.
        tube.host_read(0);  // should not crash
        tube.coprocessor_read(0);

        // R2 status at offset 2.
        tube.host_read(2);
        tube.coprocessor_read(2);

        // R3 status at offset 4.
        tube.host_read(4);
        tube.coprocessor_read(4);

        // R4 status at offset 6.
        tube.host_read(6);
        tube.coprocessor_read(6);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Boundary conditions and ordering
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("R1 FIFO boundary conditions", "[tube][fifo][boundary]") {
    TubeUla tube;

    SECTION("Fill to exactly 23, verify not-full, add 24th, verify full") {
        for (int i = 0; i < 23; i++)
            tube.coprocessor_write(1, static_cast<uint8_t>(i));

        uint8_t stat = tube.coprocessor_read(0);
        REQUIRE((stat & TubeUla::SPACE_AVAILABLE) != 0);

        tube.coprocessor_write(1, 23);
        stat = tube.coprocessor_read(0);
        REQUIRE((stat & TubeUla::SPACE_AVAILABLE) == 0);
    }

    SECTION("Read one from full FIFO, verify not-full, write one, verify full again") {
        for (int i = 0; i < 24; i++)
            tube.coprocessor_write(1, static_cast<uint8_t>(i));

        tube.host_read(1);  // read one off

        uint8_t stat = tube.coprocessor_read(0);
        REQUIRE((stat & TubeUla::SPACE_AVAILABLE) != 0);

        tube.coprocessor_write(1, 0xFF);  // write one more

        stat = tube.coprocessor_read(0);
        REQUIRE((stat & TubeUla::SPACE_AVAILABLE) == 0);
    }

    SECTION("FIFO wraps around correctly") {
        // Fill and drain 20 bytes to advance head/tail pointers.
        for (int i = 0; i < 20; i++)
            tube.coprocessor_write(1, static_cast<uint8_t>(i));
        for (int i = 0; i < 20; i++)
            tube.host_read(1);

        // Now fill 24 bytes (will wrap around the 24-element buffer).
        for (int i = 0; i < 24; i++)
            tube.coprocessor_write(1, static_cast<uint8_t>(100 + i));
        for (int i = 0; i < 24; i++)
            REQUIRE(tube.host_read(1) == static_cast<uint8_t>(100 + i));
    }
}

//////////////////////////////////////////////////////////////////////////////
// Writes to a full register
//
// The Tube ULA has no way to stall the host (issue #71; see
// docs/discussion/tube-ula-full-register-writes.md). Every write completes in
// its own cycle and, when the target register is full, is either ignored or
// overwrites, per hoglet's measured Ferranti table:
//
//   HP1 overwrite | PH1 (24-deep) ignore | HP2/PH2 overwrite
//   HP3 ignore    | PH3 ignore           | HP4/PH4 overwrite
//
// One case per row and direction. R1 data = offset 1, R2 = 3, R3 = 5, R4 = 7.
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("R1 H-to-P (HP1): a full latch overwrites", "[tube][full-write][r1]") {
    TubeUla tube;
    tube.host_write(1, 0xAA);        // latch now full
    tube.host_write(1, 0xBB);        // full: overwrites (not stalled, not dropped)
    REQUIRE(tube.coprocessor_read(1) == 0xBB);
}

TEST_CASE("R1 P-to-H (PH1): a full 24-byte FIFO ignores", "[tube][full-write][r1]") {
    TubeUla tube;
    for (int i = 0; i < 24; ++i)
        tube.coprocessor_write(1, static_cast<uint8_t>(0x40 + i));  // fill
    tube.coprocessor_write(1, 0xFF);  // full: ignored
    for (int i = 0; i < 24; ++i)
        REQUIRE(tube.host_read(1) == static_cast<uint8_t>(0x40 + i));  // 0xFF absent
}

TEST_CASE("R2 H-to-P (HP2): a full latch overwrites", "[tube][full-write][r2]") {
    TubeUla tube;
    tube.host_write(3, 0xAA);
    tube.host_write(3, 0xBB);        // overwrites without blocking
    REQUIRE(tube.coprocessor_read(3) == 0xBB);
}

TEST_CASE("R2 P-to-H (PH2): a full latch overwrites", "[tube][full-write][r2]") {
    TubeUla tube;
    tube.coprocessor_write(3, 0xAA);
    tube.coprocessor_write(3, 0xBB);
    REQUIRE(tube.host_read(3) == 0xBB);
}

TEST_CASE("R3 H-to-P (HP3): a full 2-byte FIFO ignores", "[tube][full-write][r3]") {
    TubeUla tube;                     // one-byte mode; depth is two regardless
    tube.host_write(5, 0x11);
    tube.host_write(5, 0x22);         // FIFO now full
    tube.host_write(5, 0x33);         // full: ignored
    REQUIRE(tube.coprocessor_read(5) == 0x11);
    REQUIRE(tube.coprocessor_read(5) == 0x22);  // 0x33 never queued
    REQUIRE((tube.coprocessor_peek(4) & TubeUla::DATA_AVAILABLE) == 0);  // empty
}

TEST_CASE("R3 P-to-H (PH3): a full 2-byte FIFO ignores", "[tube][full-write][r3]") {
    TubeUla tube;
    tube.host_read(5);                // drain the reset dummy byte first
    tube.coprocessor_write(5, 0x11);
    tube.coprocessor_write(5, 0x22);  // FIFO now full
    tube.coprocessor_write(5, 0x33);  // full: ignored
    REQUIRE(tube.host_read(5) == 0x11);
    REQUIRE(tube.host_read(5) == 0x22);  // 0x33 never queued
    REQUIRE((tube.host_read(4) & TubeUla::DATA_AVAILABLE) == 0);  // empty
}

TEST_CASE("R4 H-to-P (HP4): a full latch overwrites", "[tube][full-write][r4]") {
    TubeUla tube;
    tube.host_write(7, 0xAA);
    tube.host_write(7, 0xBB);        // full: overwrites (not stalled, not dropped)
    REQUIRE(tube.coprocessor_read(7) == 0xBB);
}

TEST_CASE("R4 P-to-H (PH4): a full latch overwrites", "[tube][full-write][r4]") {
    TubeUla tube;
    tube.coprocessor_write(7, 0xAA);
    tube.coprocessor_write(7, 0xBB);
    REQUIRE(tube.host_read(7) == 0xBB);
}

TEST_CASE("Register access mirroring", "[tube][addressing]") {
    TubeUla tube;

    SECTION("Offset masked to 3 bits: offset 8 == offset 0") {
        // Write control flags via offset 0.
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        REQUIRE((tube.control_flags() & TubeUla::FLAG_Q) != 0);

        // Read back via offset 8 (same register due to 3-bit mask).
        uint8_t stat = tube.host_read(8);
        REQUIRE((stat & TubeUla::FLAG_Q) != 0);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Interleaved host/coprocessor access
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("TubeUla R2 handshake interleaved", "[tube]") {
    TubeUla tube;
    constexpr int num_transfers = 1000;
    int host_received = 0;
    int coprocessor_received = 0;
    int coprocessor_write_idx = 0;
    bool coprocessor_waiting_reply = false;
    int host_read_idx = 0;

    for (int tick = 0; tick < 100000 && (host_received < num_transfers || coprocessor_received < num_transfers); ++tick) {
        // Coprocessor side: write a byte to P2H, then wait for H2P reply
        if (!coprocessor_waiting_reply && coprocessor_write_idx < num_transfers) {
            tube.coprocessor_write(3, static_cast<uint8_t>(coprocessor_write_idx & 0xFF));
            coprocessor_waiting_reply = true;
        }
        if (coprocessor_waiting_reply) {
            uint8_t status = tube.coprocessor_read(2);
            if (status & TubeUla::DATA_AVAILABLE) {
                tube.coprocessor_read(3);
                ++coprocessor_received;
                ++coprocessor_write_idx;
                coprocessor_waiting_reply = false;
            }
        }

        // Host side: read from P2H, then reply on H2P
        if (host_read_idx < num_transfers) {
            uint8_t status = tube.host_read(2);
            if (status & TubeUla::DATA_AVAILABLE) {
                tube.host_read(3);
                ++host_received;
                tube.host_write(3, static_cast<uint8_t>(host_read_idx & 0xFF));
                ++host_read_idx;
            }
        }
    }
    CHECK(host_received == num_transfers);
    CHECK(coprocessor_received == num_transfers);
}

TEST_CASE("TubeUla R1 FIFO transfer interleaved", "[tube]") {
    TubeUla tube;
    constexpr int num_bytes = 5000;
    std::vector<uint8_t> received;
    received.reserve(num_bytes);

    int coprocessor_written = 0;
    for (int tick = 0; tick < 1000000 && static_cast<int>(received.size()) < num_bytes; ++tick) {
        // Coprocessor writes to R1 P2H FIFO
        if (coprocessor_written < num_bytes) {
            uint8_t status = tube.coprocessor_read(0);
            if (status & TubeUla::SPACE_AVAILABLE) {
                tube.coprocessor_write(1, static_cast<uint8_t>(coprocessor_written & 0xFF));
                ++coprocessor_written;
            }
        }
        // Host reads from R1 P2H FIFO
        {
            uint8_t status = tube.host_read(0);
            if (status & TubeUla::DATA_AVAILABLE) {
                received.push_back(tube.host_read(1));
            }
        }
    }
    REQUIRE(received.size() == num_bytes);
    for (int i = 0; i < num_bytes; ++i) {
        INFO("byte " << i);
        CHECK(received[i] == static_cast<uint8_t>(i & 0xFF));
    }
}

TEST_CASE("TubeUla R3 NMI-style transfer interleaved", "[tube]") {
    TubeUla tube;
    constexpr int num_bytes = 2000;
    std::vector<uint8_t> received;
    received.reserve(num_bytes);

    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

    int host_written = 0;
    for (int tick = 0; tick < 10000000 && static_cast<int>(received.size()) < num_bytes; ++tick) {
        // Host writes to R3 H2P (check space first -- no spin-wait)
        if (host_written < num_bytes
            && (tube.host_peek(4) & TubeUla::SPACE_AVAILABLE)) {
            tube.host_write(5, static_cast<uint8_t>(host_written & 0xFF));
            ++host_written;
        }
        // Coprocessor reads from R3 H2P
        {
            uint8_t status = tube.coprocessor_read(4);
            if (status & TubeUla::DATA_AVAILABLE) {
                received.push_back(tube.coprocessor_read(5));
            }
        }
    }
    REQUIRE(received.size() == num_bytes);
    for (int i = 0; i < num_bytes; ++i) {
        INFO("byte " << i);
        CHECK(received[i] == static_cast<uint8_t>(i & 0xFF));
    }
}
