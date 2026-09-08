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

#include <beebium/tube/TubeSocket.hpp>
#include <beebium/tube/TubeConcepts.hpp>
#include <beebium/tube/Coprocessor.hpp>
#include <beebium/IrqAggregator.hpp>
#include <beebium/MemoryMap.hpp>
#include <beebium/devices/Ram.hpp>

#include <cstdint>
#include <vector>

using namespace beebium;

namespace {

// Recording Coprocessor stub: logs the host_cycle argument of every run_until
// call and counts resets. Runs no cycles of its own -- the socket's only job
// here is to pass host time through and delegate reset.
class RecordingCoprocessor : public Coprocessor {
public:
    std::vector<uint64_t> run_until_args;
    int reset_count = 0;
    bool paused = false;

    void run_until(uint64_t host_cycle) override { run_until_args.push_back(host_cycle); }
    void pause() override { paused = true; }
    void resume() override { paused = false; }
    bool is_paused() const override { return paused; }
    void reset() override { ++reset_count; }
    ClockRatio clock_ratio() const override { return ClockRatio{3, 2}; }
};

}  // namespace

// ===========================================================================
// Empty socket behaviour (no second processor attached)
// ===========================================================================

TEST_CASE("TubeSocket: default-constructed socket is disabled", "[tube][socket]") {
    TubeSocket socket;
    CHECK_FALSE(socket.enabled());
}

TEST_CASE("TubeSocket: empty socket read returns 0xFF without bus value pointer", "[tube][socket]") {
    TubeSocket socket;
    // Without last_bus_value_ptr set, falls back to 0xFF
    for (uint16_t offset = 0; offset < 8; ++offset) {
        CHECK(socket.read(offset) == 0xFF);
    }
}

TEST_CASE("TubeSocket: empty socket read returns last bus value (2MHz open bus)", "[tube][socket]") {
    TubeSocket socket;
    uint8_t bus_value = 0x42;
    socket.set_last_bus_value_ptr(&bus_value);

    for (uint16_t offset = 0; offset < 8; ++offset) {
        CHECK(socket.read(offset) == 0x42);
    }

    // Bus value changes are reflected immediately
    bus_value = 0xA5;
    CHECK(socket.read(0) == 0xA5);
}

TEST_CASE("TubeSocket: empty socket write is harmless", "[tube][socket]") {
    TubeSocket socket;
    // Writing to all register offsets should not crash
    for (uint16_t offset = 0; offset < 8; ++offset) {
        socket.write(offset, 0x42);
    }
    CHECK_FALSE(socket.enabled());
}

TEST_CASE("TubeSocket: empty socket irq_pending returns false", "[tube][socket]") {
    TubeSocket socket;
    CHECK_FALSE(socket.irq_pending());
}

TEST_CASE("TubeSocket: empty socket reset is harmless", "[tube][socket]") {
    TubeSocket socket;
    socket.reset();
    CHECK_FALSE(socket.enabled());
}

// ===========================================================================
// Enable/disable
// ===========================================================================

TEST_CASE("TubeSocket: enable activates the socket", "[tube][socket]") {
    TubeSocket socket;
    socket.enable();
    CHECK(socket.enabled());
}

TEST_CASE("TubeSocket: disable returns to empty state", "[tube][socket]") {
    TubeSocket socket;
    socket.enable();
    CHECK(socket.enabled());

    socket.disable();
    CHECK_FALSE(socket.enabled());
}

TEST_CASE("TubeSocket: reads return last bus value after disable", "[tube][socket]") {
    TubeSocket socket;
    uint8_t bus_value = 0x73;
    socket.set_last_bus_value_ptr(&bus_value);
    socket.enable();

    // Write data through the Tube ULA so registers have state
    socket.write(1, 0x42);  // R1 H-to-P

    socket.disable();

    // All reads should return the last bus value now
    for (uint16_t offset = 0; offset < 8; ++offset) {
        CHECK(socket.read(offset) == 0x73);
    }
}

