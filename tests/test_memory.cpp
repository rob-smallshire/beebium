#include <catch2/catch_test_macros.hpp>
#include <beebium/ModelBHardware.hpp>
#include <beebium/Via6522.hpp>
#include <array>

using namespace beebium;

TEST_CASE("ModelBHardware initialization", "[memory][init]") {
    ModelBHardware hw;

    SECTION("RAM is zeroed on construction") {
        for (uint16_t addr = 0x0000; addr < 0x8000; ++addr) {
            REQUIRE(hw.read(addr) == 0x00);
        }
    }

    SECTION("ROM areas return 0x00 initially (unloaded ROMs)") {
        // Paged ROM area (bank 0 - basic_rom, unloaded)
        REQUIRE(hw.read(0x8000) == 0x00);

        // MOS area (excluding I/O at 0xFE00-0xFEFF)
        for (uint16_t addr = 0xC000; addr < 0xFE00; ++addr) {
            REQUIRE(hw.read(addr) == 0x00);  // Unloaded ROM is zero
        }
    }

    SECTION("Empty banks return 0xFF") {
        // Bank 2 is not populated, should return 0xFF
        hw.write(0xFE30, 2);  // Select bank 2
        REQUIRE(hw.read(0x8000) == 0xFF);
    }

    SECTION("Default ROM bank is 0") {
        REQUIRE(hw.sideways.selected_bank() == 0);
    }
}

TEST_CASE("ModelBHardware RAM read/write", "[memory][ram]") {
    ModelBHardware hw;

    SECTION("Write and read back single byte") {
        hw.write(0x1234, 0xAB);
        REQUIRE(hw.read(0x1234) == 0xAB);
    }

    SECTION("Write to zero page") {
        hw.write(0x00, 0x12);
        hw.write(0xFF, 0x34);
        REQUIRE(hw.read(0x00) == 0x12);
        REQUIRE(hw.read(0xFF) == 0x34);
    }

    SECTION("Write to stack page") {
        hw.write(0x01FF, 0x42);
        hw.write(0x0100, 0x24);
        REQUIRE(hw.read(0x01FF) == 0x42);
        REQUIRE(hw.read(0x0100) == 0x24);
    }

    SECTION("Write entire RAM range") {
        for (uint16_t addr = 0x0000; addr < 0x8000; ++addr) {
            hw.write(addr, static_cast<uint8_t>(addr & 0xFF));
        }
        for (uint16_t addr = 0x0000; addr < 0x8000; ++addr) {
            REQUIRE(hw.read(addr) == static_cast<uint8_t>(addr & 0xFF));
        }
    }
}

TEST_CASE("ModelBHardware ROM is read-only", "[memory][rom]") {
    ModelBHardware hw;

    // Load some test data into MOS
    std::array<uint8_t, 16384> mos_data;
    std::fill(mos_data.begin(), mos_data.end(), 0x42);
    hw.load_mos(mos_data.data(), mos_data.size());

    SECTION("MOS ROM can be read") {
        REQUIRE(hw.read(0xC000) == 0x42);
        // Note: 0xFExx is I/O region, not MOS ROM
        REQUIRE(hw.read(0xFDFF) == 0x42);
    }

    SECTION("Writes to MOS ROM are ignored") {
        hw.write(0xC000, 0x00);
        REQUIRE(hw.read(0xC000) == 0x42);
    }

    // Load some test data into ROM bank 0 (basic_rom)
    std::array<uint8_t, 16384> rom_data;
    std::fill(rom_data.begin(), rom_data.end(), 0x24);
    hw.load_basic(rom_data.data(), rom_data.size());

    SECTION("Paged ROM can be read") {
        REQUIRE(hw.read(0x8000) == 0x24);
        REQUIRE(hw.read(0xBFFF) == 0x24);
    }

    SECTION("Writes to paged ROM are ignored") {
        hw.write(0x8000, 0x00);
        REQUIRE(hw.read(0x8000) == 0x24);
    }
}

