#include <catch2/catch_test_macros.hpp>
#include <beebium/MemoryHistogram.hpp>
#include <beebium/Machines.hpp>
#include <array>
#include <memory>

using namespace beebium;

TEST_CASE("MemoryHistogram basic operations", "[histogram][init]") {
    MemoryHistogram histogram;

    SECTION("Initially all counts are zero") {
        REQUIRE(histogram.reads(0x0000) == 0);
        REQUIRE(histogram.writes(0x0000) == 0);
        REQUIRE(histogram.reads(0xFFFF) == 0);
        REQUIRE(histogram.writes(0xFFFF) == 0);
        REQUIRE(histogram.total_reads() == 0);
        REQUIRE(histogram.total_writes() == 0);
    }

    SECTION("record_read increments read count") {
        histogram.record_read(0x1234);
        REQUIRE(histogram.reads(0x1234) == 1);
        REQUIRE(histogram.writes(0x1234) == 0);

        histogram.record_read(0x1234);
        REQUIRE(histogram.reads(0x1234) == 2);
    }

    SECTION("record_write increments write count") {
        histogram.record_write(0x5678);
        REQUIRE(histogram.writes(0x5678) == 1);
        REQUIRE(histogram.reads(0x5678) == 0);

        histogram.record_write(0x5678);
        REQUIRE(histogram.writes(0x5678) == 2);
    }

    SECTION("total returns sum of reads and writes") {
        histogram.record_read(0x1000);
        histogram.record_read(0x1000);
        histogram.record_write(0x1000);
        REQUIRE(histogram.total(0x1000) == 3);
    }

    SECTION("clear resets all counts") {
        histogram.record_read(0x0000);
        histogram.record_write(0xFFFF);
        histogram.clear();
        REQUIRE(histogram.reads(0x0000) == 0);
        REQUIRE(histogram.writes(0xFFFF) == 0);
    }
}

TEST_CASE("MemoryHistogram aggregate queries", "[histogram][queries]") {
    MemoryHistogram histogram;

    // Set up some test data
    for (int i = 0; i < 100; ++i) {
        histogram.record_read(0x1000);
    }
    for (int i = 0; i < 50; ++i) {
        histogram.record_write(0x2000);
    }
    histogram.record_read(0x3000);
    histogram.record_write(0x3000);

    SECTION("total_reads sums all read counts") {
        REQUIRE(histogram.total_reads() == 101);  // 100 + 1
    }

    SECTION("total_writes sums all write counts") {
        REQUIRE(histogram.total_writes() == 51);  // 50 + 1
    }

    SECTION("max_reads finds address with most reads") {
        auto [addr, count] = histogram.max_reads();
        REQUIRE(addr == 0x1000);
        REQUIRE(count == 100);
    }

    SECTION("max_writes finds address with most writes") {
        auto [addr, count] = histogram.max_writes();
        REQUIRE(addr == 0x2000);
        REQUIRE(count == 50);
    }

    SECTION("max_total finds address with most total accesses") {
        auto [addr, count] = histogram.max_total();
        REQUIRE(addr == 0x1000);
        REQUIRE(count == 100);
    }

    SECTION("active_addresses counts non-zero addresses") {
        REQUIRE(histogram.active_addresses() == 3);
    }
}

TEST_CASE("MemoryHistogram attach to Machine", "[histogram][machine]") {
    ModelB machine;
    auto histogram = std::make_unique<MemoryHistogram>();

    // Create a minimal MOS with reset vector pointing to 0x0400
    std::array<uint8_t, 16384> mos{};
    std::fill(mos.begin(), mos.end(), 0xEA);  // Fill with NOPs
    mos[0x3FFC] = 0x00;  // Reset vector low byte
    mos[0x3FFD] = 0x04;  // Reset vector high byte -> 0x0400
    machine.memory().load_mos(mos.data(), mos.size());

    // Simple program at 0x0400: LDA #$42, STA $1000, NOP
    machine.write(0x0400, 0xA9);  // LDA #
    machine.write(0x0401, 0x42);  // $42
    machine.write(0x0402, 0x8D);  // STA abs
    machine.write(0x0403, 0x00);  // $1000 low
    machine.write(0x0404, 0x10);  // $1000 high
    machine.write(0x0405, 0xEA);  // NOP

    // Attach histogram AFTER writing program
    histogram->attach(machine);

    // Reset CPU (not full machine reset)
    M6502_Reset(&machine.cpu());

    SECTION("Histogram records memory accesses during execution") {
        // Execute reset sequence (7 cycles)
        machine.step_instruction();

        // Execute LDA #$42 (2 cycles, reads 2 bytes)
        machine.step_instruction();

        // Execute STA $1000 (4 cycles, reads 3 bytes, writes 1 byte)
        machine.step_instruction();

        // Check that histogram recorded accesses
        REQUIRE(histogram->total_reads() > 0);
        REQUIRE(histogram->total_writes() > 0);

        // The STA should have written to 0x1000
        REQUIRE(histogram->writes(0x1000) == 1);
    }

    SECTION("clear_watchpoints disables histogram") {
        machine.step_instruction();  // Reset sequence
        uint64_t reads_before = histogram->total_reads();

        machine.clear_watchpoints();
        machine.step_instruction();  // Execute with no watchpoints

        // No new accesses should be recorded
        REQUIRE(histogram->total_reads() == reads_before);
    }
}

TEST_CASE("Watchpoint full address range via CPU execution", "[watchpoint][range]") {
    ModelB machine;

    // Create a minimal MOS with reset vector pointing to 0x0400
    std::array<uint8_t, 16384> mos{};
    std::fill(mos.begin(), mos.end(), 0xEA);  // Fill with NOPs
    mos[0x3FFC] = 0x00;  // Reset vector low byte
    mos[0x3FFD] = 0x04;  // Reset vector high byte -> 0x0400
    machine.memory().load_mos(mos.data(), mos.size());

    // LDA $1000 (reads from 0x1000) followed by STA $2000 (writes to 0x2000)
    machine.write(0x0400, 0xAD);  // LDA abs
    machine.write(0x0401, 0x00);  // $1000 low
    machine.write(0x0402, 0x10);  // $1000 high
    machine.write(0x0403, 0x8D);  // STA abs
    machine.write(0x0404, 0x00);  // $2000 low
    machine.write(0x0405, 0x20);  // $2000 high
    machine.write(0x0406, 0xEA);  // NOP

    uint32_t read_1000_count = 0;
    uint32_t write_2000_count = 0;

    // Add watchpoints AFTER writing program (convenience writes don't trigger watchpoints)
    machine.add_watchpoint(0x1000, 1, WATCH_READ,
        [&read_1000_count](uint16_t, uint8_t, bool, uint64_t) {
            ++read_1000_count;
        });

    machine.add_watchpoint(0x2000, 1, WATCH_WRITE,
        [&write_2000_count](uint16_t, uint8_t, bool, uint64_t) {
            ++write_2000_count;
        });

    M6502_Reset(&machine.cpu());

    // Execute reset sequence
    machine.step_instruction();

    SECTION("Watchpoint fires on CPU read") {
        // Execute LDA $1000
        machine.step_instruction();
        REQUIRE(read_1000_count == 1);
    }

    SECTION("Watchpoint fires on CPU write") {
        // Execute LDA $1000, then STA $2000
        machine.step_instruction();
        machine.step_instruction();
        REQUIRE(write_2000_count == 1);
    }
}
