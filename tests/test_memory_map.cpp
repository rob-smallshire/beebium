#include <catch2/catch_test_macros.hpp>
#include <beebium/MemoryMap.hpp>
#include <beebium/BankBinding.hpp>
#include <beebium/ClockConcepts.hpp>
#include <beebium/devices/Ram.hpp>
#include <beebium/devices/Rom.hpp>
#include <beebium/devices/EmptySlot.hpp>
#include <beebium/devices/BankedMemory.hpp>
#include <beebium/Via6522.hpp>

using namespace beebium;

TEST_CASE("Ram device", "[memory_map][devices]") {
    Ram<256> ram;

    SECTION("Initialized to zero") {
        for (uint16_t i = 0; i < 256; ++i) {
            REQUIRE(ram.read(i) == 0);
        }
    }

    SECTION("Write and read back") {
        ram.write(0x42, 0xAB);
        REQUIRE(ram.read(0x42) == 0xAB);
    }

    SECTION("Initialize with fill value") {
        Ram<16> filled(0xFF);
        REQUIRE(filled.read(0) == 0xFF);
        REQUIRE(filled.read(15) == 0xFF);
    }

    SECTION("Load data") {
        uint8_t data[] = {0x11, 0x22, 0x33};
        ram.load(data, 3, 10);
        REQUIRE(ram.read(10) == 0x11);
        REQUIRE(ram.read(11) == 0x22);
        REQUIRE(ram.read(12) == 0x33);
    }
}

TEST_CASE("Rom device", "[memory_map][devices]") {
    Rom<256> rom;

    SECTION("Initialized to zero") {
        for (uint16_t i = 0; i < 256; ++i) {
            REQUIRE(rom.read(i) == 0);
        }
    }

    SECTION("Writes are ignored") {
        rom.write(0x42, 0xAB);
        REQUIRE(rom.read(0x42) == 0);
    }

    SECTION("Load data") {
        uint8_t data[] = {0x11, 0x22, 0x33, 0x44};
        rom.load(data, 4);
        REQUIRE(rom.read(0) == 0x11);
        REQUIRE(rom.read(3) == 0x44);

        // Write still ignored after load
        rom.write(0, 0xFF);
        REQUIRE(rom.read(0) == 0x11);
    }
}

TEST_CASE("MemoryMappedDevice concept", "[memory_map][concepts]") {
    STATIC_REQUIRE(MemoryMappedDevice<Ram<256>>);
    STATIC_REQUIRE(MemoryMappedDevice<Rom<256>>);
    STATIC_REQUIRE(MemoryMappedDevice<Via6522>);
}

TEST_CASE("ClockSubscriber concept", "[memory_map][concepts]") {
    STATIC_REQUIRE(ClockSubscriber<Via6522>);
}

TEST_CASE("Region binding", "[memory_map][region]") {
    Ram<256> ram;

    auto region = make_region<0x1000, 0x10FF>(ram);

    SECTION("Contains check") {
        REQUIRE(region.contains(0x1000));
        REQUIRE(region.contains(0x10FF));
        REQUIRE(region.contains(0x1050));
        REQUIRE_FALSE(region.contains(0x0FFF));
        REQUIRE_FALSE(region.contains(0x1100));
    }

    SECTION("Read and write through region") {
        region.write(0x1010, 0x42);
        REQUIRE(region.read(0x1010) == 0x42);

        // Verify offset calculation (0x1010 - 0x1000 = 0x10)
        REQUIRE(ram.read(0x10) == 0x42);
    }
}

TEST_CASE("Mirror policy", "[memory_map][region]") {
    Ram<16> ram;  // Only 16 bytes

    // Mirror with mask 0x0F (addresses 0-15 only)
    auto region = make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(ram);

    SECTION("Mirrored addresses map to same underlying memory") {
        region.write(0xFE40, 0x42);  // Offset 0, mask -> 0
        REQUIRE(region.read(0xFE50) == 0x42);  // Offset 0x10, mask -> 0

        region.write(0xFE51, 0xAB);  // Offset 0x11, mask -> 1
        REQUIRE(region.read(0xFE41) == 0xAB);  // Offset 1, mask -> 1
    }
}

TEST_CASE("MemoryMap single region", "[memory_map]") {
    Ram<256> ram;

    auto memory = MemoryMap{
        make_region<0x0000, 0x00FF>(ram)
    };

    SECTION("Read and write") {
        memory.write(0x0042, 0xAB);
        REQUIRE(memory.read(0x0042) == 0xAB);
    }

    SECTION("Unmapped address returns 0xFF") {
        REQUIRE(memory.read(0x0100) == 0xFF);
    }

    SECTION("Unmapped write is silently ignored") {
        memory.write(0x0100, 0x42);
        // No crash, no effect
    }
}

