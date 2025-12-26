// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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
#include <beebium/Via6522.hpp>

using namespace beebium;

//////////////////////////////////////////////////////////////////////////////
// Initialization and reset tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 initialization", "[via][init]") {
    Via6522 via;

    SECTION("Registers default to expected values") {
        REQUIRE(via.read(Via6522::REG_DDRA) == 0x00);  // All inputs
        REQUIRE(via.read(Via6522::REG_DDRB) == 0x00);
        REQUIRE(via.read(Via6522::REG_ACR) == 0x00);
        REQUIRE(via.read(Via6522::REG_PCR) == 0x00);
    }

    SECTION("IER bit 7 always reads as 1") {
        REQUIRE((via.read(Via6522::REG_IER) & 0x80) == 0x80);
    }

    SECTION("IFR is clear initially") {
        REQUIRE((via.read(Via6522::REG_IFR) & 0x7F) == 0x00);
    }

    SECTION("Port pins read high by default (inputs pulled up)") {
        REQUIRE(via.read(Via6522::REG_ORA_NH) == 0xFF);
    }

    SECTION("No IRQ pending initially") {
        REQUIRE_FALSE(via.irq_pending());
    }
}

TEST_CASE("Via6522 reset", "[via][reset]") {
    Via6522 via;

    // Modify some state
    via.write(Via6522::REG_DDRA, 0xFF);
    via.write(Via6522::REG_ORA, 0x42);
    via.write(Via6522::REG_IER, 0xFF);  // Enable all interrupts

    // Reset
    via.reset();

    SECTION("DDR is cleared") {
        REQUIRE(via.read(Via6522::REG_DDRA) == 0x00);
    }

    SECTION("IER is cleared (except bit 7)") {
        REQUIRE(via.read(Via6522::REG_IER) == 0x80);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Port tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 data direction register", "[via][ports]") {
    Via6522 via;

    SECTION("DDR 0 means input, 1 means output") {
        via.write(Via6522::REG_DDRA, 0xF0);  // High nibble output
        REQUIRE(via.read(Via6522::REG_DDRA) == 0xF0);

        via.write(Via6522::REG_DDRB, 0x0F);  // Low nibble output
        REQUIRE(via.read(Via6522::REG_DDRB) == 0x0F);
    }
}

TEST_CASE("Via6522 port output", "[via][ports]") {
    Via6522 via;

    SECTION("Output bits come from OR when DDR is output") {
        via.write(Via6522::REG_DDRA, 0xFF);  // All outputs
        via.write(Via6522::REG_ORA, 0xAA);
        // Need to run a clock cycle for port value to update
        via.update_phi2_trailing_edge();
        REQUIRE(via.port_a().p == 0xAA);
    }

    SECTION("Input bits are pulled high") {
        via.write(Via6522::REG_DDRA, 0x00);  // All inputs
        via.update_phi2_trailing_edge();
        REQUIRE(via.port_a().p == 0xFF);
    }

    SECTION("Mixed input/output") {
        via.write(Via6522::REG_DDRA, 0xF0);  // High nibble output
        via.write(Via6522::REG_ORA, 0xA5);
        via.update_phi2_trailing_edge();
        // High nibble from OR (0xA0), low nibble pulled high (0x0F)
        REQUIRE(via.port_a().p == 0xAF);
    }
}

TEST_CASE("Via6522 port read", "[via][ports]") {
    Via6522 via;

    SECTION("Reading ORB returns output bits from OR, input bits from port") {
        via.write(Via6522::REG_DDRB, 0xF0);  // High nibble output
        via.write(Via6522::REG_ORB, 0xA0);
        via.update_phi2_trailing_edge();
        uint8_t value = via.read(Via6522::REG_ORB);
        // Output bits: 0xA0, input bits: 0x0F (pulled high)
        REQUIRE(value == 0xAF);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Timer 1 tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 timer 1 one-shot mode", "[via][timer][t1]") {
    Via6522 via;

    // Ensure one-shot mode (ACR bit 6 = 0)
    via.write(Via6522::REG_ACR, 0x00);

    // Enable T1 interrupt
    via.write(Via6522::REG_IER, 0xC0);  // Set bit 6 (T1)

    SECTION("Timer 1 fires after N+1.5 cycles") {
        // Program T1 for 5 cycle timeout
        via.write(Via6522::REG_T1LL, 5);
        via.write(Via6522::REG_T1CH, 0);  // Start timer

        // T1 should not have fired yet
        REQUIRE_FALSE(via.irq_pending());

        // Run 5 trailing edge + 5 leading edge cycles
        // Timer counts down on trailing edge
        // Initial timeout is N+1.5 cycles due to phase offset
        for (int i = 0; i < 6; i++) {
            via.update_phi2_trailing_edge();
            REQUIRE_FALSE(via.irq_pending());  // Not yet
            via.update_phi2_leading_edge();
        }

        // Run one more trailing edge (timeout happens)
        via.update_phi2_trailing_edge();
        // IRQ fires on leading edge
        via.update_phi2_leading_edge();

        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) != 0);  // T1 flag set
        REQUIRE(via.irq_pending());
    }

    SECTION("Reading T1C-L clears T1 interrupt") {
        via.write(Via6522::REG_T1LL, 0);
        via.write(Via6522::REG_T1CH, 0);

        // Run until timeout
        for (int i = 0; i < 3; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) != 0);

        // Read T1C-L to clear
        via.read(Via6522::REG_T1CL);
        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) == 0);
    }

    SECTION("One-shot mode does not re-fire") {
        via.write(Via6522::REG_T1LL, 2);
        via.write(Via6522::REG_T1CH, 0);

        // Run until first timeout
        for (int i = 0; i < 5; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE(via.irq_pending());

        // Clear the interrupt
        via.read(Via6522::REG_T1CL);
        REQUIRE_FALSE(via.irq_pending());

        // Run more cycles - should not fire again
        for (int i = 0; i < 10; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE_FALSE(via.irq_pending());
    }
}

TEST_CASE("Via6522 timer 1 continuous mode", "[via][timer][t1]") {
    Via6522 via;

    // Enable continuous mode (ACR bit 6 = 1)
    via.write(Via6522::REG_ACR, 0x40);

    // Enable T1 interrupt
    via.write(Via6522::REG_IER, 0xC0);

    SECTION("Timer 1 re-fires in continuous mode") {
        via.write(Via6522::REG_T1LL, 3);
        via.write(Via6522::REG_T1CH, 0);

        // Run until first timeout
        int cycles = 0;
        while (!via.irq_pending() && cycles < 20) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
            cycles++;
        }
        REQUIRE(via.irq_pending());

        // Need to run a trailing edge first - reading T1C-L won't clear the
        // interrupt if it just timed out this cycle (matching B2 behavior)
        via.update_phi2_trailing_edge();

        // Now clear interrupt
        via.read(Via6522::REG_T1CL);
        REQUIRE_FALSE(via.irq_pending());

        // Run until second timeout
        cycles = 0;
        while (!via.irq_pending() && cycles < 20) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
            cycles++;
        }
        REQUIRE(via.irq_pending());  // Should fire again
    }
}