// ===========================================================================
// Delegation to TubeUla host-side interface
// ===========================================================================

TEST_CASE("TubeSocket: enabled socket read delegates to TubeUla host_read", "[tube][socket]") {
    TubeSocket socket;
    socket.enable();

    // Write data from the coprocessor side, then read from the host side via TubeSocket
    socket.tube_ula()->coprocessor_write(1, 0xAB);  // R1 P-to-H FIFO

    // TubeSocket::read delegates to tube_ula_.host_read
    uint8_t status = socket.read(0);  // R1 status
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    uint8_t data = socket.read(1);  // R1 data
    CHECK(data == 0xAB);
}

TEST_CASE("TubeSocket: enabled socket write delegates to TubeUla host_write", "[tube][socket]") {
    TubeSocket socket;
    socket.enable();

    // Write data via TubeSocket (host side)
    socket.write(1, 0xCD);  // R1 H-to-P latch

    // Verify the coprocessor can read it
    uint8_t data = socket.tube_ula()->coprocessor_read(1);
    CHECK(data == 0xCD);
}

TEST_CASE("TubeSocket: R1 status read via socket shows control flags", "[tube][socket]") {
    TubeSocket socket;
    socket.enable();

    // Set control flags via socket (host write to offset 0)
    socket.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_M);

    uint8_t status = socket.read(0);
    CHECK((status & TubeUla::FLAG_Q) != 0);
    CHECK((status & TubeUla::FLAG_M) != 0);
}

TEST_CASE("TubeSocket: all register offsets accessible through socket", "[tube][socket]") {
    TubeSocket socket;
    socket.enable();

    // Write and read through each register pair to verify full delegation
    // R1: H-to-P latch
    socket.write(1, 0x11);
    CHECK(socket.tube_ula()->coprocessor_read(1) == 0x11);

    // R2: H-to-P latch
    socket.write(3, 0x22);
    CHECK(socket.tube_ula()->coprocessor_read(3) == 0x22);

    // R3: H-to-P FIFO
    socket.write(5, 0x33);
    CHECK(socket.tube_ula()->coprocessor_read(5) == 0x33);

    // R4: H-to-P latch
    socket.write(7, 0x44);
    CHECK(socket.tube_ula()->coprocessor_read(7) == 0x44);
}

// ===========================================================================
// IRQ routing (HIRQ through irq_pending)
// ===========================================================================

TEST_CASE("TubeSocket: irq_pending reflects HIRQ when enabled", "[tube][socket][irq]") {
    TubeSocket socket;
    socket.enable();

    SECTION("no IRQ when Q=0") {
        socket.tube_ula()->coprocessor_write(7, 0x42);  // R4 P-to-H data
        CHECK_FALSE(socket.irq_pending());
    }

    SECTION("no IRQ when Q=1 but no R4 P-to-H data") {
        socket.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        CHECK_FALSE(socket.irq_pending());
    }

    SECTION("IRQ asserted when Q=1 and R4 P-to-H has data") {
        socket.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        socket.tube_ula()->coprocessor_write(7, 0x42);
        CHECK(socket.irq_pending());
    }

    SECTION("IRQ deasserted when R4 P-to-H data consumed") {
        socket.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        socket.tube_ula()->coprocessor_write(7, 0x42);
        CHECK(socket.irq_pending());

        socket.read(7);  // host reads R4 data, clearing the cause
        CHECK_FALSE(socket.irq_pending());
    }

    SECTION("IRQ deasserted when Q cleared") {
        socket.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        socket.tube_ula()->coprocessor_write(7, 0x42);
        CHECK(socket.irq_pending());

        socket.write(0, TubeUla::FLAG_Q);  // S=0, clear Q
        CHECK_FALSE(socket.irq_pending());
    }
}