TEST_CASE("MemoryMap multiple regions", "[memory_map]") {
    Ram<256> ram;
    Rom<256> rom;

    // Load some data into ROM
    uint8_t rom_data[256];
    for (int i = 0; i < 256; ++i) rom_data[i] = 0xC0 + (i & 0x0F);
    rom.load(rom_data, 256);

    auto memory = MemoryMap{
        make_region<0x0000, 0x00FF>(ram),  // RAM at 0x0000-0x00FF
        make_region<0x8000, 0x80FF>(rom)   // ROM at 0x8000-0x80FF
    };

    SECTION("RAM is readable and writable") {
        memory.write(0x0010, 0x42);
        REQUIRE(memory.read(0x0010) == 0x42);
    }

    SECTION("ROM is readable") {
        REQUIRE(memory.read(0x8000) == 0xC0);
        REQUIRE(memory.read(0x8010) == 0xC0);
    }

    SECTION("ROM writes are ignored") {
        memory.write(0x8000, 0x00);
        REQUIRE(memory.read(0x8000) == 0xC0);
    }

    SECTION("Gap between regions returns 0xFF") {
        REQUIRE(memory.read(0x0100) == 0xFF);
        REQUIRE(memory.read(0x7FFF) == 0xFF);
    }
}

TEST_CASE("MemoryMap overlapping regions (first match wins)", "[memory_map]") {
    Ram<16> small_ram;
    Ram<4096> large_ram;

    // Small RAM at 0x0010-0x001F should be checked first
    auto memory = MemoryMap{
        make_region<0x0010, 0x001F>(small_ram),  // First: specific
        make_region<0x0000, 0x0FFF>(large_ram)   // Second: general
    };

    SECTION("Overlapping region uses first match") {
        memory.write(0x0010, 0x42);
        REQUIRE(small_ram.read(0) == 0x42);  // Wrote to small_ram
        REQUIRE(large_ram.read(0x10) == 0);  // large_ram unchanged
    }

    SECTION("Non-overlapping addresses use correct region") {
        memory.write(0x0000, 0xAB);
        REQUIRE(large_ram.read(0) == 0xAB);

        memory.write(0x0020, 0xCD);
        REQUIRE(large_ram.read(0x20) == 0xCD);
    }
}

// Custom device to test concept compliance
struct MockDevice {
    uint8_t value = 0;

    uint8_t read(uint16_t) const { return value; }
    void write(uint16_t, uint8_t v) { value = v; }
};

TEST_CASE("MemoryMap with custom device", "[memory_map]") {
    MockDevice device;

    auto memory = MemoryMap{
        make_region<0xFE00, 0xFEFF>(device)
    };

    SECTION("Custom device works through memory map") {
        memory.write(0xFE00, 0x42);
        REQUIRE(device.value == 0x42);
        REQUIRE(memory.read(0xFE00) == 0x42);
    }
}

TEST_CASE("MemoryMap BBC-like configuration", "[memory_map][integration]") {
    Ram<32768> main_ram;       // 32KB at 0x0000-0x7FFF
    Rom<16384> paged_rom;      // 16KB at 0x8000-0xBFFF
    Rom<16384> mos_rom;        // 16KB at 0xC000-0xFFFF
    MockDevice via;            // I/O device at 0xFE40-0xFE5F (16 regs, mirrored)

    // Load ROM data
    uint8_t paged_data[16384];
    uint8_t mos_data[16384];
    for (int i = 0; i < 16384; ++i) {
        paged_data[i] = 0x80;
        mos_data[i] = 0xC0;
    }
    paged_rom.load(paged_data, 16384);
    mos_rom.load(mos_data, 16384);

    // Order matters: VIA (specific) before MOS (general)
    // VIA region is 0xFE40-0xFE5F (32 bytes), but only 16 registers, so mirror with 0x0F
    auto memory = MemoryMap{
        make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(via),  // VIA (16 regs, mirrored over 32 bytes)
        make_region<0x0000, 0x7FFF>(main_ram),           // Main RAM
        make_region<0x8000, 0xBFFF>(paged_rom),          // Paged ROM
        make_region<0xC000, 0xFFFF>(mos_rom)             // MOS ROM
    };

    SECTION("RAM read/write") {
        memory.write(0x1234, 0x42);
        REQUIRE(memory.read(0x1234) == 0x42);
    }

    SECTION("Paged ROM read") {
        REQUIRE(memory.read(0x8000) == 0x80);
        REQUIRE(memory.read(0xBFFF) == 0x80);
    }

    SECTION("MOS ROM read") {
        REQUIRE(memory.read(0xC000) == 0xC0);
        REQUIRE(memory.read(0xFDFF) == 0xC0);  // Before VIA region
    }

    SECTION("VIA access (overlaps MOS ROM region but matches first)") {
        memory.write(0xFE40, 0x55);
        REQUIRE(via.value == 0x55);
        REQUIRE(memory.read(0xFE40) == 0x55);

        // Mirrored access
        memory.write(0xFE50, 0xAA);  // Mirrors to offset 0
        REQUIRE(via.value == 0xAA);
    }
}