TEST_CASE("Via6522 timer 1 PB7 toggle", "[via][timer][t1][pb7]") {
    Via6522 via;

    // Enable T1 output to PB7 (ACR bit 7 = 1) and continuous mode
    via.write(Via6522::REG_ACR, 0xC0);  // bits 7 and 6

    SECTION("PB7 goes low when timer starts") {
        via.write(Via6522::REG_T1LL, 5);
        via.write(Via6522::REG_T1CH, 0);  // Start timer - PB7 should go low
        REQUIRE(via.state().t1_pb7 == 0);
    }

    SECTION("PB7 toggles on timeout") {
        via.write(Via6522::REG_T1LL, 2);
        via.write(Via6522::REG_T1CH, 0);

        REQUIRE(via.state().t1_pb7 == 0);  // Initially low

        // Run until timeout
        for (int i = 0; i < 5; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE(via.state().t1_pb7 == 0x80);  // Toggled to high
    }
}

//////////////////////////////////////////////////////////////////////////////
// Timer 2 tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 timer 2 one-shot", "[via][timer][t2]") {
    Via6522 via;

    // Enable T2 interrupt
    via.write(Via6522::REG_IER, 0xA0);  // Set bit 5 (T2)

    SECTION("Timer 2 fires once") {
        via.write(Via6522::REG_T2CL, 3);  // Low latch
        via.write(Via6522::REG_T2CH, 0);  // High counter, starts timer

        REQUIRE_FALSE(via.irq_pending());

        // Run until timeout
        int cycles = 0;
        while (!via.irq_pending() && cycles < 20) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
            cycles++;
        }

        REQUIRE((via.read(Via6522::REG_IFR) & 0x20) != 0);  // T2 flag set
        REQUIRE(via.irq_pending());
    }

    SECTION("Reading T2C-L clears T2 interrupt") {
        via.write(Via6522::REG_T2CL, 0);
        via.write(Via6522::REG_T2CH, 0);

        for (int i = 0; i < 5; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE((via.read(Via6522::REG_IFR) & 0x20) != 0);

        via.read(Via6522::REG_T2CL);
        REQUIRE((via.read(Via6522::REG_IFR) & 0x20) == 0);
    }
}

