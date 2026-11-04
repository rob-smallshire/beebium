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
#include <beebium/devices/ConfigurableSlot.hpp>
#include <array>

using namespace beebium;

TEST_CASE("ConfigurableSlot default construction", "[configurable_slot][init]") {
    ConfigurableSlot slot;

    SECTION("Default type is Empty") {
        REQUIRE(slot.type() == SlotType::Empty);
        REQUIRE(slot.is_empty());
        REQUIRE_FALSE(slot.is_rom());
        REQUIRE_FALSE(slot.is_ram());
    }

    SECTION("Empty slot is not populated") {
        REQUIRE_FALSE(slot.is_populated());
    }

    SECTION("Empty slot is not writable") {
        REQUIRE_FALSE(slot.is_writable());
    }
}

TEST_CASE("ConfigurableSlot empty behavior", "[configurable_slot][empty]") {
    ConfigurableSlot slot(SlotType::Empty);

    SECTION("Read returns 0xFF") {
        REQUIRE(slot.read(0x0000) == 0xFF);
        REQUIRE(slot.read(0x2000) == 0xFF);
        REQUIRE(slot.read(0x3FFF) == 0xFF);
    }

    SECTION("Write is ignored") {
        slot.write(0x0000, 0x42);
        REQUIRE(slot.read(0x0000) == 0xFF);
    }

    SECTION("Not populated") {
        REQUIRE_FALSE(slot.is_populated());
    }
}

TEST_CASE("ConfigurableSlot ROM behavior", "[configurable_slot][rom]") {
    ConfigurableSlot slot(SlotType::Rom);

    SECTION("Read returns data from slot") {
        // Load test data
        std::array<uint8_t, 16384> rom_data;
        rom_data.fill(0x42);
        slot.load(rom_data.data(), rom_data.size());

        REQUIRE(slot.read(0x0000) == 0x42);
        REQUIRE(slot.read(0x3FFF) == 0x42);
    }

    SECTION("Write is ignored") {
        std::array<uint8_t, 16384> rom_data;
        rom_data.fill(0x42);
        slot.load(rom_data.data(), rom_data.size());

        slot.write(0x0000, 0x00);
        REQUIRE(slot.read(0x0000) == 0x42);
    }

    SECTION("Populated after loading data") {
        std::array<uint8_t, 16384> rom_data;
        rom_data.fill(0x42);
        slot.load(rom_data.data(), rom_data.size());

        REQUIRE(slot.is_populated());
    }

    SECTION("Not populated if all 0xFF") {
        // Slot defaults to 0xFF, so if not loaded it's "not populated"
        REQUIRE_FALSE(slot.is_populated());
    }

    SECTION("ROM is not writable") {
        REQUIRE_FALSE(slot.is_writable());
    }
}

TEST_CASE("ConfigurableSlot RAM behavior", "[configurable_slot][ram]") {
    ConfigurableSlot slot(SlotType::Ram);

    SECTION("Read returns data from slot") {
        slot.write(0x0000, 0x42);
        REQUIRE(slot.read(0x0000) == 0x42);
    }

    SECTION("Write updates data") {
        slot.write(0x1234, 0xAB);
        slot.write(0x5678, 0xCD);

        REQUIRE(slot.read(0x1234) == 0xAB);
        REQUIRE(slot.read(0x5678) == 0xCD);
    }

    SECTION("RAM is writable") {
        REQUIRE(slot.is_writable());
    }

    SECTION("Freshly fitted RAM is blank and not populated") {
        REQUIRE(slot.read(0x0000) == 0x00);
        REQUIRE_FALSE(slot.is_populated());
    }

    SECTION("Populated after writing") {
        slot.write(0x0000, 0x42);
        REQUIRE(slot.is_populated());
    }

    SECTION("Populated after writing 0xFF") {
        slot.write(0x2000, 0xFF);
        REQUIRE(slot.is_populated());
    }

    SECTION("Can load pre-initialised data") {
        std::array<uint8_t, 16384> ram_data;
        ram_data.fill(0x55);
        slot.load(ram_data.data(), ram_data.size());

        REQUIRE(slot.read(0x0000) == 0x55);
        REQUIRE(slot.read(0x3FFF) == 0x55);
    }

    SECTION("Can write after loading") {
        std::array<uint8_t, 16384> ram_data;
        ram_data.fill(0x55);
        slot.load(ram_data.data(), ram_data.size());

        slot.write(0x0000, 0xAA);
        REQUIRE(slot.read(0x0000) == 0xAA);
        REQUIRE(slot.read(0x0001) == 0x55);
    }
}