TEST_CASE("TubeSocket: irq_pending always false when disabled", "[tube][socket][irq]") {
    TubeSocket socket;
    socket.enable();

    // Set up HIRQ condition
    socket.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
    socket.tube_ula()->coprocessor_write(7, 0x42);
    REQUIRE(socket.irq_pending());

    // Disable the socket -- IRQ should disappear
    socket.disable();
    CHECK_FALSE(socket.irq_pending());
}

// ===========================================================================
// IRQ aggregator integration
// ===========================================================================

TEST_CASE("TubeSocket: satisfies IrqSource concept", "[tube][socket][irq]") {
    CHECK(IrqSource<TubeSocket>);
}

TEST_CASE("TubeSocket: HIRQ appears at correct bit in IrqAggregator", "[tube][socket][irq]") {
    TubeSocket socket;
    socket.enable();

    auto aggregator = make_irq_aggregator(
        make_irq_binding<2>(socket)
    );

    SECTION("no IRQ: poll returns 0") {
        CHECK(aggregator.poll() == 0x00);
    }

    SECTION("HIRQ active: poll returns bit 2") {
        socket.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        socket.tube_ula()->coprocessor_write(7, 0x42);
        CHECK(aggregator.poll() == 0x04);
    }
}

TEST_CASE("TubeSocket: HIRQ coexists with VIA IRQs in aggregator", "[tube][socket][irq]") {
    // Minimal IrqSource stub for testing aggregation
    struct StubIrqSource {
        bool active = false;
        bool irq_pending() const { return active; }
    };

    StubIrqSource system_via;
    StubIrqSource user_via;
    TubeSocket tube;
    tube.enable();

    auto aggregator = make_irq_aggregator(
        make_irq_binding<0>(system_via),
        make_irq_binding<1>(user_via),
        make_irq_binding<2>(tube)
    );

    SECTION("no sources active: poll returns 0") {
        CHECK(aggregator.poll() == 0x00);
    }

    SECTION("only Tube HIRQ: poll returns bit 2") {
        tube.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        tube.tube_ula()->coprocessor_write(7, 0x42);
        CHECK(aggregator.poll() == 0x04);
    }

    SECTION("only System VIA: poll returns bit 0") {
        system_via.active = true;
        CHECK(aggregator.poll() == 0x01);
    }

    SECTION("System VIA and Tube: poll returns bits 0 and 2") {
        system_via.active = true;
        tube.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        tube.tube_ula()->coprocessor_write(7, 0x42);
        CHECK(aggregator.poll() == 0x05);
    }

    SECTION("all three sources: poll returns bits 0, 1, and 2") {
        system_via.active = true;
        user_via.active = true;
        tube.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);
        tube.tube_ula()->coprocessor_write(7, 0x42);
        CHECK(aggregator.poll() == 0x07);
    }
}

// ===========================================================================
// Reset
// ===========================================================================

TEST_CASE("TubeSocket: reset clears TubeUla state", "[tube][socket]") {
    TubeSocket socket;
    socket.enable();

    // Put some data into registers and set control flags
    socket.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q | TubeUla::FLAG_I);
    socket.write(1, 0xAA);  // R1 H-to-P
    socket.write(3, 0xBB);  // R2 H-to-P

    socket.reset();

    // Control flags should be cleared
    CHECK(socket.tube_ula()->control_flags() == 0);

    // No interrupts after reset
    CHECK_FALSE(socket.irq_pending());
    CHECK_FALSE(socket.tube_ula()->hirq());
    CHECK_FALSE(socket.tube_ula()->pirq());
    CHECK_FALSE(socket.tube_ula()->pnmi());

    // Registers should be empty (coprocessor side sees no data)
    uint8_t r1_stat = socket.tube_ula()->coprocessor_read(0);
    CHECK((r1_stat & TubeUla::DATA_AVAILABLE) == 0);
}