//////////////////////////////////////////////////////////////////////////////
// IFR/IER tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 IFR operations", "[via][ifr]") {
    Via6522 via;

    SECTION("Writing 1s to IFR clears flags") {
        // Force T1 interrupt
        via.write(Via6522::REG_IER, 0xC0);
        via.write(Via6522::REG_T1LL, 0);
        via.write(Via6522::REG_T1CH, 0);

        for (int i = 0; i < 5; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) != 0);

        // Clear T1 flag by writing 0x40 to IFR
        via.write(Via6522::REG_IFR, 0x40);
        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) == 0);
    }

    SECTION("IFR bit 7 reflects enabled interrupt state") {
        // No enabled interrupts = bit 7 clear
        REQUIRE((via.read(Via6522::REG_IFR) & 0x80) == 0);

        // Enable T1 and trigger it
        via.write(Via6522::REG_IER, 0xC0);
        via.write(Via6522::REG_T1LL, 0);
        via.write(Via6522::REG_T1CH, 0);

        for (int i = 0; i < 5; i++) {
            via.update_phi2_trailing_edge();
            via.update_phi2_leading_edge();
        }

        // Enabled interrupt pending = bit 7 set
        REQUIRE((via.read(Via6522::REG_IFR) & 0x80) != 0);
    }
}

TEST_CASE("Via6522 IER operations", "[via][ier]") {
    Via6522 via;

    SECTION("IER bit 7 = 1 sets bits") {
        via.write(Via6522::REG_IER, 0x82);  // Set CA1 enable
        REQUIRE((via.read(Via6522::REG_IER) & 0x02) != 0);

        via.write(Via6522::REG_IER, 0xC0);  // Set T1 enable
        REQUIRE((via.read(Via6522::REG_IER) & 0x40) != 0);
        REQUIRE((via.read(Via6522::REG_IER) & 0x02) != 0);  // CA1 still set
    }

    SECTION("IER bit 7 = 0 clears bits") {
        via.write(Via6522::REG_IER, 0xFF);  // Enable all
        REQUIRE((via.read(Via6522::REG_IER) & 0x7F) == 0x7F);

        via.write(Via6522::REG_IER, 0x02);  // Clear CA1 enable
        REQUIRE((via.read(Via6522::REG_IER) & 0x02) == 0);
        REQUIRE((via.read(Via6522::REG_IER) & 0x40) != 0);  // T1 still set
    }

    SECTION("IER bit 7 always reads as 1") {
        via.write(Via6522::REG_IER, 0x00);  // Clear all
        REQUIRE((via.read(Via6522::REG_IER) & 0x80) == 0x80);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Shift register tests (basic)
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 shift register", "[via][sr]") {
    Via6522 via;

    SECTION("SR can be written and read") {
        via.write(Via6522::REG_SR, 0xA5);
        REQUIRE(via.read(Via6522::REG_SR) == 0xA5);
    }
}

//////////////////////////////////////////////////////////////////////////////
// ACR/PCR tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 ACR", "[via][acr]") {
    Via6522 via;

    SECTION("ACR can be written and read") {
        via.write(Via6522::REG_ACR, 0xC0);  // T1 continuous + PB7 output
        REQUIRE(via.read(Via6522::REG_ACR) == 0xC0);
    }
}

