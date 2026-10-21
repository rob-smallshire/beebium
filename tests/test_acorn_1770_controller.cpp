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

#include <catch2/catch_test_macros.hpp>

#include "beebium/ResetCause.hpp"
#include "beebium/disc/Acorn1770DiscController.hpp"
#include "beebium/disc/DiscDrive.hpp"

using namespace beebium;

TEST_CASE("Acorn1770DiscController identification", "[disc][acorn1770]") {
    Acorn1770DiscController controller;

    CHECK(controller.name() == "Acorn 1770");
}

TEST_CASE("Acorn1770DiscController control register address mapping", "[disc][acorn1770]") {
    Acorn1770DiscController controller;

    SECTION("control register is write-only (reads as 0xFF)") {
        // Control register at offset 0x00-0x03 (mirrored)
        CHECK(controller.read(0x00) == 0xFF);
        CHECK(controller.read(0x01) == 0xFF);
        CHECK(controller.read(0x02) == 0xFF);
        CHECK(controller.read(0x03) == 0xFF);
    }

    SECTION("WD1770 registers at offset 0x04-0x07") {
        // Status register (offset 0x04) should be readable
        uint8_t status = controller.read(0x04);
        CHECK((status & 0x01) == 0x00);  // Not busy

        // Track register (offset 0x05) - defaults to 0
        CHECK(controller.read(0x05) == 0x00);

        // Sector register (offset 0x06) - defaults to 1
        CHECK(controller.read(0x06) == 0x01);

        // Data register (offset 0x07) - defaults to 0
        CHECK(controller.read(0x07) == 0x00);
    }

    SECTION("addresses beyond 0x07 return 0xFF") {
        CHECK(controller.read(0x08) == 0xFF);
        CHECK(controller.read(0x0F) == 0xFF);
        CHECK(controller.read(0x1F) == 0xFF);
    }

    SECTION("address mirroring within regions") {
        // Control register mirrors at 0x00-0x03
        controller.write(0x00, 0x21);
        CHECK(controller.control() == 0x21);

        controller.write(0x03, 0x25);
        CHECK(controller.control() == 0x25);

        // WD1770 track register mirrors at 0x04-0x07 (offset 1)
        controller.write(0x05, 0x10);
        CHECK(controller.read(0x05) == 0x10);
    }
}

TEST_CASE("Acorn1770DiscController control register bits", "[disc][acorn1770]") {
    Acorn1770DiscController controller;

    SECTION("drive select (bits 0-1)") {
        // Bit 0 = drive 0
        controller.write(0x00, 0x01);
        CHECK(controller.wd1770().selected_drive() == 0);

        // Bit 1 = drive 1
        controller.write(0x00, 0x02);
        CHECK(controller.wd1770().selected_drive() == 1);

        // Both bits set - bit 0 takes priority
        controller.write(0x00, 0x03);
        CHECK(controller.wd1770().selected_drive() == 0);
    }

    SECTION("side select (bit 2)") {
        controller.write(0x00, 0x00);
        CHECK(controller.wd1770().selected_side() == 0);

        controller.write(0x00, 0x04);
        CHECK(controller.wd1770().selected_side() == 1);
    }

    SECTION("density select (bit 3)") {
        // Bit 3 = 0: double density (MFM)
        controller.write(0x00, 0x00);
        CHECK(controller.wd1770().is_double_density() == true);

        // Bit 3 = 1: single density (FM)
        controller.write(0x00, 0x08);
        CHECK(controller.wd1770().is_double_density() == false);
    }

    SECTION("NMI enable (bit 6)") {
        controller.write(0x00, 0x00);
        CHECK(controller.nmi_enabled() == false);

        controller.write(0x00, 0x40);
        CHECK(controller.nmi_enabled() == true);

        controller.write(0x00, 0x00);
        CHECK(controller.nmi_enabled() == false);
    }
}