TEST_CASE("TubeSocket: reset does not change enabled state", "[tube][socket]") {
    TubeSocket socket;
    socket.enable();
    socket.reset();
    CHECK(socket.enabled());
}

// ===========================================================================
// Coprocessor management (host-time-driven)
// ===========================================================================

TEST_CASE("TubeSocket: run_coprocessor_until passes host time through unchanged", "[tube][socket][coprocessor]") {
    TubeSocket socket;
    RecordingCoprocessor cop;
    socket.install_coprocessor(&cop);

    // install_coprocessor() runs the coprocessor to the current host time
    // (0 here) at once, establishing its origin at install; that is the
    // leading 0. Every later call is forwarded unchanged.
    socket.run_coprocessor_until(0);
    socket.run_coprocessor_until(1);
    socket.run_coprocessor_until(1);   // idempotent second call still forwarded
    socket.run_coprocessor_until(42);

    CHECK(cop.run_until_args == std::vector<uint64_t>{0, 0, 1, 1, 42});
}

TEST_CASE("TubeSocket: a register access after a direct run_coprocessor_until does not run the coprocessor backwards",
          "[tube][socket][coprocessor]") {
    TubeSocket socket;
    RecordingCoprocessor cop;
    socket.install_coprocessor(&cop);

    // A driver that advances the coprocessor directly via run_coprocessor_until
    // (rather than host_cycle()) still leaves the socket's host time consistent:
    // run_coprocessor_until keeps host_time_ >= the furthest run.
    socket.run_coprocessor_until(100000);

    // A host register read syncs the coprocessor to host time first. It must
    // sync to at least 100000, never backwards to a stale host_time_ of 0.
    socket.read(0);

    REQUIRE(!cop.run_until_args.empty());
    uint64_t highest = 0;
    for (uint64_t arg : cop.run_until_args) {
        CHECK(arg >= highest);   // monotonic non-decreasing: never backwards
        highest = arg;
    }
    CHECK(cop.run_until_args.back() >= 100000);
}

TEST_CASE("TubeSocket: run_coprocessor_until is a no-op when nothing is installed", "[tube][socket][coprocessor]") {
    TubeSocket socket;
    // No coprocessor installed: must not crash and must do nothing.
    socket.run_coprocessor_until(0);
    socket.run_coprocessor_until(1000);
    CHECK_FALSE(socket.enabled());
}

TEST_CASE("TubeSocket: run_coprocessor_until stops after remove_coprocessor", "[tube][socket][coprocessor]") {
    TubeSocket socket;
    RecordingCoprocessor cop;
    socket.install_coprocessor(&cop);
    socket.run_coprocessor_until(5);
    socket.remove_coprocessor();
    socket.run_coprocessor_until(6);   // no longer forwarded

    // The leading 0 is the origin-establishing call made at install.
    CHECK(cop.run_until_args == std::vector<uint64_t>{0, 5});
}


TEST_CASE("TubeSocket: reset() resets the installed coprocessor", "[tube][socket][coprocessor]") {
    TubeSocket socket;
    RecordingCoprocessor cop;
    socket.install_coprocessor(&cop);

    socket.reset();
    CHECK(cop.reset_count == 1);

    socket.reset();
    CHECK(cop.reset_count == 2);

    // After removal, reset() no longer touches the coprocessor.
    socket.remove_coprocessor();
    socket.reset();
    CHECK(cop.reset_count == 2);
}

// ===========================================================================
// HasTubeSocket concept and tube_present() helper
// ===========================================================================

namespace {

struct HardwareWithTube {
    TubeSocket tube_socket;
};

struct HardwareWithoutTube {
    int some_field = 0;
};

}  // namespace

TEST_CASE("TubeConcepts: HasTubeSocket detects tube_socket member", "[tube][concepts]") {
    CHECK(HasTubeSocket<HardwareWithTube>);
    CHECK_FALSE(HasTubeSocket<HardwareWithoutTube>);
}