TEST_CASE("ModelBHardware ROM bank switching", "[memory][rom][banking]") {
    ModelBHardware hw;

    // Load different data into ROM banks (bank 0 = basic, bank 1 = dfs)
    std::array<uint8_t, 16384> basic_data, dfs_data;
    std::fill(basic_data.begin(), basic_data.end(), 0x00);
    std::fill(dfs_data.begin(), dfs_data.end(), 0x11);

    hw.load_basic(basic_data.data(), basic_data.size());
    hw.load_dfs(dfs_data.data(), dfs_data.size());

    SECTION("Default bank is 0 (basic)") {
        REQUIRE(hw.read(0x8000) == 0x00);
    }

    SECTION("Switch to bank 1 (dfs) via sideways") {
        hw.sideways.select_bank(1);
        REQUIRE(hw.sideways.selected_bank() == 1);
        REQUIRE(hw.read(0x8000) == 0x11);
    }

    SECTION("Empty bank returns 0xFF") {
        hw.sideways.select_bank(15);  // Bank 15 is not populated
        REQUIRE(hw.sideways.selected_bank() == 15);
        REQUIRE(hw.read(0x8000) == 0xFF);
    }

    SECTION("ROMSEL write switches bank") {
        // Writing to ROMSEL (0xFE30) should switch ROM bank
        hw.write(0xFE30, 1);
        REQUIRE(hw.sideways.selected_bank() == 1);
        REQUIRE(hw.read(0x8000) == 0x11);

        // Switching to empty bank
        hw.write(0xFE30, 15);
        REQUIRE(hw.sideways.selected_bank() == 15);
        REQUIRE(hw.read(0x8000) == 0xFF);
    }

    SECTION("Sideways RAM at bank 4 is writable") {
        hw.write(0xFE30, 4);  // Select bank 4 (sideways RAM)
        hw.write(0x8000, 0x42);
        REQUIRE(hw.read(0x8000) == 0x42);

        // Switch back to ROM and verify it's different
        hw.write(0xFE30, 0);
        REQUIRE(hw.read(0x8000) == 0x00);  // Basic ROM
    }
}

TEST_CASE("ModelBHardware I/O address handling", "[memory][io]") {
    ModelBHardware hw;

    SECTION("ROMSEL read returns 0xFF (write-only)") {
        REQUIRE(hw.read(0xFE30) == 0xFF);
    }

    SECTION("VIA addresses are handled directly") {
        // System VIA at FE40 - write DDRA (register 3)
        hw.system_via.write(Via6522::REG_DDRA, 0xFF);
        REQUIRE(hw.read(0xFE43) == 0xFF);

        // User VIA at FE60 - write DDRB (register 2)
        hw.user_via.write(Via6522::REG_DDRB, 0xAA);
        REQUIRE(hw.read(0xFE62) == 0xAA);
    }

    SECTION("VIA mirroring works") {
        // System VIA mirrors at 0xFE50
        hw.write(0xFE43, 0x55);  // DDRA at base
        REQUIRE(hw.read(0xFE53) == 0x55);  // Mirrored

        // User VIA mirrors at 0xFE70
        hw.write(0xFE62, 0xAA);  // DDRB at base
        REQUIRE(hw.read(0xFE72) == 0xAA);  // Mirrored
    }
}

TEST_CASE("ModelBHardware reset", "[memory][reset]") {
    ModelBHardware hw;

    // Write some data
    hw.write(0x1000, 0x42);
    hw.write(0xFE30, 5);  // Change ROM bank

    // Reset
    hw.reset();

    SECTION("RAM is cleared on reset") {
        REQUIRE(hw.read(0x1000) == 0x00);
    }

    SECTION("ROM bank is reset to 0") {
        REQUIRE(hw.sideways.selected_bank() == 0);
    }
}

TEST_CASE("ModelBHardware direct device access", "[memory][devices]") {
    ModelBHardware hw;

    SECTION("Can access main_ram directly") {
        hw.main_ram.write(0x100, 0x42);
        REQUIRE(hw.read(0x100) == 0x42);
    }

    SECTION("Can access system_via directly") {
        hw.system_via.write(Via6522::REG_DDRA, 0xFF);
        hw.system_via.write(Via6522::REG_ORA, 0x55);
        // Read back via memory map
        REQUIRE((hw.read(0xFE41) & 0xFF) != 0);
    }

    SECTION("Can access sideways directly") {
        // Load data into basic_rom (bank 0) via direct access
        uint8_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
        hw.basic_rom.load(data, 4);
        hw.sideways.select_bank(0);
        REQUIRE(hw.read(0x8000) == 0xAA);
    }

    SECTION("Can write to sideways_ram directly") {
        hw.sideways_ram.write(0x100, 0x42);
        hw.sideways.select_bank(4);  // Select sideways RAM bank
        REQUIRE(hw.read(0x8100) == 0x42);
    }
}

TEST_CASE("ModelBHardware peripheral clocking", "[memory][peripherals]") {
    ModelBHardware hw;

    SECTION("tick_peripherals runs without crash") {
        for (uint64_t cycle = 0; cycle < 100; ++cycle) {
            hw.tick_peripherals(cycle);
        }
    }

    SECTION("VIA timers decrement over time") {
        // Set up a timer
        hw.system_via.write(Via6522::REG_T1LL, 0xFF);
        hw.system_via.write(Via6522::REG_T1LH, 0x00);
        hw.system_via.write(Via6522::REG_T1CH, 0x00);  // Start timer at 0x00FF

        // Run some cycles
        for (uint64_t cycle = 0; cycle < 10; ++cycle) {
            hw.tick_peripherals(cycle);
        }

        // Timer should have decremented
        uint8_t low = hw.system_via.read(Via6522::REG_T1CL);
        REQUIRE(low < 0xFF);
    }
}