TEST_CASE("EmptySlot", "[memory_map][devices]") {
    EmptySlot slot;

    SECTION("Returns 0xFF") {
        REQUIRE(slot.read(0) == 0xFF);
        REQUIRE(slot.read(0x3FFF) == 0xFF);
    }

    SECTION("Writes ignored") {
        slot.write(0, 0x42);
        REQUIRE(slot.read(0) == 0xFF);
    }

    SECTION("Is empty") {
        REQUIRE(EmptySlot::is_empty());
    }

    SECTION("Satisfies MemoryMappedDevice") {
        STATIC_REQUIRE(MemoryMappedDevice<EmptySlot>);
    }
}

TEST_CASE("Bank binding", "[memory_map][bank]") {
    Rom<16384> rom;
    Ram<16384> ram;

    SECTION("Bank index is compile-time constant") {
        auto binding = make_bank<5>(rom);
        STATIC_REQUIRE(binding.bank_index == 5);
    }

    SECTION("Read and write through binding") {
        auto binding = make_bank<0>(ram);
        binding.write(0x100, 0x42);
        REQUIRE(binding.read(0x100) == 0x42);
        REQUIRE(ram.read(0x100) == 0x42);
    }

    SECTION("ROM binding ignores writes") {
        uint8_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
        rom.load(data, 4);

        auto binding = make_bank<0>(rom);
        binding.write(0, 0x00);
        REQUIRE(binding.read(0) == 0xAA);
    }
}

TEST_CASE("BankedMemory", "[memory_map][banked]") {
    Rom<16384> rom0, rom1;
    Ram<16384> ram7;

    // Load data into devices before creating BankedMemory
    uint8_t rom0_data[4] = {0x00, 0x01, 0x02, 0x03};
    uint8_t rom1_data[4] = {0x10, 0x11, 0x12, 0x13};
    rom0.load(rom0_data, 4);
    rom1.load(rom1_data, 4);

    // Create BankedMemory with static bindings
    BankedMemory memory{
        make_bank<0>(rom0),
        make_bank<1>(rom1),
        make_bank<7>(ram7)
    };

    SECTION("Default bank is 0") {
        REQUIRE(memory.selected_bank() == 0);
    }

    SECTION("Populated banks are marked") {
        REQUIRE(memory.is_bank_populated(0));
        REQUIRE(memory.is_bank_populated(1));
        REQUIRE(memory.is_bank_populated(7));
        REQUIRE_FALSE(memory.is_bank_populated(2));
        REQUIRE_FALSE(memory.is_bank_populated(15));
    }

    SECTION("Unpopulated banks return 0xFF") {
        memory.select_bank(5);  // Not populated
        REQUIRE(memory.read(0) == 0xFF);
        REQUIRE(memory.read(0x3FFF) == 0xFF);
    }

    SECTION("Bank selection") {
        memory.select_bank(5);
        REQUIRE(memory.selected_bank() == 5);

        memory.select_bank(15);
        REQUIRE(memory.selected_bank() == 15);

        // Wraps around (masked to 0-15)
        memory.select_bank(16);
        REQUIRE(memory.selected_bank() == 0);
    }

    SECTION("Read from ROM bank") {
        memory.select_bank(0);
        REQUIRE(memory.read(0) == 0x00);
        REQUIRE(memory.read(3) == 0x03);

        memory.select_bank(1);
        REQUIRE(memory.read(0) == 0x10);
        REQUIRE(memory.read(3) == 0x13);
    }

    SECTION("ROM banks ignore writes") {
        memory.select_bank(0);
        memory.write(0, 0xFF);
        REQUIRE(memory.read(0) == 0x00);  // Unchanged
    }

    SECTION("RAM bank read and write") {
        memory.select_bank(7);
        memory.write(0x100, 0x42);
        REQUIRE(memory.read(0x100) == 0x42);
    }

    SECTION("Bank switching isolates banks") {
        // Write to RAM bank
        memory.select_bank(7);
        memory.write(0, 0x42);

        // ROM bank has different data
        memory.select_bank(0);
        REQUIRE(memory.read(0) == 0x00);

        // RAM bank still has written value
        memory.select_bank(7);
        REQUIRE(memory.read(0) == 0x42);
    }
}