TEST_CASE("TubeConcepts: tube_present returns false for hardware without socket", "[tube][concepts]") {
    HardwareWithoutTube hw;
    CHECK_FALSE(tube_present(hw));
}

TEST_CASE("TubeConcepts: tube_present returns false when socket disabled", "[tube][concepts]") {
    HardwareWithTube hw;
    CHECK_FALSE(tube_present(hw));
}

TEST_CASE("TubeConcepts: tube_present returns true when socket enabled", "[tube][concepts]") {
    HardwareWithTube hw;
    hw.tube_socket.enable();
    CHECK(tube_present(hw));
}

// ===========================================================================
// Memory-mapped device interface (address mirroring)
// ===========================================================================

TEST_CASE("TubeSocket: offset masked to 3 bits by TubeUla", "[tube][socket]") {
    TubeSocket socket;
    socket.enable();

    // Write R1 data at offset 1, read status via offset 8 (should map to offset 0)
    socket.write(1, 0x42);

    // Offset 8 should be the same as offset 0 (3-bit mask in TubeUla)
    uint8_t stat_via_0 = socket.read(0);
    uint8_t stat_via_8 = socket.read(8);
    CHECK(stat_via_0 == stat_via_8);
}

// ===========================================================================
// OSBYTE &EA detection (MOS Tube presence check)
// ===========================================================================

TEST_CASE("TubeSocket: open bus read reflects last bus value", "[tube][socket]") {
    // Direct unit test: empty socket returns whatever last_bus_value_ptr points to
    TubeSocket socket;
    uint8_t bus_value = 0xA3;
    socket.set_last_bus_value_ptr(&bus_value);
    CHECK(socket.read(0) == 0xA3);
}

TEST_CASE("TubeSocket: enabled socket status has valid flags", "[tube][socket]") {
    TubeSocket socket;
    socket.enable();
    // After reset: no data, space available, no control flags set.
    // Bits 7-6: 0,1 (no P-to-H data, H-to-P not full). Bits 5-0: 0 (flags clear).
    uint8_t status = socket.read(0);
    CHECK(status != 0xFF);
    CHECK((status & TubeUla::SPACE_AVAILABLE) != 0);  // bit 6 set
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);    // bit 7 clear
    CHECK((status & 0x3F) == 0x00);                     // no control flags
}

// ===========================================================================
// MOS 1.20 Tube detection -- cycle-accurate simulation
// ===========================================================================
//
// MOS 1.20 detects the Tube by writing 0x81 (S=1, Q=1) to the Tube ULA
// status register at $FEE0, then reading it back and checking bit 0 (Q).
// If the Tube ULA is present, Q latches and reads back as 1. If absent,
// the read returns the open bus value.
//
// The critical subtlety is that the 6502's instruction fetch cycles between
// the STA and LDA update MemoryMap's last_bus_value_. The final fetch
// before the LDA's read cycle is the high address byte ($FE), so the open
// bus returns $FE (%11111110) -- bit 0 is 0 -- and MOS correctly concludes
// no Tube is present.
//
// Reference: https://tobylobster.github.io/mos/mos/S-s10.html

