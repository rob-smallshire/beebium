#include <catch2/catch_test_macros.hpp>
#include <beebium/ModelBMemory.hpp>
#include <array>
#include <cstring>

using namespace beebium;

TEST_CASE("ModelBMemory initialization", "[memory][init]") {
    ModelBMemory mem;

    SECTION("RAM is zeroed on reset") {
        for (uint16_t addr = 0x0000; addr < 0x8000; ++addr) {
            REQUIRE(mem.read(addr) == 0x00);
        }
    }

    SECTION("ROM areas return 0xFF initially (empty EPROM)") {
        // Paged ROM area
        for (uint16_t addr = 0x8000; addr < 0xC000; ++addr) {
            REQUIRE(mem.read(addr) == 0xFF);
        }

        // MOS area (excluding I/O)
        for (uint16_t addr = 0xC000; addr < 0xFE00; ++addr) {
            REQUIRE(mem.read(addr) == 0xFF);
        }
    }

    SECTION("Default ROM bank is 0") {
        REQUIRE(mem.rom_bank().value == 0);
    }
}

TEST_CASE("ModelBMemory RAM read/write", "[memory][ram]") {
    ModelBMemory mem;

    SECTION("Write and read back single byte") {
        mem.write(0x1234, 0xAB);
        REQUIRE(mem.read(0x1234) == 0xAB);
    }

    SECTION("Write to zero page") {
        mem.write(0x00, 0x12);
        mem.write(0xFF, 0x34);
        REQUIRE(mem.read(0x00) == 0x12);
        REQUIRE(mem.read(0xFF) == 0x34);
    }

    SECTION("Write to stack page") {
        mem.write(0x01FF, 0x42);
        mem.write(0x0100, 0x24);
        REQUIRE(mem.read(0x01FF) == 0x42);
        REQUIRE(mem.read(0x0100) == 0x24);
    }

    SECTION("Write entire RAM range") {
        for (uint16_t addr = 0x0000; addr < 0x8000; ++addr) {
            mem.write(addr, static_cast<uint8_t>(addr & 0xFF));
        }
        for (uint16_t addr = 0x0000; addr < 0x8000; ++addr) {
            REQUIRE(mem.read(addr) == static_cast<uint8_t>(addr & 0xFF));
        }
    }
}

TEST_CASE("ModelBMemory ROM is read-only", "[memory][rom]") {
    ModelBMemory mem;

    // Load some test data into MOS
    std::array<uint8_t, 16384> mos_data;
    std::fill(mos_data.begin(), mos_data.end(), 0x42);
    mem.load_mos(mos_data.data(), mos_data.size());

    SECTION("MOS ROM can be read") {
        REQUIRE(mem.read(0xC000) == 0x42);
        REQUIRE(mem.read(0xFFFF) == 0x42);
    }

    SECTION("Writes to MOS ROM are ignored") {
        mem.write(0xC000, 0x00);
        REQUIRE(mem.read(0xC000) == 0x42);
    }

    // Load some test data into ROM bank 0
    std::array<uint8_t, 16384> rom_data;
    std::fill(rom_data.begin(), rom_data.end(), 0x24);
    mem.load_rom_bank(RomBankIndex{0}, rom_data.data(), rom_data.size());

    SECTION("Paged ROM can be read") {
        REQUIRE(mem.read(0x8000) == 0x24);
        REQUIRE(mem.read(0xBFFF) == 0x24);
    }

    SECTION("Writes to paged ROM are ignored") {
        mem.write(0x8000, 0x00);
        REQUIRE(mem.read(0x8000) == 0x24);
    }
}