TEST_CASE("BankedMemory with no bindings", "[memory_map][banked]") {
    BankedMemory<> empty_memory;

    SECTION("All banks return 0xFF") {
        for (uint8_t bank = 0; bank < 16; ++bank) {
            empty_memory.select_bank(bank);
            REQUIRE(empty_memory.read(0) == 0xFF);
        }
    }

    SECTION("No banks are populated") {
        for (uint8_t bank = 0; bank < 16; ++bank) {
            REQUIRE_FALSE(empty_memory.is_bank_populated(bank));
        }
    }
}

TEST_CASE("BankedMemory satisfies MemoryMappedDevice", "[memory_map][concepts]") {
    // Empty BankedMemory
    STATIC_REQUIRE(MemoryMappedDevice<BankedMemory<>>);

    // BankedMemory with bindings
    using TestBankedMemory = BankedMemory<
        decltype(make_bank<0>(std::declval<Rom<16384>&>()))
    >;
    STATIC_REQUIRE(MemoryMappedDevice<TestBankedMemory>);
}

// Include ModelBHardware for integration test
#include <beebium/ModelBHardware.hpp>

TEST_CASE("ModelBHardware", "[memory_map][integration]") {
    ModelBHardware hw;

    SECTION("RAM read/write at 0x0000-0x7FFF") {
        hw.write(0x1234, 0x42);
        REQUIRE(hw.read(0x1234) == 0x42);

        hw.write(0x0000, 0xAA);
        REQUIRE(hw.read(0x0000) == 0xAA);

        hw.write(0x7FFF, 0xBB);
        REQUIRE(hw.read(0x7FFF) == 0xBB);
    }

    SECTION("VIA access at 0xFE40-0xFE5F (System VIA)") {
        // Write to DDRA (register 3)
        hw.write(0xFE43, 0xFF);
        REQUIRE(hw.read(0xFE43) == 0xFF);

        // Mirrored access at 0xFE53
        hw.write(0xFE53, 0xAA);
        REQUIRE(hw.read(0xFE43) == 0xAA);  // Same register
    }

    SECTION("VIA access at 0xFE60-0xFE7F (User VIA)") {
        hw.write(0xFE62, 0x55);  // DDRB
        REQUIRE(hw.read(0xFE62) == 0x55);
    }

    SECTION("ROMSEL at 0xFE30 switches sideways banks") {
        // Load different data into banks 0 (basic) and 1 (dfs)
        uint8_t basic_data[4] = {0x00, 0x00, 0x00, 0x00};
        uint8_t dfs_data[4] = {0x11, 0x11, 0x11, 0x11};
        hw.load_basic(basic_data, 4);
        hw.load_dfs(dfs_data, 4);

        // Default is bank 0 (basic)
        REQUIRE(hw.read(0x8000) == 0x00);

        // Switch to bank 1 (dfs) via ROMSEL
        hw.write(0xFE30, 0x01);
        REQUIRE(hw.read(0x8000) == 0x11);

        // Switch back to bank 0 (basic)
        hw.write(0xFE30, 0x00);
        REQUIRE(hw.read(0x8000) == 0x00);

        // Empty bank (bank 2) returns 0xFF
        hw.write(0xFE30, 0x02);
        REQUIRE(hw.read(0x8000) == 0xFF);
    }

    SECTION("MOS ROM at 0xC000-0xFBFF") {
        uint8_t mos_data[16] = {0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
                                0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF};
        hw.load_mos(mos_data, 16);

        REQUIRE(hw.read(0xC000) == 0xC0);
        REQUIRE(hw.read(0xC00F) == 0xCF);
    }

    SECTION("Reset clears RAM and resets VIAs") {
        hw.write(0x1000, 0x42);
        hw.write(0xFE30, 0x05);  // Change bank

        hw.reset();

        REQUIRE(hw.read(0x1000) == 0x00);
        REQUIRE(hw.sideways.selected_bank() == 0);
    }

    SECTION("poll_irq aggregates VIA IRQs") {
        // Just verify it doesn't crash and returns something
        uint8_t irq0 = hw.poll_irq();
        (void)irq0;

        // VIAs can be ticked directly for testing
        hw.system_via.tick_falling();
        hw.user_via.tick_falling();
        uint8_t irq1 = hw.poll_irq();
        (void)irq1;
    }
}

TEST_CASE("ModelBHardware satisfies MemoryMappedDevice", "[memory_map][concepts]") {
    STATIC_REQUIRE(MemoryMappedDevice<ModelBHardware>);
}