TEST_CASE("Via6522 PCR", "[via][pcr]") {
    Via6522 via;

    SECTION("PCR can be written and read") {
        via.write(Via6522::REG_PCR, 0xEE);  // CA2/CB2 high
        REQUIRE(via.read(Via6522::REG_PCR) == 0xEE);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Timer latch tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 timer latches", "[via][timer]") {
    Via6522 via;

    SECTION("T1 latches can be written without starting timer") {
        via.write(Via6522::REG_T1LL, 0x12);
        via.write(Via6522::REG_T1LH, 0x34);

        REQUIRE(via.read(Via6522::REG_T1LL) == 0x12);
        REQUIRE(via.read(Via6522::REG_T1LH) == 0x34);

        // Timer should not have started (t1_pending should be false)
        REQUIRE_FALSE(via.state().t1_pending);
    }

    SECTION("Writing T1C-H starts timer") {
        via.write(Via6522::REG_T1LL, 0x56);
        via.write(Via6522::REG_T1CH, 0x78);  // This starts the timer

        REQUIRE(via.state().t1_pending);
        REQUIRE(via.state().t1_reload);
    }
}

//////////////////////////////////////////////////////////////////////////////
// peek() side-effect-free read tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Via6522 peek does not clear interrupt flags", "[via][peek]") {
    Via6522 via;

    SECTION("peek T1CL does not clear T1 interrupt flag") {
        // Set up timer to fire immediately
        via.write(Via6522::REG_T1LL, 0x01);
        via.write(Via6522::REG_T1LH, 0x00);
        via.write(Via6522::REG_T1CH, 0x00);  // Start timer with counter = 0x0001

        // Tick until timer fires
        for (int i = 0; i < 10; ++i) {
            via.tick_rising();
            via.tick_falling();
        }

        // Enable T1 interrupt
        via.write(Via6522::REG_IER, 0xC0);  // Set bit 6 (T1) with bit 7 high

        // Verify T1 interrupt is set
        REQUIRE((via.read(Via6522::REG_IFR) & 0x40) == 0x40);

        // peek should return same value as read but NOT clear the flag
        uint8_t peek_value = via.peek(Via6522::REG_T1CL);
        (void)peek_value;  // Suppress unused warning

        // IFR should still have T1 flag set
        REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x40);

        // Now read() should clear it
        (void)via.read(Via6522::REG_T1CL);
        REQUIRE((via.peek(Via6522::REG_IFR) & 0x40) == 0x00);
    }

    SECTION("peek T2CL does not clear T2 interrupt flag") {
        // Set up T2 timer to fire immediately
        via.write(Via6522::REG_T2CL, 0x01);
        via.write(Via6522::REG_T2CH, 0x00);  // Start timer with counter = 0x0001

        // Tick until timer fires
        for (int i = 0; i < 10; ++i) {
            via.tick_rising();
            via.tick_falling();
        }

        // Enable T2 interrupt
        via.write(Via6522::REG_IER, 0xA0);  // Set bit 5 (T2) with bit 7 high

        // Verify T2 interrupt is set
        REQUIRE((via.read(Via6522::REG_IFR) & 0x20) == 0x20);

        // peek should NOT clear the flag
        uint8_t peek_value = via.peek(Via6522::REG_T2CL);
        (void)peek_value;
        REQUIRE((via.peek(Via6522::REG_IFR) & 0x20) == 0x20);

        // read() should clear it
        (void)via.read(Via6522::REG_T2CL);
        REQUIRE((via.peek(Via6522::REG_IFR) & 0x20) == 0x00);
    }

    SECTION("peek returns same register values as read") {
        // Write some values
        via.write(Via6522::REG_DDRA, 0x55);
        via.write(Via6522::REG_DDRB, 0xAA);
        via.write(Via6522::REG_ACR, 0x12);
        via.write(Via6522::REG_PCR, 0x34);

        // peek should return same values
        REQUIRE(via.peek(Via6522::REG_DDRA) == 0x55);
        REQUIRE(via.peek(Via6522::REG_DDRB) == 0xAA);
        REQUIRE(via.peek(Via6522::REG_ACR) == 0x12);
        REQUIRE(via.peek(Via6522::REG_PCR) == 0x34);

        // IER bit 7 always reads as 1
        REQUIRE((via.peek(Via6522::REG_IER) & 0x80) == 0x80);
    }
}