TEST_CASE("Acorn1770DiscController reset (bit 5)", "[disc][acorn1770]") {
    Acorn1770DiscController controller;

    SECTION("reset is active-low, edge triggered") {
        // Set up some state in WD1770
        controller.write(0x05, 0x10);  // Track register
        CHECK(controller.read(0x05) == 0x10);

        // Reset inactive (bit 5 = 1)
        controller.write(0x00, 0x20);
        CHECK(controller.read(0x05) == 0x10);  // Still has value

        // Trigger reset by falling edge (1 -> 0)
        controller.write(0x00, 0x00);  // Bit 5 = 0
        CHECK(controller.read(0x05) == 0x00);  // Track reset to 0
    }

    SECTION("reset only triggers on falling edge") {
        controller.write(0x05, 0x10);

        // Already low - no edge
        controller.write(0x00, 0x00);
        controller.write(0x05, 0x20);  // Set track again
        controller.write(0x00, 0x00);  // Still low - no falling edge
        CHECK(controller.read(0x05) == 0x20);  // No reset

        // Rising edge (0 -> 1) - no reset
        controller.write(0x00, 0x20);
        CHECK(controller.read(0x05) == 0x20);  // No reset

        // Falling edge (1 -> 0) - reset!
        controller.write(0x00, 0x00);
        CHECK(controller.read(0x05) == 0x00);  // Reset occurred
    }
}

TEST_CASE("Acorn1770DiscController drive attachment", "[disc][acorn1770]") {
    Acorn1770DiscController controller;
    DiscDrive drive0;
    DiscDrive drive1;

    SECTION("attach and query drives") {
        CHECK(controller.attached_drive(0) == nullptr);
        CHECK(controller.attached_drive(1) == nullptr);

        controller.attach_drive(0, &drive0);
        CHECK(controller.attached_drive(0) == &drive0);
        CHECK(controller.attached_drive(1) == nullptr);

        controller.attach_drive(1, &drive1);
        CHECK(controller.attached_drive(0) == &drive0);
        CHECK(controller.attached_drive(1) == &drive1);
    }

    SECTION("detach all drives") {
        controller.attach_drive(0, &drive0);
        controller.attach_drive(1, &drive1);

        controller.detach_drives();

        CHECK(controller.attached_drive(0) == nullptr);
        CHECK(controller.attached_drive(1) == nullptr);
    }

    SECTION("invalid drive numbers return nullptr") {
        controller.attach_drive(0, &drive0);
        CHECK(controller.attached_drive(-1) == nullptr);
        CHECK(controller.attached_drive(2) == nullptr);
    }
}

TEST_CASE("Acorn1770DiscController spin-up delay configuration", "[disc][acorn1770]") {
    Acorn1770DiscController controller;

    SECTION("spin-up delay enabled by default") {
        CHECK(controller.spin_up_delay_enabled() == true);
    }

    SECTION("can disable spin-up delay") {
        controller.set_spin_up_delay_enabled(false);
        CHECK(controller.spin_up_delay_enabled() == false);
    }

    SECTION("can re-enable spin-up delay") {
        controller.set_spin_up_delay_enabled(false);
        controller.set_spin_up_delay_enabled(true);
        CHECK(controller.spin_up_delay_enabled() == true);
    }
}

TEST_CASE("Acorn1770DiscController reset", "[disc][acorn1770]") {
    Acorn1770DiscController controller;

    SECTION("reset clears control register") {
        controller.write(0x00, 0x55);
        CHECK(controller.control() == 0x55);

        controller.reset(ResetCause::PowerOn);
        CHECK(controller.control() == 0x00);
    }

    SECTION("reset clears NMI enable") {
        controller.write(0x00, 0x40);
        CHECK(controller.nmi_enabled() == true);

        controller.reset(ResetCause::PowerOn);
        CHECK(controller.nmi_enabled() == false);
    }

    SECTION("reset resets WD1770") {
        controller.write(0x05, 0x10);  // Track
        controller.write(0x06, 0x05);  // Sector

        controller.reset(ResetCause::PowerOn);

        CHECK(controller.read(0x05) == 0x00);  // Track reset
        CHECK(controller.read(0x06) == 0x01);  // Sector reset to 1
    }
}