TEST_CASE("ConfigurableSlot type switching", "[configurable_slot][type]") {
    ConfigurableSlot slot;

    SECTION("Can switch from Empty to ROM") {
        slot.set_type(SlotType::Rom);
        REQUIRE(slot.type() == SlotType::Rom);
        REQUIRE(slot.is_rom());
    }

    SECTION("Can switch from Empty to RAM") {
        slot.set_type(SlotType::Ram);
        REQUIRE(slot.type() == SlotType::Ram);
        REQUIRE(slot.is_ram());
    }

    SECTION("Switching from ROM to RAM clears data to 0x00") {
        slot.set_type(SlotType::Rom);
        std::array<uint8_t, 16384> data;
        data.fill(0x42);
        slot.load(data.data(), data.size());

        slot.set_type(SlotType::Ram);

        // Data is cleared to 0x00 (matches real hardware power-on state)
        REQUIRE(slot.read(0x0000) == 0x00);

        // Now writable
        slot.write(0x0000, 0xAA);
        REQUIRE(slot.read(0x0000) == 0xAA);
    }

    SECTION("Switching from RAM to ROM clears data to 0xFF") {
        slot.set_type(SlotType::Ram);
        slot.write(0x0000, 0x42);

        slot.set_type(SlotType::Rom);

        // Data is cleared to 0xFF (unpopulated socket behavior)
        REQUIRE(slot.read(0x0000) == 0xFF);

        // Now read-only
        slot.write(0x0000, 0xAA);
        REQUIRE(slot.read(0x0000) == 0xFF);
    }

    SECTION("Switching to Empty clears data to 0xFF") {
        slot.set_type(SlotType::Ram);
        slot.write(0x0000, 0x42);

        slot.set_type(SlotType::Empty);

        // Empty clears data and returns 0xFF
        REQUIRE(slot.read(0x0000) == 0xFF);
    }
}

TEST_CASE("ConfigurableSlot clear", "[configurable_slot][clear]") {
    SECTION("Clearing RAM fills with 0x00") {
        ConfigurableSlot slot(SlotType::Ram);
        slot.write(0x0000, 0x42);
        slot.write(0x3FFF, 0x42);

        slot.clear();

        REQUIRE(slot.read(0x0000) == 0x00);
        REQUIRE(slot.read(0x3FFF) == 0x00);
    }

    SECTION("Clearing ROM fills with 0xFF") {
        ConfigurableSlot slot(SlotType::Rom);
        std::array<uint8_t, 16384> data;
        data.fill(0x42);
        slot.load(data.data(), data.size());

        slot.clear();

        REQUIRE(slot.read(0x0000) == 0xFF);
        REQUIRE(slot.read(0x3FFF) == 0xFF);
    }

    SECTION("Clearing Empty fills with 0xFF") {
        ConfigurableSlot slot(SlotType::Empty);

        // Even though empty returns 0xFF anyway, clear should reset internal data
        slot.clear();

        // Switch to ROM to check internal data
        slot.set_type(SlotType::Rom);
        REQUIRE(slot.read(0x0000) == 0xFF);
    }
}

TEST_CASE("ConfigurableSlot image name tracking", "[configurable_slot][metadata]") {
    ConfigurableSlot slot(SlotType::Rom);

    SECTION("Initially empty") {
        REQUIRE(slot.image_name().empty());
    }

    SECTION("Can set image name") {
        slot.set_image_name("bbc-basic_2.rom");
        REQUIRE(slot.image_name() == "bbc-basic_2.rom");
    }

    SECTION("Can clear image name") {
        slot.set_image_name("test.rom");
        slot.clear_image_name();
        REQUIRE(slot.image_name().empty());
    }
}

TEST_CASE("ConfigurableSlot address wrapping", "[configurable_slot][addressing]") {
    ConfigurableSlot slot(SlotType::Ram);

    SECTION("Addresses wrap at 16KB boundary") {
        slot.write(0x0000, 0xAA);
        // Address 0x4000 should wrap to 0x0000 (0x4000 % 16384 = 0)
        REQUIRE(slot.read(0x4000) == 0xAA);

        slot.write(0x8000, 0xBB);
        // Address 0x8000 should wrap to 0x0000 (0x8000 % 16384 = 0)
        REQUIRE(slot.read(0x0000) == 0xBB);
    }
}