TEST_CASE("TubeSocket: MOS 1.20 Tube detection with bus cycle simulation", "[tube][socket][integration]") {
    // Minimal memory map: TubeSocket at $FEE0, RAM standing in for MOS ROM
    Ram<16384> rom;
    TubeSocket tube_socket;

    auto memory = MemoryMap{
        make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(tube_socket),
        make_region<0xC000, 0xFFFF>(rom)
    };

    tube_socket.set_last_bus_value_ptr(memory.last_bus_value_ptr());

    // Plant the MOS detection sequence at $C000:
    //   $C000: A9 81       LDA #$81
    //   $C002: 8D E0 FE    STA $FEE0
    //   $C005: AD E0 FE    LDA $FEE0
    //   $C008: 6A          ROR A
    //   $C009: 90 02       BCC +2
    const uint8_t code[] = {
        0xA9, 0x81,             // LDA #$81
        0x8D, 0xE0, 0xFE,      // STA $FEE0
        0xAD, 0xE0, 0xFE,      // LDA $FEE0
        0x6A,                   // ROR A
        0x90, 0x02              // BCC +2
    };
    for (size_t i = 0; i < sizeof(code); ++i) {
        rom.write(static_cast<uint16_t>(i), code[i]);
    }

    SECTION("Tube absent: detection correctly fails") {
        REQUIRE_FALSE(tube_socket.enabled());

        // === LDA #$81 (2 cycles) ===
        uint8_t a = 0;
        memory.read(0xC000);        // fetch opcode: $A9
        a = memory.read(0xC001);    // fetch immediate: $81

        // === STA $FEE0 (4 cycles) ===
        memory.read(0xC002);        // fetch opcode: $8D
        memory.read(0xC003);        // fetch addr lo: $E0
        memory.read(0xC004);        // fetch addr hi: $FE
        memory.write(0xFEE0, a);    // write $81 to Tube (no-op, socket empty)

        // At this point last_bus_value_ = $81 (from the write).
        // But the next instruction's fetch cycles will overwrite it.

        // === LDA $FEE0 (4 cycles) ===
        memory.read(0xC005);        // fetch opcode: $AD  -> last_bus_value_ = $AD
        memory.read(0xC006);        // fetch addr lo: $E0 -> last_bus_value_ = $E0
        memory.read(0xC007);        // fetch addr hi: $FE -> last_bus_value_ = $FE
        a = memory.read(0xFEE0);    // read Tube status   -> returns last_bus_value_ = $FE

        // $FE = %11111110 -- bit 0 is 0
        // ROR A: bit 0 -> carry
        bool carry = (a & 0x01) != 0;

        // BCC: carry clear -> branch taken -> no Tube detected
        CHECK_FALSE(carry);
    }

    SECTION("Tube present: detection correctly succeeds") {
        tube_socket.enable();

        // === LDA #$81 ===
        uint8_t a = 0;
        memory.read(0xC000);
        a = memory.read(0xC001);

        // === STA $FEE0 ===
        memory.read(0xC002);
        memory.read(0xC003);
        memory.read(0xC004);
        memory.write(0xFEE0, a);    // writes $81 -> TubeUla sets S=1, Q=1

        // === LDA $FEE0 ===
        memory.read(0xC005);
        memory.read(0xC006);
        memory.read(0xC007);
        a = memory.read(0xFEE0);    // reads TubeUla status: Q=1 in bit 0

        // Verify the status register has the expected shape:
        // bit 7 = 0 (no P-to-H data), bit 6 = 1 (H-to-P space), bit 0 = 1 (Q)
        CHECK((a & TubeUla::DATA_AVAILABLE) == 0);
        CHECK((a & TubeUla::SPACE_AVAILABLE) != 0);
        CHECK((a & TubeUla::FLAG_Q) != 0);

        // ROR A: bit 0 -> carry
        bool carry = (a & 0x01) != 0;

        // BCC: carry set -> branch not taken -> Tube detected
        CHECK(carry);
    }

    SECTION("Tube absent: instruction fetches are what prevent false detection") {
        // Demonstrate that WITHOUT intervening instruction fetches,
        // the open bus WOULD echo the written value (false positive).
        // This is why cycle-accurate bus tracking matters.
        REQUIRE_FALSE(tube_socket.enabled());

        // Back-to-back write then read with no intervening bus activity
        memory.write(0xFEE0, 0x81);
        uint8_t a = memory.read(0xFEE0);

        // Without instruction fetches, the bus still holds $81
        CHECK(a == 0x81);
        CHECK((a & 0x01) != 0);  // bit 0 set -- WOULD be a false positive
        // This confirms that the instruction fetch cycles in the sections
        // above are essential for correct Tube detection.
    }
}
