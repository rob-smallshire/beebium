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

#include "beebium/disc/DiscControllerSocket.hpp"
#include "beebium/disc/Acorn1770DiscController.hpp"
#include "beebium/disc/DiscDrive.hpp"

using namespace beebium;

TEST_CASE("DiscControllerSocket empty socket behavior", "[disc][socket]") {
    DiscControllerSocket socket;

    SECTION("empty socket has no controller") {
        CHECK_FALSE(socket.has_controller());
        CHECK(socket.controller() == nullptr);
    }

    SECTION("empty socket reads return 0xFF (open bus)") {
        CHECK(socket.read(0x00) == 0xFF);
        CHECK(socket.read(0x04) == 0xFF);
        CHECK(socket.read(0x1F) == 0xFF);
    }

    SECTION("empty socket writes are ignored") {
        socket.write(0x00, 0x55);  // Should not crash
        socket.write(0x04, 0xAA);
        // Still reads as 0xFF
        CHECK(socket.read(0x00) == 0xFF);
    }

    SECTION("empty socket has no NMI pending") {
        CHECK_FALSE(socket.nmi_pending());
    }

    SECTION("empty socket tick does nothing") {
        socket.tick();  // Should not crash
        CHECK_FALSE(socket.nmi_pending());
    }

    SECTION("empty socket reset does nothing") {
        socket.reset();  // Should not crash
        CHECK_FALSE(socket.has_controller());
    }

    SECTION("empty socket attached_drive returns nullptr") {
        CHECK(socket.attached_drive(0) == nullptr);
        CHECK(socket.attached_drive(1) == nullptr);
    }
}

TEST_CASE("DiscControllerSocket controller installation", "[disc][socket]") {
    DiscControllerSocket socket;

    SECTION("install controller") {
        auto controller = std::make_unique<Acorn1770DiscController>();
        socket.install(std::move(controller));

        CHECK(socket.has_controller());
        CHECK(socket.controller() != nullptr);
        CHECK(socket.controller()->name() == "Acorn 1770");
    }

    SECTION("remove controller returns it") {
        socket.install(std::make_unique<Acorn1770DiscController>());
        CHECK(socket.has_controller());

        auto removed = socket.remove();
        CHECK(removed != nullptr);
        CHECK(removed->name() == "Acorn 1770");
        CHECK_FALSE(socket.has_controller());
    }

    SECTION("remove from empty socket returns nullptr") {
        auto removed = socket.remove();
        CHECK(removed == nullptr);
    }

    SECTION("installing new controller replaces old") {
        socket.install(std::make_unique<Acorn1770DiscController>());
        auto* first = socket.controller();

        socket.install(std::make_unique<Acorn1770DiscController>());
        auto* second = socket.controller();

        CHECK(first != second);
        CHECK(socket.has_controller());
    }
}

TEST_CASE("DiscControllerSocket drive attachment", "[disc][socket]") {
    DiscControllerSocket socket;
    DiscDrive drive0;
    DiscDrive drive1;

    SECTION("attach drives to empty socket does nothing") {
        socket.attach_drive(0, &drive0);
        socket.attach_drive(1, &drive1);
        // No crash, drives not queryable without controller
        CHECK(socket.attached_drive(0) == nullptr);
        CHECK(socket.attached_drive(1) == nullptr);
    }

    SECTION("attach drives after controller installation") {
        socket.install(std::make_unique<Acorn1770DiscController>());
        socket.attach_drive(0, &drive0);
        socket.attach_drive(1, &drive1);

        CHECK(socket.attached_drive(0) == &drive0);
        CHECK(socket.attached_drive(1) == &drive1);
    }

    SECTION("drives detached when controller removed") {
        socket.install(std::make_unique<Acorn1770DiscController>());
        socket.attach_drive(0, &drive0);
        socket.attach_drive(1, &drive1);

        socket.remove();

        CHECK(socket.attached_drive(0) == nullptr);
        CHECK(socket.attached_drive(1) == nullptr);
    }

    SECTION("drives detached when new controller installed") {
        socket.install(std::make_unique<Acorn1770DiscController>());
        socket.attach_drive(0, &drive0);

        CHECK(socket.attached_drive(0) == &drive0);

        // Install new controller - old drives should be detached
        socket.install(std::make_unique<Acorn1770DiscController>());

        // New controller has no drives attached yet
        CHECK(socket.attached_drive(0) == nullptr);
    }
}

TEST_CASE("DiscControllerSocket delegates to controller", "[disc][socket]") {
    DiscControllerSocket socket;
    socket.install(std::make_unique<Acorn1770DiscController>());

    SECTION("read delegates to controller") {
        // Control register (0x00-0x03) reads as 0xFF (write-only)
        CHECK(socket.read(0x00) == 0xFF);

        // WD1770 registers (0x04-0x07) readable
        // Status register should be 0x00 initially (not busy)
        CHECK((socket.read(0x04) & 0x01) == 0x00);  // Not busy
    }

    SECTION("write delegates to controller") {
        // Write to control register
        socket.write(0x00, 0x21);  // Drive 0, reset inactive

        // Can verify through controller
        auto* ctrl = dynamic_cast<Acorn1770DiscController*>(socket.controller());
        REQUIRE(ctrl != nullptr);
        CHECK(ctrl->control() == 0x21);
    }

    SECTION("reset delegates to controller") {
        socket.write(0x00, 0x55);
        socket.reset();

        auto* ctrl = dynamic_cast<Acorn1770DiscController*>(socket.controller());
        REQUIRE(ctrl != nullptr);
        CHECK(ctrl->control() == 0x00);
    }
}

TEST_CASE("DiscControllerSocket reports the fitted controller's chip (#173)",
          "[disc][socket][fdc-chip]") {
    DiscControllerSocket socket;
    CHECK(socket.fdc_chip().empty());

    socket.install(std::make_unique<Acorn1770DiscController>());
    CHECK(socket.fdc_chip() == "WD1770");

    socket.remove();
    CHECK(socket.fdc_chip().empty());
}

TEST_CASE("The Acorn 1770 controller names its chip from the device", "[disc][fdc-chip]") {
    Acorn1770DiscController controller;
    CHECK(controller.fdc_chip() == controller.wd1770().name());
    CHECK(controller.fdc_chip() == "WD1770");
}