TEST_CASE("ModelBMemory ROM bank switching", "[memory][rom][banking]") {
    ModelBMemory mem;

    // Load different data into different ROM banks
    std::array<uint8_t, 16384> bank0_data, bank1_data, bank15_data;
    std::fill(bank0_data.begin(), bank0_data.end(), 0x00);
    std::fill(bank1_data.begin(), bank1_data.end(), 0x11);
    std::fill(bank15_data.begin(), bank15_data.end(), 0xFF);

    mem.load_rom_bank(RomBankIndex{0}, bank0_data.data(), bank0_data.size());
    mem.load_rom_bank(RomBankIndex{1}, bank1_data.data(), bank1_data.size());
    mem.load_rom_bank(RomBankIndex{15}, bank15_data.data(), bank15_data.size());

    SECTION("Default bank is 0") {
        REQUIRE(mem.read(0x8000) == 0x00);
    }

    SECTION("Switch to bank 1") {
        mem.set_rom_bank(RomBankIndex{1});
        REQUIRE(mem.rom_bank().value == 1);
        REQUIRE(mem.read(0x8000) == 0x11);
    }

    SECTION("Switch to bank 15") {
        mem.set_rom_bank(RomBankIndex{15});
        REQUIRE(mem.rom_bank().value == 15);
        REQUIRE(mem.read(0x8000) == 0xFF);
    }

    SECTION("ROMSEL write switches bank") {
        // Writing to ROMSEL (0xFE30) should switch ROM bank
        mem.write(0xFE30, 1);
        REQUIRE(mem.rom_bank().value == 1);
        REQUIRE(mem.read(0x8000) == 0x11);

        mem.write(0xFE30, 15);
        REQUIRE(mem.rom_bank().value == 15);
        REQUIRE(mem.read(0x8000) == 0xFF);
    }
}

TEST_CASE("ModelBMemory I/O address handling", "[memory][io]") {
    ModelBMemory mem;

    SECTION("I/O reads return 0xFF by default") {
        REQUIRE(mem.read(0xFE00) == 0xFF);
        REQUIRE(mem.read(0xFE30) == 0xFF);
        REQUIRE(mem.read(0xFEFF) == 0xFF);
    }

    SECTION("I/O read callback is called") {
        bool callback_called = false;
        uint16_t callback_addr = 0;

        mem.set_io_read_callback([&](uint16_t addr) -> uint8_t {
            callback_called = true;
            callback_addr = addr;
            return 0x42;
        });

        uint8_t result = mem.read(0xFE40);
        REQUIRE(callback_called);
        REQUIRE(callback_addr == 0xFE40);
        REQUIRE(result == 0x42);
    }

    SECTION("I/O write callback is called") {
        bool callback_called = false;
        uint16_t callback_addr = 0;
        uint8_t callback_value = 0;

        mem.set_io_write_callback([&](uint16_t addr, uint8_t value) {
            callback_called = true;
            callback_addr = addr;
            callback_value = value;
        });

        mem.write(0xFE40, 0xAB);
        REQUIRE(callback_called);
        REQUIRE(callback_addr == 0xFE40);
        REQUIRE(callback_value == 0xAB);
    }
}

TEST_CASE("ModelBMemory page access", "[memory][pages]") {
    ModelBMemory mem;

    SECTION("Page count is correct") {
        REQUIRE(kNumPages == 16);
    }

    SECTION("RAM pages have read and write pointers") {
        for (uint8_t i = 0; i < 8; ++i) {
            const auto& page = mem.page(PageIndex{i});
            REQUIRE(page.read != nullptr);
            REQUIRE(page.write != nullptr);
            REQUIRE(page.index.value == i);
        }
    }

    SECTION("ROM pages have read pointer only") {
        for (uint8_t i = 8; i < 16; ++i) {
            const auto& page = mem.page(PageIndex{i});
            REQUIRE(page.read != nullptr);
            // Write pointer goes to discard buffer (not nullptr)
            REQUIRE(page.index.value == i);
        }
    }
}

TEST_CASE("ModelBMemory reset", "[memory][reset]") {
    ModelBMemory mem;

    // Write some data
    mem.write(0x1000, 0x42);
    mem.set_rom_bank(RomBankIndex{5});

    // Reset
    mem.reset();

    SECTION("RAM is cleared on reset") {
        REQUIRE(mem.read(0x1000) == 0x00);
    }

    SECTION("ROM bank is reset to 0") {
        REQUIRE(mem.rom_bank().value == 0);
    }
}

TEST_CASE("PageIndex from_address", "[memory][types]") {
    REQUIRE(PageIndex::from_address(0x0000).value == 0);
    REQUIRE(PageIndex::from_address(0x0FFF).value == 0);
    REQUIRE(PageIndex::from_address(0x1000).value == 1);
    REQUIRE(PageIndex::from_address(0x8000).value == 8);
    REQUIRE(PageIndex::from_address(0xFFFF).value == 15);
}
