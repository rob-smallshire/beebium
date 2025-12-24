// test_addressable_latch.cpp
// Behaviour tests for IC32 addressable latch (74LS259)
//
// Tests the externally observable contract:
// - Setting/clearing individual bits via 3-bit address + data
// - Query methods for screen base, LED state, enable signals
// - Reset behaviour

#include <beebium/AddressableLatch.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;

TEST_CASE("AddressableLatch bit addressing", "[addressable_latch]") {
    AddressableLatch latch;

    SECTION("write to each bit position sets only that bit") {
        for (uint8_t addr = 0; addr < 8; ++addr) {
            latch.reset();
            latch.write(addr, true);
            REQUIRE(latch.value == (1 << addr));
        }
    }

    SECTION("clearing a bit leaves others unchanged") {
        latch.value = 0xFF;  // All bits set
        latch.write(3, false);  // Clear bit 3
        REQUIRE(latch.value == 0xF7);
    }

    SECTION("address wraps at 3 bits") {
        latch.reset();
        latch.write(8, true);   // Should write to bit 0 (8 & 0x07 = 0)
        REQUIRE(latch.value == 0x01);

        latch.reset();
        latch.write(15, true);  // Should write to bit 7 (15 & 0x07 = 7)
        REQUIRE(latch.value == 0x80);
    }

    SECTION("multiple writes accumulate state") {
        latch.reset();
        latch.write(0, true);
        latch.write(2, true);
        latch.write(4, true);
        REQUIRE(latch.value == 0x15);  // bits 0, 2, 4
    }
}

TEST_CASE("AddressableLatch screen base output", "[addressable_latch]") {
    AddressableLatch latch;

    SECTION("screen base reflects bits 4-5") {
        latch.reset();
        REQUIRE(latch.screen_base() == 0);

        latch.write(4, true);  // Set bit 4
        REQUIRE(latch.screen_base() == 1);

        latch.write(5, true);  // Set bit 5
        REQUIRE(latch.screen_base() == 3);

        latch.write(4, false);  // Clear bit 4
        REQUIRE(latch.screen_base() == 2);
    }

    SECTION("screen base ignores other bits") {
        latch.value = 0xFF;  // All bits set
        REQUIRE(latch.screen_base() == 3);  // Only bits 4-5 matter

        latch.value = 0x30;  // Only bits 4-5
        REQUIRE(latch.screen_base() == 3);
    }
}

TEST_CASE("AddressableLatch LED outputs", "[addressable_latch]") {
    AddressableLatch latch;

    SECTION("caps lock LED reflects bit 6") {
        latch.reset();
        REQUIRE(latch.caps_lock_led() == false);

        latch.write(6, true);
        REQUIRE(latch.caps_lock_led() == true);

        latch.write(6, false);
        REQUIRE(latch.caps_lock_led() == false);
    }

    SECTION("shift lock LED reflects bit 7") {
        latch.reset();
        REQUIRE(latch.shift_lock_led() == false);

        latch.write(7, true);
        REQUIRE(latch.shift_lock_led() == true);

        latch.write(7, false);
        REQUIRE(latch.shift_lock_led() == false);
    }
}

TEST_CASE("AddressableLatch active-low signals", "[addressable_latch]") {
    AddressableLatch latch;

    SECTION("sound write enabled when bit 0 is clear (active low)") {
        latch.reset();
        REQUIRE(latch.sound_write_enabled() == true);  // Bit 0 clear = enabled

        latch.write(0, true);
        REQUIRE(latch.sound_write_enabled() == false);  // Bit 0 set = disabled

        latch.write(0, false);
        REQUIRE(latch.sound_write_enabled() == true);  // Bit 0 clear = enabled
    }

    SECTION("keyboard enabled when bit 3 is clear (active low)") {
        latch.reset();
        REQUIRE(latch.keyboard_enabled() == true);  // Bit 3 clear = enabled

        latch.write(3, true);
        REQUIRE(latch.keyboard_enabled() == false);  // Bit 3 set = disabled

        latch.write(3, false);
        REQUIRE(latch.keyboard_enabled() == true);  // Bit 3 clear = enabled
    }
}

TEST_CASE("AddressableLatch reset", "[addressable_latch]") {
    AddressableLatch latch;

    SECTION("reset clears all bits") {
        latch.value = 0xFF;
        latch.reset();
        REQUIRE(latch.value == 0);
    }

    SECTION("after reset, all outputs reflect cleared state") {
        latch.value = 0xFF;
        latch.reset();

        REQUIRE(latch.screen_base() == 0);
        REQUIRE(latch.caps_lock_led() == false);
        REQUIRE(latch.shift_lock_led() == false);
        REQUIRE(latch.sound_write_enabled() == true);  // Active low
        REQUIRE(latch.keyboard_enabled() == true);      // Active low
    }
}