TEST_CASE("ConfigurableSlot load mirrors undersized data", "[configurable_slot][load]") {
    ConfigurableSlot slot(SlotType::Rom);

    SECTION("8 kB ROM mirrors into upper 8 kB") {
        std::array<uint8_t, 8192> rom_8k;
        for (size_t i = 0; i < rom_8k.size(); ++i) {
            rom_8k[i] = static_cast<uint8_t>(i & 0xFF);
        }

        slot.load(rom_8k.data(), rom_8k.size());

        // Lower 8 kB has original data
        REQUIRE(slot.read(0x0000) == 0x00);
        REQUIRE(slot.read(0x0001) == 0x01);
        REQUIRE(slot.read(0x1FFF) == 0xFF);

        // Upper 8 kB mirrors the lower 8 kB
        REQUIRE(slot.read(0x2000) == 0x00);
        REQUIRE(slot.read(0x2001) == 0x01);
        REQUIRE(slot.read(0x3FFF) == 0xFF);
    }

    SECTION("4 kB ROM mirrors four times") {
        std::array<uint8_t, 4096> rom_4k;
        for (size_t i = 0; i < rom_4k.size(); ++i) {
            rom_4k[i] = static_cast<uint8_t>(i & 0xFF);
        }

        slot.load(rom_4k.data(), rom_4k.size());

        // All four 4 kB regions should be identical
        for (uint16_t base : {0x0000, 0x1000, 0x2000, 0x3000}) {
            REQUIRE(slot.read(base) == 0x00);
            REQUIRE(slot.read(base + 1) == 0x01);
            REQUIRE(slot.read(base + 0x0FFF) == 0xFF);
        }
    }

    SECTION("2 kB ROM mirrors eight times") {
        std::array<uint8_t, 2048> rom_2k;
        for (size_t i = 0; i < rom_2k.size(); ++i) {
            rom_2k[i] = static_cast<uint8_t>(i & 0xFF);
        }

        slot.load(rom_2k.data(), rom_2k.size());

        for (uint16_t base = 0; base < 16384; base += 2048) {
            REQUIRE(slot.read(base) == 0x00);
            REQUIRE(slot.read(base + 1) == 0x01);
            REQUIRE(slot.read(base + 0x07FF) == 0xFF);
        }
    }

    SECTION("16 kB load fills slot exactly") {
        std::array<uint8_t, 16384> rom_16k;
        for (size_t i = 0; i < rom_16k.size(); ++i) {
            rom_16k[i] = static_cast<uint8_t>(i & 0xFF);
        }

        slot.load(rom_16k.data(), rom_16k.size());

        REQUIRE(slot.read(0x0000) == 0x00);
        REQUIRE(slot.read(0x0100) == 0x00);
        REQUIRE(slot.read(0x3FFF) == 0xFF);
    }

    SECTION("Non-power-of-two size mirrors cyclically") {
        std::array<uint8_t, 100> small_data;
        small_data.fill(0x42);

        slot.load(small_data.data(), small_data.size());

        REQUIRE(slot.read(0x0000) == 0x42);
        REQUIRE(slot.read(99) == 0x42);
        // Mirrored data continues beyond the original
        REQUIRE(slot.read(100) == 0x42);
        REQUIRE(slot.read(199) == 0x42);
    }

    SECTION("Empty load is a no-op") {
        // Pre-fill with known data
        std::array<uint8_t, 16384> data;
        data.fill(0x42);
        slot.load(data.data(), data.size());

        // Loading empty span should not change contents
        slot.load(static_cast<const uint8_t*>(nullptr), 0);

        REQUIRE(slot.read(0x0000) == 0x42);
        REQUIRE(slot.read(0x3FFF) == 0x42);
    }
}

TEST_CASE("ConfigurableSlot direct data access", "[configurable_slot][direct]") {
    ConfigurableSlot slot(SlotType::Ram);

    SECTION("Can access raw data pointer") {
        uint8_t* data = slot.data();
        REQUIRE(data != nullptr);

        data[0] = 0xAA;
        data[16383] = 0xBB;

        REQUIRE(slot.read(0) == 0xAA);
        REQUIRE(slot.read(16383) == 0xBB);
    }

    SECTION("Const access works") {
        slot.write(0, 0x42);
        const ConfigurableSlot& const_slot = slot;
        const uint8_t* data = const_slot.data();
        REQUIRE(data[0] == 0x42);
    }
}

TEST_CASE("ConfigurableSlot RAM fitted by set_type is blank and not populated",
          "[configurable_slot][ram][populated]") {
    ConfigurableSlot slot;
    slot.set_type(SlotType::Ram);
    REQUIRE_FALSE(slot.is_populated());

    slot.write(0x3FFF, 0x01);
    REQUIRE(slot.is_populated());

    slot.clear();
    REQUIRE_FALSE(slot.is_populated());
}
