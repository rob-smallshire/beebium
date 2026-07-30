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

#include <beebium/econet/Mc6854.hpp>
#include <beebium/econet/FourWayHandshake.hpp>
#include <beebium/econet/TestBackend.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;

namespace {

struct TestFixture {
    TestBackend backend;
    Mc6854 adlc;

    TestFixture() : adlc(backend) {}

    // Release from reset (both TX and RX)
    void release_reset() {
        adlc.write(0, 0x00);  // CR1 = 0: clear TX_RESET and RX_RESET
    }

    // Release TX from reset only
    void release_tx_reset() {
        adlc.write(0, Mc6854::CR1_RX_RESET);  // Keep RX in reset, release TX
    }

    // Release RX from reset only
    void release_rx_reset() {
        adlc.write(0, Mc6854::CR1_TX_RESET);  // Keep TX in reset, release RX
    }

    // Take up the listening position, as the NFS ROM does before it expects
    // to receive anything (sub_c96eb writes CR2=&67, which carries CLR Rx ST).
    //
    // This matters because a quiet line latches Inactive Idle, and Inactive
    // Idle sits at priority 2 in the PSE tree -- above Address Present and
    // Receiver Data Available -- so it inhibits both until software clears
    // it. Real software always does; a test that receives a frame without
    // clearing first is exercising a sequence no driver performs. Reading the
    // status register first mirrors the hardware rule that a condition must
    // have been read before CLR Rx ST will clear it.
    void begin_listening(uint8_t extra_cr2_bits = 0) {
        (void)adlc.sr2();
        adlc.write(1, static_cast<uint8_t>(Mc6854::CR2_CLR_RX_ST | extra_cr2_bits));
    }

    void tick() {
        adlc.tick_rising();
        adlc.tick_falling();
    }

    // Tick for N 2MHz half-cycles (one rising + one falling = 2 half-cycles)
    void tick_n(int half_cycles) {
        for (int i = 0; i < half_cycles; ++i) {
            if (i % 2 == 0) adlc.tick_rising();
            else adlc.tick_falling();
        }
    }

    // Tick through one complete byte period (default 128 half-cycles)
    void tick_one_byte_period() {
        tick_n(adlc.byte_period());
    }

    // Tick through N byte periods
    void tick_byte_periods(int n) {
        for (int i = 0; i < n; ++i) {
            tick_one_byte_period();
        }
    }

    // Set Address Control bit (AC) to access CR3/CR4
    void set_ac() {
        adlc.write(0, adlc.cr1() | Mc6854::CR1_AC);
    }

    // Clear Address Control bit (AC) to access CR2/TX data
    void clear_ac() {
        adlc.write(0, adlc.cr1() & ~Mc6854::CR1_AC);
    }
};

}  // namespace

// =============================================================================
// Power-on / Hard Reset State
// =============================================================================

TEST_CASE("Power-on state: TX and RX held in reset", "[econet][mc6854]") {
    TestFixture t;

    CHECK(t.adlc.cr1() == (Mc6854::CR1_RX_RESET | Mc6854::CR1_TX_RESET));
    CHECK(t.adlc.cr2() == 0);
    CHECK(t.adlc.cr3() == 0);
    CHECK(t.adlc.cr4() == 0);
}

TEST_CASE("Power-on state: FIFOs empty", "[econet][mc6854]") {
    TestFixture t;

    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.rx_fifo_empty());
    CHECK_FALSE(t.adlc.tx_fifo_full());
    CHECK_FALSE(t.adlc.rx_fifo_full());
}

TEST_CASE("Power-on state: status registers clear", "[econet][mc6854]") {
    TestFixture t;

    // SR1/SR2 should be mostly clear at power-on
    // SR1 bit 6 (TDRA) might be set since TX FIFO is empty - but TX is in reset
    CHECK((t.adlc.sr1() & ~Mc6854::SR1_IRQ) == 0x00);
}

TEST_CASE("Power-on state: IRQ not asserted", "[econet][mc6854]") {
    TestFixture t;
    CHECK_FALSE(t.adlc.irq_output());
}

TEST_CASE("Power-on state: frame fields idle", "[econet][mc6854]") {
    TestFixture t;
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Idle);
    CHECK(t.adlc.rx_frame_field() == Mc6854::FrameField::Idle);
}

// =============================================================================
// Control Register 1 (CR1) — Offset 0 Write
// =============================================================================

TEST_CASE("CR1: writing offset 0 sets CR1", "[econet][mc6854]") {
    TestFixture t;
    t.adlc.write(0, 0x07);  // RIE, TIE, AC
    CHECK(t.adlc.cr1() == 0x07);
}

TEST_CASE("CR1: TX reset clears TX FIFO", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Put data in TX FIFO
    t.adlc.write(2, 0xAA);  // Write to TX FIFO
    CHECK_FALSE(t.adlc.tx_fifo_empty());

    // Assert TX reset
    t.adlc.write(0, Mc6854::CR1_TX_RESET);
    CHECK(t.adlc.tx_fifo_empty());
}

TEST_CASE("CR1: RX reset clears RX FIFO", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Can't easily fill RX FIFO in Phase 1 without backend injection.
    // Just verify reset doesn't crash and state is correct.
    t.adlc.write(0, Mc6854::CR1_RX_RESET);
    CHECK(t.adlc.rx_fifo_empty());
    CHECK(t.adlc.rx_frame_field() == Mc6854::FrameField::Idle);
}

TEST_CASE("CR1: Rx Frame Discontinue (bit 5) auto-clears", "[econet][mc6854]") {
    TestFixture t;
    t.adlc.write(0, Mc6854::CR1_DISCONTINUE);
    // Bit 5 should have auto-cleared
    CHECK((t.adlc.cr1() & Mc6854::CR1_DISCONTINUE) == 0);
}

TEST_CASE("CR1: releasing from reset", "[econet][mc6854]") {
    TestFixture t;
    CHECK(t.adlc.cr1() & Mc6854::CR1_TX_RESET);
    CHECK(t.adlc.cr1() & Mc6854::CR1_RX_RESET);

    t.release_reset();

    CHECK_FALSE(t.adlc.cr1() & Mc6854::CR1_TX_RESET);
    CHECK_FALSE(t.adlc.cr1() & Mc6854::CR1_RX_RESET);
}

// =============================================================================
// Control Register 2 (CR2) — Offset 1 Write, AC=0
// =============================================================================

TEST_CASE("CR2: writing offset 1 with AC=0 sets CR2", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, 0x82);  // RTS(0x80) + 2/1-byte(0x02)
    CHECK(t.adlc.cr2() == 0x82);
}

TEST_CASE("CR2: Clear Rx Status (bit 4) auto-clears", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    CHECK((t.adlc.cr2() & Mc6854::CR2_CLR_RX_ST) == 0);
}

TEST_CASE("CR2: Clear Tx Status (bit 5) auto-clears", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_CLR_TX_ST);
    CHECK((t.adlc.cr2() & Mc6854::CR2_CLR_TX_ST) == 0);
}

TEST_CASE("CR2: both clear bits auto-clear together", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST | Mc6854::CR2_CLR_TX_ST | Mc6854::CR2_RTS);
    CHECK(t.adlc.cr2() == Mc6854::CR2_RTS);
}

// =============================================================================
// Control Register 3 (CR3) — Offset 1 Write, AC=1
// =============================================================================

TEST_CASE("CR3: writing offset 1 with AC=1 sets CR3", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.set_ac();

    t.adlc.write(1, 0x0A);  // AEX + FD Enable
    CHECK(t.adlc.cr3() == 0x0A);
}

TEST_CASE("AC bit toggles between CR2 and CR3", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Write CR2 (AC=0)
    t.clear_ac();
    t.adlc.write(1, 0x82);  // RTS(0x80) + 2/1-byte(0x02)
    CHECK(t.adlc.cr2() == 0x82);

    // Write CR3 (AC=1)
    t.set_ac();
    t.adlc.write(1, 0x0B);
    CHECK(t.adlc.cr3() == 0x0B);
    CHECK(t.adlc.cr2() == 0x82);  // CR2 unchanged
}

// =============================================================================
// Control Register 4 (CR4) — Offset 3 Write, AC=1
// =============================================================================

TEST_CASE("CR4: writing offset 3 with AC=1 sets CR4", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.set_ac();

    t.adlc.write(3, 0xC1);  // TX CRC inhibit + RX CRC inhibit + double flag
    CHECK(t.adlc.cr4() == (0xC1 & ~Mc6854::CR4_ABT_EXT));
}

TEST_CASE("CR4: Abort Extend (bit 4) auto-clears", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.set_ac();

    t.adlc.write(3, Mc6854::CR4_ABT_EXT | Mc6854::CR4_DBL_FLAG);
    CHECK((t.adlc.cr4() & Mc6854::CR4_ABT_EXT) == 0);
    CHECK(t.adlc.cr4() & Mc6854::CR4_DBL_FLAG);  // Other bits preserved
}

// =============================================================================
// Status Register 1 (SR1) — Offset 0 Read
// =============================================================================

TEST_CASE("SR1: reading offset 0 returns SR1", "[econet][mc6854]") {
    TestFixture t;
    // At power-on, SR1 should be clear
    uint8_t sr1 = t.adlc.read(0);
    CHECK(sr1 == 0x00);
}

TEST_CASE("SR1: TDRA set when TX FIFO has space and not in reset", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_RTS);  // Assert RTS so CTS clears
    t.tick();

    uint8_t sr1 = t.adlc.read(0);
    CHECK(sr1 & Mc6854::SR1_TDRA);
}

TEST_CASE("SR1: TDRA clear when TX in reset", "[econet][mc6854]") {
    TestFixture t;
    // TX in reset by default
    t.tick();

    uint8_t sr1 = t.adlc.read(0);
    CHECK_FALSE(sr1 & Mc6854::SR1_TDRA);
}

// =============================================================================
// Status Register 2 (SR2) — Offset 1 Read
// =============================================================================

TEST_CASE("SR2: reading offset 1 returns SR2", "[econet][mc6854]") {
    TestFixture t;
    uint8_t sr2 = t.adlc.read(1);
    // At power-on with connected backend, DCD should be clear
    CHECK_FALSE(sr2 & Mc6854::SR2_DCD);
}

TEST_CASE("SR2: DCD reflects backend disconnection", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.backend.set_connected(true);
    t.tick();
    CHECK_FALSE(t.adlc.read(1) & Mc6854::SR2_DCD);

    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.read(1) & Mc6854::SR2_DCD);
}

// =============================================================================
// TX FIFO — Offset 2 Write (frame continue) and Offset 3 Write, AC=0 (frame terminate)
// =============================================================================

TEST_CASE("TX FIFO: write blocked when TX in reset", "[econet][mc6854]") {
    TestFixture t;
    // TX is in reset by default
    t.adlc.write(2, 0xAA);
    CHECK(t.adlc.tx_fifo_empty());
}

TEST_CASE("TX FIFO: write succeeds when released from reset", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.adlc.write(2, 0xAA);
    CHECK_FALSE(t.adlc.tx_fifo_empty());
}

TEST_CASE("TX FIFO: three bytes fill the FIFO", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.adlc.write(2, 0x01);
    CHECK_FALSE(t.adlc.tx_fifo_full());
    t.adlc.write(2, 0x02);
    CHECK_FALSE(t.adlc.tx_fifo_full());
    t.adlc.write(2, 0x03);
    CHECK(t.adlc.tx_fifo_full());
}

TEST_CASE("TX FIFO: TDRA clears when FIFO full", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.adlc.write(2, 0x01);
    t.adlc.write(2, 0x02);
    t.adlc.write(2, 0x03);

    t.tick();
    CHECK_FALSE(t.adlc.read(0) & Mc6854::SR1_TDRA);
}

TEST_CASE("TX FIFO: frame terminate via offset 3 with AC=0", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(2, 0x01);  // Continue
    t.adlc.write(3, 0x02);  // Terminate (last byte) — triggers immediate flush

    // FIFO should be empty after flush, and frame should be sent
    CHECK(t.adlc.tx_fifo_empty());
    REQUIRE(t.backend.sent_frame_count() == 1);
    CHECK(t.backend.sent_frames()[0] == std::vector<uint8_t>({0x01, 0x02}));
}

TEST_CASE("TX FIFO: offset 3 with AC=1 writes CR4, not TX FIFO", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.set_ac();

    t.adlc.write(3, 0x41);  // Should go to CR4
    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.cr4() == (0x41 & ~Mc6854::CR4_ABT_EXT));
}

// =============================================================================
// RX FIFO — Offset 2/3 Read
// =============================================================================

TEST_CASE("RX FIFO: read returns 0 when empty", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    CHECK(t.adlc.read(2) == 0x00);
    CHECK(t.adlc.read(3) == 0x00);
}

// =============================================================================
// Frame Field State Machine — TX
// =============================================================================

TEST_CASE("TX frame field: first byte is address (AP set)", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Idle);
    t.adlc.write(2, 0xFF);  // First byte — should be address
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Address);
}

TEST_CASE("TX frame field: second byte advances to control", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.adlc.write(2, 0xFF);  // Dest addr
    t.adlc.write(2, 0x01);  // Src addr
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Control);
}

TEST_CASE("TX frame field: third byte advances to data", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.adlc.write(2, 0xFF);  // Address
    t.adlc.write(2, 0x01);  // Address
    t.adlc.write(2, 0x80);  // Control → advances to data
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Data);
}

TEST_CASE("TX frame field: frame terminate resets to idle", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(2, 0xFF);  // Address
    t.adlc.write(2, 0x01);  // Address
    t.adlc.write(3, 0x80);  // Frame terminate
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Idle);
}

TEST_CASE("TX frame field: extended addressing with AEX", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Enable AEX
    t.set_ac();
    t.adlc.write(1, Mc6854::CR3_AEX);
    t.clear_ac();

    // First address byte with bit 0 = 0 (more address bytes follow)
    t.adlc.write(2, 0x00);
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Address);

    // Second address byte with bit 0 = 0 (still more)
    t.adlc.write(2, 0x02);
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Address);

    // Third address byte with bit 0 = 1 (last address byte)
    t.adlc.write(2, 0x01);
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Control);
}

// =============================================================================
// Register Addressing — AC Bit Routing
// =============================================================================

TEST_CASE("Register addressing: offset 0 always writes CR1", "[econet][mc6854]") {
    TestFixture t;

    // AC=0
    t.adlc.write(0, Mc6854::CR1_RIE);
    CHECK(t.adlc.cr1() == Mc6854::CR1_RIE);

    // AC=1 — offset 0 still goes to CR1
    t.set_ac();
    t.adlc.write(0, Mc6854::CR1_TIE | Mc6854::CR1_AC);
    CHECK(t.adlc.cr1() == (Mc6854::CR1_TIE | Mc6854::CR1_AC));
}

TEST_CASE("Register addressing: offset 0 read always returns SR1", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Should return SR1 regardless of AC state
    t.tick();
    uint8_t sr1_ac0 = t.adlc.read(0);

    t.set_ac();
    t.tick();
    uint8_t sr1_ac1 = t.adlc.read(0);

    CHECK(sr1_ac0 == sr1_ac1);
}

TEST_CASE("Register addressing: offsets 2,3 read RX FIFO regardless of AC", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Both with AC=0 and AC=1, offsets 2 and 3 should read from RX FIFO
    t.clear_ac();
    CHECK(t.adlc.read(2) == 0x00);
    CHECK(t.adlc.read(3) == 0x00);

    t.set_ac();
    CHECK(t.adlc.read(2) == 0x00);
    CHECK(t.adlc.read(3) == 0x00);
}

// =============================================================================
// TX/RX Reset
// =============================================================================

TEST_CASE("TX Reset: clears FIFO and frame state", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Fill TX FIFO
    t.adlc.write(2, 0xAA);
    t.adlc.write(2, 0xBB);
    CHECK_FALSE(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.tx_frame_field() != Mc6854::FrameField::Idle);

    // Assert TX reset
    t.adlc.write(0, Mc6854::CR1_TX_RESET);
    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Idle);
}

TEST_CASE("RX Reset: clears FIFO and frame state", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Assert RX reset
    t.adlc.write(0, Mc6854::CR1_RX_RESET);
    CHECK(t.adlc.rx_fifo_empty());
    CHECK(t.adlc.rx_frame_field() == Mc6854::FrameField::Idle);
}

TEST_CASE("Both resets: can be asserted simultaneously", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.adlc.write(2, 0x42);  // Put something in TX FIFO

    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RX_RESET);
    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.rx_fifo_empty());
}

// =============================================================================
// Clear Status Operations
// =============================================================================

TEST_CASE("Clear Rx Status: clears stored RX status bits", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Clear RX status via CR2 bit 4
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);

    // After clearing, FD in SR1 and AP/FV/ERR/ABT/OVRN in SR2 should be clear
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_FD);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_AP);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_ERR);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_ABT);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_OVRN);
}

TEST_CASE("Clear Tx Status: clears stored TX status bits", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Assert RTS (so CTS is low) and clear TX status
    t.adlc.write(1, Mc6854::CR2_RTS | Mc6854::CR2_CLR_TX_ST);

    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TXU);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_CTS);
}

// =============================================================================
// Hard Reset
// =============================================================================

TEST_CASE("Hard reset: restores power-on state", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // Modify everything
    t.set_ac();
    t.adlc.write(1, 0xFF);  // CR3
    t.adlc.write(3, 0xFF);  // CR4
    t.clear_ac();
    t.adlc.write(1, 0xFF);  // CR2
    t.release_reset();
    t.adlc.write(2, 0xAA);  // TX FIFO

    t.adlc.hard_reset();

    CHECK(t.adlc.cr1() == (Mc6854::CR1_RX_RESET | Mc6854::CR1_TX_RESET));
    CHECK(t.adlc.cr2() == 0);
    CHECK(t.adlc.cr3() == 0);
    CHECK(t.adlc.cr4() == 0);
    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.rx_fifo_empty());
    CHECK(t.adlc.tx_frame_field() == Mc6854::FrameField::Idle);
    CHECK(t.adlc.rx_frame_field() == Mc6854::FrameField::Idle);
    CHECK_FALSE(t.adlc.irq_output());
}

// =============================================================================
// Two-Phase Clock
// =============================================================================

TEST_CASE("Tick: rising and falling edges update status", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_RTS);  // Assert RTS so CTS clears

    // After releasing from reset with RTS, TDRA should be set on next tick
    t.adlc.tick_rising();
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);

    // Falling edge also works
    t.adlc.tick_falling();
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
}

// =============================================================================
// IRQ Output (basic — refined in Phase 2)
// =============================================================================

TEST_CASE("IRQ: not asserted when interrupts disabled", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_RTS);  // Assert RTS so CTS clears
    t.tick();

    // TDRA is set but TIE is not enabled
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK_FALSE(t.adlc.irq_output());
}

TEST_CASE("IRQ: asserted when TIE enabled and TDRA set", "[econet][mc6854]") {
    TestFixture t;

    // Enable TIE (AC=0 so CR2 writes go to CR2), assert RTS → CTS clear
    t.adlc.write(0, Mc6854::CR1_TIE);
    t.adlc.write(1, Mc6854::CR2_RTS);
    t.tick();

    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK(t.adlc.irq_output());
    CHECK(t.adlc.sr1() & Mc6854::SR1_IRQ);
}

TEST_CASE("IRQ: SR1 bit 7 reflects IRQ output", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    // No IRQ
    t.tick();
    CHECK_FALSE(t.adlc.read(0) & Mc6854::SR1_IRQ);

    // Enable TIE → IRQ
    t.adlc.write(0, Mc6854::CR1_TIE);
    t.tick();
    CHECK(t.adlc.read(0) & Mc6854::SR1_IRQ);
}

// =============================================================================
// S2RQ (Status #2 Read Request)
// =============================================================================

TEST_CASE("S2RQ: set when DCD asserted in SR2", "[econet][mc6854]") {
    TestFixture t;
    t.release_reset();

    t.backend.set_connected(false);  // DCD asserted
    t.tick();

    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);
}

TEST_CASE("S2RQ: clear when no SR2 conditions", "[econet][mc6854]") {
    TestFixture t;
    // Keep RX in reset so idle (SR2_INACTIVE) is suppressed — idle is a legitimate
    // SR2 condition when RX is active on a quiet line, which would set S2RQ.
    t.release_tx_reset();

    t.backend.set_connected(true);
    t.tick();

    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_S2RQ);
}

// =============================================================================
// Phase 2: Stored vs Present Status (Dual-Nature Bits)
// =============================================================================

TEST_CASE("DCD: dual-nature - stored latch + present input", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // Initially connected → DCD clear
    t.backend.set_connected(true);
    t.tick();
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);

    // Disconnect → DCD present asserts, stored latch captures edge
    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);

    // Reconnect → DCD present clears, but stored latch keeps it set
    t.backend.set_connected(true);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);  // Still set due to stored latch

    // Clear stored latch via Clear RX Status
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.tick();
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);  // Now clear — present is connected
}

TEST_CASE("DCD: stored latch persists after present clears", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // Trigger DCD edge
    t.backend.set_connected(true);
    t.tick();
    t.backend.set_connected(false);
    t.tick();

    // Present goes away but stored keeps the bit
    t.backend.set_connected(true);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
}

TEST_CASE("DCD: CLR_RX_ST clears edge latch but SR2 still reflects pin level", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // Disconnect → DCD edge latches stored condition
    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);  // Edge latch drives S2RQ

    // Clear stored with CLR_RX_ST
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.tick();

    // SR2 DCD still shows: pin is high (no carrier), receiver is active.
    // This is the level-sensitive path — the NFS ROM polls this to detect
    // "No Clock" during boot.
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);

    // But S2RQ is cleared: the edge latch was cleared and no new 0→1
    // transition occurred (pin stayed high). This prevents NMI storms
    // when carrier is continuously absent (--aun-port none).
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_S2RQ);

    // Reconnect → carrier present, DCD pin goes low, SR2 DCD clears
    t.backend.set_connected(true);
    t.tick();
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);

    // Disconnect again → new 0→1 edge re-latches
    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);  // Edge latch drives S2RQ again
}

TEST_CASE("DCD: level-sensitive SR2 path is gated by RX_RESET", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // Disconnect with receiver active → SR2 DCD shows pin level
    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);

    // Clear edge latch, then put receiver into reset
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.adlc.write(0, Mc6854::CR1_RX_RESET | Mc6854::CR1_AC);
    t.tick();

    // During RX_RESET, the level-sensitive path is disabled.
    // Edge latch was cleared, so SR2 DCD should be clear.
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);

    // Release reset → level path re-activates, DCD visible again
    t.release_reset();
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
}

// =============================================================================
// Phase 2: CTS — Stored/Present and TDRA Inhibit
// =============================================================================

TEST_CASE("CTS: positive edge latches stored condition", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // RTS asserted → CTS low (clear to send) → no CTS status
    t.adlc.write(1, Mc6854::CR2_RTS);
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_CTS);

    // Clear RTS → CTS goes high (not clear to send) → stored latch captures edge
    t.adlc.write(1, 0x00);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_CTS);

    // Re-assert RTS → CTS drops low, but stored keeps CTS status set
    t.adlc.write(1, Mc6854::CR2_RTS);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_CTS);

    // Clear TX status clears stored CTS
    t.adlc.write(1, Mc6854::CR2_RTS | Mc6854::CR2_CLR_TX_ST);
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_CTS);
}

TEST_CASE("CTS: real-time inhibit of TDRA", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // With RTS asserted → CTS low (clear to send) → TDRA should be set
    t.adlc.write(1, Mc6854::CR2_RTS);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);

    // Clear RTS → CTS high → TDRA inhibited
    t.adlc.write(1, 0x00);
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TDRA);

    // Re-assert RTS → CTS low → TDRA restored
    t.adlc.write(1, Mc6854::CR2_RTS);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
}

TEST_CASE("CTS: inhibits TDRA regardless of FIFO state", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();
    // CTS is high by default (RTS not asserted)

    // Even with empty TX FIFO, TDRA should be inhibited
    CHECK(t.adlc.tx_fifo_empty());
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TDRA);
}

// =============================================================================
// Phase 2: PSE Priority Filtering
// =============================================================================

TEST_CASE("PSE: not active by default (CR2b0=0)", "[econet][mc6854][pse]") {
    TestFixture t;
    t.release_reset();
    CHECK(t.adlc.pse_level() == 0);
    CHECK_FALSE(t.adlc.cr2() & Mc6854::CR2_PSE);
}

TEST_CASE("PSE: dynamic cascade - AP masks RDA, CLR_RX_ST reveals RDA", "[econet][mc6854][pse]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Enable PSE
    t.adlc.write(1, Mc6854::CR2_PSE);
    t.adlc.set_byte_period(4);

    // Inject a 4-byte frame (need >3 so tick after CLR_RX_ST doesn't push LAST)
    // Clear the Inactive Idle latched by the quiet line before
    // expecting a frame -- it sits above AP and RDA in the PSE tree.
    t.begin_listening(Mc6854::CR2_PSE);

    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});

    // Byte 0 arrives — AP present → P3 selected
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 3);
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_RDA);  // RDA masked by AP at P3

    // Read byte 0 (inline refill pushes byte 1) and clear RX status
    t.adlc.read(2);
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);

    // Byte 2 pushed by timer — FIFO has [byte1, byte2]. P4 selects RDA.
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 4);
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_AP);
}

TEST_CASE("PSE: FV at P1 masks RDA when inline refill pushes last byte", "[econet][mc6854][pse]") {
    // When inline refill pushes the last byte during a read, FV is set
    // immediately (push-time FV). With PSE, FV at P1 masks RDA at P4.
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_PSE);
    t.adlc.set_byte_period(4);

    // 4-byte frame: address + 3 data bytes
    // Clear the Inactive Idle latched by the quiet line before
    // expecting a frame -- it sits above AP and RDA in the PSE tree.
    t.begin_listening(Mc6854::CR2_PSE);

    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});

    // Byte 0 (AP) → P3
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 3);
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);
    t.adlc.read(2);  // inline refill: byte 1
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);

    // Timer pushes byte 2 → FIFO has [byte1, byte2]. P4 selects RDA.
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 4);
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Read byte 1 → inline refill pushes byte 3 (LAST). FV set immediately.
    // FV at P1 masks RDA at P4.
    t.adlc.read(2);
    CHECK(t.adlc.pse_level() == 1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_RDA);

    // CLR_RX_ST clears FV → RDA visible again (bytes 2, 3 still in FIFO)
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.pse_level() == 4);
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Read byte 2
    t.adlc.read(2);
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);

    // Read byte 3 (last) — was_last re-triggers FV. FIFO empty.
    t.adlc.read(2);
    CHECK(t.adlc.pse_level() == 1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_RDA);

    // CLR_RX_ST clears FV → INACTIVE visible
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.sr2() & Mc6854::SR2_INACTIVE);
}

TEST_CASE("PSE: FV at P1 masks RDA when byte timer pushes last byte", "[econet][mc6854][pse]") {
    // When the byte timer pushes the last byte, FV is set immediately
    // (push-time FV). With PSE, FV at P1 masks RDA at P4.
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_PSE);
    t.adlc.set_byte_period(4);

    // Clear the Inactive Idle latched by the quiet line before
    // expecting a frame -- it sits above AP and RDA in the PSE tree.
    t.begin_listening(Mc6854::CR2_PSE);

    t.backend.inject_rx_frame({0xFF, 0x01, 0x42});

    // Byte 0 (AP) → P3
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 3);
    t.adlc.read(2);  // inline refill: byte 1

    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);

    // Timer pushes byte 2 (LAST) → FV set immediately. FV at P1 masks RDA.
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_RDA);

    // CLR_RX_ST clears FV. Bytes still in FIFO → RDA at P4.
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Read byte 1
    t.adlc.read(2);
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);

    // Read byte 2 (last) → re-asserts FV
    t.adlc.read(2);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
}

TEST_CASE("PSE: INACTIVE visible when idle with PSE enabled", "[econet][mc6854][pse]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Enable PSE — INACTIVE is a valid condition when no frame is being received
    t.adlc.write(1, Mc6854::CR2_PSE);
    CHECK(t.adlc.pse_level() == 2);  // P2 (INACTIVE) dynamically selected
    CHECK(t.adlc.sr2() & Mc6854::SR2_INACTIVE);

    // CLR_RX_ST advances past P2
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    // CLR_RX_ST clears stored latches but INACTIVE is a present condition
    // (line is genuinely idle), so P2 still selects it
    CHECK(t.adlc.sr2() & Mc6854::SR2_INACTIVE);
}

TEST_CASE("PSE: multi-byte data frame - FV set when last byte pushed", "[econet][mc6854][pse]") {
    // FV is set when the last byte is pushed to the FIFO, whether by the
    // byte timer or inline refill. With PSE, FV at P1 masks RDA at P4.
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_PSE);
    t.adlc.set_byte_period(4);

    // 5-byte frame: 1 address + 4 data
    // Clear the Inactive Idle latched by the quiet line before
    // expecting a frame -- it sits above AP and RDA in the PSE tree.
    t.begin_listening(Mc6854::CR2_PSE);

    t.backend.inject_rx_frame({0xFF, 0x01, 0x02, 0x03, 0x04});

    // Byte 0 (AP) → P3
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 3);
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);
    t.adlc.read(2);  // inline refill: byte 1

    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);

    // Timer pushes byte 2 → FIFO [byte1, byte2]. RDA at P4.
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 4);
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_AP);
    t.adlc.read(2);  // byte 1; inline refill: byte 3

    // Timer pushes byte 4 (LAST) → FV set immediately. FV at P1 masks RDA.
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_RDA);

    // CLR_RX_ST clears FV → RDA visible (bytes 2, 3, 4 still in FIFO)
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);

    // Read remaining data bytes
    t.adlc.read(2);  // byte 2
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);
    t.adlc.read(2);  // byte 3
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);

    // Read byte 4 (last) → was_last re-triggers FV
    t.adlc.read(2);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);

    // CLR_RX_ST clears FV → INACTIVE
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.sr2() & Mc6854::SR2_INACTIVE);
}

TEST_CASE("PSE: does not advance level when PSE disabled", "[econet][mc6854][pse]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // PSE not enabled — Clear RX Status should not advance
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    CHECK(t.adlc.pse_level() == 0);
}

TEST_CASE("PSE: RX reset resets PSE level", "[econet][mc6854][pse]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Get PSE into a non-zero state via frame reception
    t.adlc.write(1, Mc6854::CR2_PSE);
    t.adlc.set_byte_period(4);
    // Clear the Inactive Idle latched by the quiet line before
    // expecting a frame -- it sits above AP and RDA in the PSE tree.
    t.begin_listening(Mc6854::CR2_PSE);

    t.backend.inject_rx_frame({0xFF, 0x01});
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 3);  // AP active

    // RX reset should clear PSE level
    t.adlc.write(0, Mc6854::CR1_RX_RESET);
    CHECK(t.adlc.pse_level() == 0);
}

// =============================================================================
// NFS ROM Interaction Regression Tests
// =============================================================================

TEST_CASE("NFS Path 1: scout data loop sees FV after penultimate byte read", "[econet][mc6854][nfs]") {
    // Simulates the NFS ROM's $9747 polling loop for a 6-byte scout frame.
    // PSE enabled. Reading the penultimate byte (byte 3) triggers inline
    // refill of the last byte (byte 5), setting FV at P1 which masks RDA.
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_PSE);
    t.adlc.set_byte_period(4);

    // 6-byte scout frame: dest_net, dest_stn, src_net, src_stn, control, port
    // Clear the Inactive Idle latched by the quiet line before
    // expecting a frame -- it sits above AP and RDA in the PSE tree.
    t.begin_listening(Mc6854::CR2_PSE);

    t.backend.inject_rx_frame({0x00, 0xFE, 0x00, 0x01, 0x80, 0x99});

    // Byte 0 (AP) pushed by timer → P3
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 3);
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);

    // Read byte 0, clear AP status (NFS ROM reads address, clears AP)
    t.adlc.read(2);  // inline refill: byte 1
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);

    // Timer pushes byte 2 → FIFO [byte1, byte2]. P4 selects RDA.
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 4);
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);

    // NFS ROM's polling loop reads bytes while RDA visible
    t.adlc.read(2);  // byte 1; inline refill: byte 3
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);

    t.adlc.read(2);  // byte 2; inline refill: byte 4
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);

    // Read byte 3; inline refill pushes byte 5 (LAST) → FV set immediately.
    // FV at P1 masks RDA — loop sees SR2 non-zero (FV) but no RDA,
    // and takes the scout completion path.
    t.adlc.read(2);  // byte 3; inline refill: byte 5 (LAST)
    CHECK(t.adlc.pse_level() == 1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_RDA);

    // Bytes 4 and 5 still readable from FIFO
    uint8_t b4 = t.adlc.read(2);
    uint8_t b5 = t.adlc.read(2);
    CHECK(b4 == 0x80);
    CHECK(b5 == 0x99);
}

TEST_CASE("NFS Path 2: reply scout handler reads bytes then checks FV", "[econet][mc6854][nfs]") {
    // Simulates the NFS ROM's $9DE3 reply scout handler for a 4-byte frame.
    // PSE enabled. Inline refill chain feeds bytes in rapid succession.
    // FV isn't set until byte 3 (LAST) is pushed during the read of byte 2,
    // so RDA is visible when the handler first checks SR2.
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_PSE);
    t.adlc.set_byte_period(4);

    // 4-byte frame
    // Clear the Inactive Idle latched by the quiet line before
    // expecting a frame -- it sits above AP and RDA in the PSE tree.
    t.begin_listening(Mc6854::CR2_PSE);

    t.backend.inject_rx_frame({0x00, 0x01, 0x00, 0xFE});

    // Byte 0 (AP) pushed by timer → P3
    t.tick_byte_periods(1);
    CHECK(t.adlc.pse_level() == 3);
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);

    // Read byte 0 (AP): inline refill pushes byte 1. CLR_RX_ST.
    t.adlc.read(2);
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_CLR_RX_ST);

    // FIFO has [byte1]. RDA at P4. No FV yet.
    CHECK(t.adlc.pse_level() == 4);
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Read byte 1: inline refill pushes byte 2. Still no FV (not last).
    t.adlc.read(2);
    CHECK(t.adlc.sr2() & Mc6854::SR2_RDA);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Read byte 2: inline refill pushes byte 3 (LAST). FV set immediately.
    // FV at P1 masks RDA.
    t.adlc.read(2);
    CHECK(t.adlc.pse_level() == 1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_RDA);

    // Read byte 3 from FIFO (still readable despite PSE masking RDA)
    uint8_t b3 = t.adlc.read(2);
    CHECK(b3 == 0xFE);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
}

// =============================================================================
// Phase 2: IRQ — Level-Sensitive
// =============================================================================

TEST_CASE("IRQ: level-sensitive - enabling TIE with existing TDRA triggers IRQ", "[econet][mc6854][irq]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_RTS);  // Assert RTS so CTS clears
    t.tick();

    // TDRA is set, TIE is not
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK_FALSE(t.adlc.irq_output());

    // Enable TIE → IRQ should assert immediately
    t.adlc.write(0, Mc6854::CR1_TIE);
    t.tick();
    CHECK(t.adlc.irq_output());
}

TEST_CASE("IRQ: clears when cause removed", "[econet][mc6854][irq]") {
    TestFixture t;

    // Enable TIE (AC=0 so CR2 writes go to CR2), assert RTS → CTS clear → TDRA + TIE = IRQ
    t.adlc.write(0, Mc6854::CR1_TIE);
    t.adlc.write(1, Mc6854::CR2_RTS);
    t.tick();
    CHECK(t.adlc.irq_output());

    // Fill TX FIFO → TDRA clears → IRQ clears
    t.adlc.write(2, 0x01);
    t.adlc.write(2, 0x02);
    t.adlc.write(2, 0x03);
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK_FALSE(t.adlc.irq_output());
}

TEST_CASE("IRQ: clears when interrupt enable disabled", "[econet][mc6854][irq]") {
    TestFixture t;

    // Enable TIE → IRQ
    t.adlc.write(0, Mc6854::CR1_TIE);
    t.tick();
    CHECK(t.adlc.irq_output());

    // Disable TIE → IRQ clears
    t.adlc.write(0, 0x00);
    t.tick();
    CHECK_FALSE(t.adlc.irq_output());
}

TEST_CASE("IRQ: RIE + DCD triggers IRQ", "[econet][mc6854][irq]") {
    TestFixture t;
    // Keep RX in reset so idle (SR2_INACTIVE) is suppressed — otherwise idle
    // sets S2RQ immediately and RIE triggers IRQ before DCD is asserted.
    t.release_tx_reset();

    // Enable RIE (keep RX in reset)
    t.adlc.write(0, Mc6854::CR1_RIE | Mc6854::CR1_RX_RESET);
    t.tick();

    // No RX conditions → no IRQ
    CHECK_FALSE(t.adlc.irq_output());

    // Disconnect → DCD → S2RQ → IRQ
    t.backend.set_connected(false);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);
    CHECK(t.adlc.irq_output());
}

TEST_CASE("IRQ: both RIE and TIE - either cause triggers", "[econet][mc6854][irq]") {
    TestFixture t;

    // Enable both, assert RTS so CTS clears for TDRA
    t.adlc.write(0, Mc6854::CR1_RIE | Mc6854::CR1_TIE);
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_RTS);
    t.tick();

    // TDRA set → TIE cause → IRQ
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK(t.adlc.irq_output());
}

// =============================================================================
// Phase 2: Stored Status Clearing
// =============================================================================

TEST_CASE("Clear RX Status: clears stored AP, FV, ERR, ABT, OVRN, Idle, DCD, FD", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();

    // Trigger DCD stored latch
    t.backend.set_connected(false);
    t.tick();
    t.backend.set_connected(true);
    t.tick();
    CHECK(t.adlc.sr2() & Mc6854::SR2_DCD);  // Stored

    // Clear
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.tick();
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_DCD);
}

TEST_CASE("Clear TX Status: clears stored CTS and TxU", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Start with RTS asserted (CTS low)
    t.adlc.write(1, Mc6854::CR2_RTS);
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_CTS);

    // Clear RTS → CTS goes high → stored latch fires
    t.adlc.write(1, 0x00);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_CTS);

    // Re-assert RTS → CTS goes low but stored remains
    t.adlc.write(1, Mc6854::CR2_RTS);
    t.tick();
    CHECK(t.adlc.sr1() & Mc6854::SR1_CTS);

    // Clear TX status clears stored CTS
    t.adlc.write(1, Mc6854::CR2_RTS | Mc6854::CR2_CLR_TX_ST);
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_CTS);
}

// =============================================================================
// Phase 3: TX Frame Assembly
// =============================================================================

TEST_CASE("TX: 3-byte frame sent immediately on last-byte write", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);  // Short period for fast tests
    t.clear_ac();

    // Write a 3-byte frame (fits in FIFO): dest, src, terminate
    t.adlc.write(2, 0xFF);  // Dest address
    t.adlc.write(2, 0x01);  // Src address
    t.adlc.write(3, 0x80);  // Control (last byte) — triggers immediate flush

    // Frame sent immediately, no byte timer ticks needed
    REQUIRE(t.backend.sent_frame_count() == 1);
    auto& frame = t.backend.sent_frames()[0];
    REQUIRE(frame.size() == 3);
    CHECK(frame[0] == 0xFF);
    CHECK(frame[1] == 0x01);
    CHECK(frame[2] == 0x80);
}

TEST_CASE("TX: longer frame with FIFO drain-and-refill", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // Fill FIFO (3 bytes)
    t.adlc.write(2, 0xFF);  // Dest
    t.adlc.write(2, 0x01);  // Src
    t.adlc.write(2, 0x80);  // Control

    // Tick to drain one slot
    t.tick_byte_periods(1);

    // Now write the terminate byte into the freed slot
    t.adlc.write(3, 0x99);  // Data (last byte)

    // Drain remaining 3 bytes
    t.tick_byte_periods(3);

    REQUIRE(t.backend.sent_frame_count() == 1);
    auto& frame = t.backend.sent_frames()[0];
    REQUIRE(frame.size() == 4);
    CHECK(frame[0] == 0xFF);
    CHECK(frame[1] == 0x01);
    CHECK(frame[2] == 0x80);
    CHECK(frame[3] == 0x99);
}

TEST_CASE("TX: frame not sent until last-byte flag", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // Write 3 continuation bytes (no terminate)
    t.adlc.write(2, 0xFF);
    t.adlc.write(2, 0x01);
    t.adlc.write(2, 0x80);

    // Tick to drain FIFO
    t.tick_byte_periods(3);

    // No frame sent yet (no last-byte flag)
    CHECK(t.backend.sent_frame_count() == 0);
}

TEST_CASE("TX: underrun when FIFO empty mid-frame", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // Write one byte (frame start, not terminated)
    t.adlc.write(2, 0xFF);

    // Drain FIFO
    t.tick_byte_periods(1);

    // Now FIFO is empty but frame is incomplete → TxU on next byte period
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr1() & Mc6854::SR1_TXU);

    // No frame should have been sent
    CHECK(t.backend.sent_frame_count() == 0);
}

TEST_CASE("TX: blocked when TX in reset", "[econet][mc6854][frame]") {
    TestFixture t;
    // TX in reset by default
    t.adlc.set_byte_period(4);

    t.adlc.write(2, 0xFF);  // Should be blocked
    t.tick_byte_periods(4);

    CHECK(t.backend.sent_frame_count() == 0);
}

TEST_CASE("TX: configurable byte period affects continuation byte drain", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(10);
    t.clear_ac();

    // Write a continuation byte only (not terminated)
    t.adlc.write(2, 0xFF);

    // After 9 half-cycles: byte not yet drained from FIFO by timer
    t.tick_n(9);
    CHECK_FALSE(t.adlc.tx_fifo_empty());

    // After 10: first byte drained from FIFO into frame buffer
    t.tick_n(1);
    CHECK(t.adlc.tx_fifo_empty());

    // Write last byte — frame completes immediately via flush
    t.adlc.write(3, 0x01);
    REQUIRE(t.backend.sent_frame_count() == 1);
    auto& frame = t.backend.sent_frames()[0];
    REQUIRE(frame.size() == 2);
    CHECK(frame[0] == 0xFF);
    CHECK(frame[1] == 0x01);
}

// =============================================================================
// Phase 3: RX Frame Delivery
// =============================================================================

TEST_CASE("RX: received frame delivered to FIFO via byte trickle", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Inject a 4-byte frame into backend
    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});

    // After 1 byte period: first byte available
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    uint8_t b0 = t.adlc.read(2);
    CHECK(b0 == 0xFF);

    // After 2nd byte period: second byte
    t.tick_byte_periods(1);
    uint8_t b1 = t.adlc.read(2);
    CHECK(b1 == 0x01);

    // After 3rd: third byte
    t.tick_byte_periods(1);
    uint8_t b2 = t.adlc.read(2);
    CHECK(b2 == 0x80);

    // After 4th: fourth byte (last)
    t.tick_byte_periods(1);
    uint8_t b3 = t.adlc.read(2);
    CHECK(b3 == 0x42);
}

TEST_CASE("RX: FV set when last byte pushed to FIFO", "[econet][mc6854][frame]") {
    // FV is set when the last byte of a frame enters the FIFO (push-time FV),
    // not delayed by one byte period.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    t.backend.inject_rx_frame({0xFF, 0x01, 0x80});

    // FV not set while non-last bytes are being pushed
    t.tick_byte_periods(2);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Third byte (last) pushed → FV set immediately
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);

    // FV persists through reads of earlier bytes
    t.adlc.read(2);  // 0xFF
    t.adlc.read(2);  // 0x01
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);

    // FV still set after reading the last byte (cleared only by CLR_RX_ST)
    t.adlc.read(2);  // 0x80 (last)
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
}

TEST_CASE("RX: FV and RDA both set when inline refill pushes last byte", "[econet][mc6854][frame]") {
    // When inline refill pushes the last byte during a read, FV is set
    // immediately (push-time FV). Both FV and RDA are present because
    // the remaining bytes are still in the FIFO. PSE is not enabled here,
    // so both bits are visible simultaneously.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});

    // Push bytes 0-2 via trickle
    t.tick_byte_periods(3);

    // Read byte 0 → inline refill pushes byte 3 (LAST) → FV set immediately.
    // FIFO has [byte1, byte2, byte3(LAST)]. Both FV and RDA present.
    t.adlc.read(2);  // 0xFF
    uint8_t sr2 = t.adlc.sr2();
    CHECK(sr2 & Mc6854::SR2_FV);
    CHECK(sr2 & Mc6854::SR2_RDA);

    // FV and RDA persist through further reads
    t.adlc.read(2);  // 0x01
    sr2 = t.adlc.sr2();
    CHECK(sr2 & Mc6854::SR2_FV);
    CHECK(sr2 & Mc6854::SR2_RDA);

    t.adlc.read(2);  // 0x80
    sr2 = t.adlc.sr2();
    CHECK(sr2 & Mc6854::SR2_FV);
    CHECK(sr2 & Mc6854::SR2_RDA);

    // Read byte 3 (last) → FIFO empty. FV persists, RDA clears.
    t.adlc.read(2);  // 0x42
    sr2 = t.adlc.sr2();
    CHECK(sr2 & Mc6854::SR2_FV);
    CHECK_FALSE(sr2 & Mc6854::SR2_RDA);
}

TEST_CASE("RX: FV re-asserted on read after CLR_RX_ST", "[econet][mc6854][frame]") {
    // If software clears FV via CLR_RX_ST but the last byte is still in
    // the FIFO, reading that byte re-triggers fv_deferred_ → FV re-appears.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    t.backend.inject_rx_frame({0xFF, 0x42});

    // Push both bytes (byte 1 is LAST) + closing flag delay
    t.tick_byte_periods(3);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);

    // CLR_RX_ST clears FV
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Read first byte (not last) — FV stays clear
    t.adlc.read(2);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Read last byte — FV re-asserted because was_last triggers fv_deferred_
    t.adlc.read(2);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
}

TEST_CASE("RX: FV blocks further FIFO pushes until cleared", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Two frames queued
    t.backend.inject_rx_frame({0xFF, 0x01});
    t.backend.inject_rx_frame({0xAA, 0xBB});

    // Process first frame — push both bytes + closing flag delay
    t.tick_byte_periods(3);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);

    // Read first frame's bytes; FV persists
    t.adlc.read(2);  // 0xFF
    t.adlc.read(2);  // 0x01 (last)
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);

    // More ticks shouldn't deliver second frame while FV is stored
    t.tick_byte_periods(4);

    // Clear FV
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.tick();

    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Now second frame should start arriving
    t.tick_byte_periods(2);
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    uint8_t b0 = t.adlc.read(2);
    CHECK(b0 == 0xAA);
}

TEST_CASE("RX: AP set on first byte only", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});

    // First byte (dest address): AP set
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);

    // Read it
    t.adlc.read(2);

    // Second byte (src address): AP NOT set — matching BBC Micro NFS ROM
    // expectations and BeebEm behaviour
    t.tick_byte_periods(1);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_AP);

    // Read it
    t.adlc.read(2);

    // Third byte (control): AP not set
    t.tick_byte_periods(1);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_AP);
}

TEST_CASE("RX: blocked when RX in reset", "[econet][mc6854][frame]") {
    TestFixture t;
    // Keep RX in reset
    t.adlc.write(0, Mc6854::CR1_RX_RESET);
    t.adlc.set_byte_period(4);

    t.backend.inject_rx_frame({0xFF, 0x01});
    t.tick_byte_periods(4);

    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_RDA);
}

TEST_CASE("RX: empty backend returns nothing", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // No frames injected
    t.tick_byte_periods(10);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_RDA);
}

// =============================================================================
// Phase 3: TX/RX Round-Trip
// =============================================================================

TEST_CASE("Round-trip: TX frame appears in backend, backend injects for RX", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // TX: send a frame
    t.adlc.write(2, 0xAA);
    t.adlc.write(2, 0xBB);
    t.adlc.write(3, 0xCC);
    t.tick_byte_periods(3);

    REQUIRE(t.backend.sent_frame_count() == 1);
    CHECK(t.backend.sent_frames()[0] == std::vector<uint8_t>({0xAA, 0xBB, 0xCC}));

    // RX: inject a response frame
    t.backend.inject_rx_frame({0xDD, 0xEE, 0xFF});
    t.tick_byte_periods(3);

    // Read back
    CHECK(t.adlc.read(2) == 0xDD);
    CHECK(t.adlc.read(2) == 0xEE);
    CHECK(t.adlc.read(2) == 0xFF);
}

// =============================================================================
// Phase 3: Back-to-Back Frames
// =============================================================================

TEST_CASE("TX: back-to-back frames", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // First frame
    t.adlc.write(2, 0x01);
    t.adlc.write(3, 0x02);
    t.tick_byte_periods(2);
    CHECK(t.backend.sent_frame_count() == 1);

    // Second frame
    t.adlc.write(2, 0x03);
    t.adlc.write(3, 0x04);
    t.tick_byte_periods(2);
    CHECK(t.backend.sent_frame_count() == 2);

    CHECK(t.backend.sent_frames()[0] == std::vector<uint8_t>({0x01, 0x02}));
    CHECK(t.backend.sent_frames()[1] == std::vector<uint8_t>({0x03, 0x04}));
}

// =============================================================================
// INACTIVE (SR2 Rx Idle) — independent of flag fill
// =============================================================================

TEST_CASE("SR2: INACTIVE set when idle with flag fill present", "[econet][mc6854][status]") {
    TestFixture t;
    t.backend.set_connected(true);
    t.release_reset();
    t.tick();

    // Flag fill is present (TestBackend defaults to connected, but doesn't implement
    // is_receiving_flags — it inherits the base which returns false). However, INACTIVE
    // should be set regardless: the RX is idle (no data in FIFO, no pending frame).
    uint8_t sr2 = t.adlc.read(1);
    CHECK(sr2 & Mc6854::SR2_INACTIVE);
}

TEST_CASE("SR2: INACTIVE and FD simultaneously set when idle with clock box", "[econet][mc6854][status]") {
    // On real Econet, an idle network with a clock box has both FD=1 (flags being
    // received from clock box) and INACTIVE=1 (no data frames in progress).
    // This test uses a backend that reports is_receiving_flags()=true to simulate
    // the clock box, and verifies both bits are set simultaneously.
    TestBackend backend;
    backend.set_connected(true);

    // Wrap in FourWayHandshake to get is_receiving_flags()=true when idle+connected
    FourWayHandshake handshake(backend);
    Mc6854 adlc(handshake);

    // Release from reset with AC bit set so we can access CR3
    adlc.write(0, Mc6854::CR1_AC);
    // Enable Flag Detected reporting in CR3 (offset 1 when AC=1)
    adlc.write(1, Mc6854::CR3_FD_ENABLE);
    // Clear AC bit (remain out of reset)
    adlc.write(0, 0x00);

    adlc.tick_rising();
    adlc.tick_falling();

    uint8_t sr1 = adlc.read(0);
    uint8_t sr2 = adlc.read(1);

    CHECK(sr1 & Mc6854::SR1_FD);        // Flag Detected — clock box active
    CHECK(sr2 & Mc6854::SR2_INACTIVE);   // Rx Idle — no data frames
}

TEST_CASE("SR2: INACTIVE clear when RX frame data pending", "[econet][mc6854][status]") {
    TestFixture t;
    t.backend.set_connected(true);
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Clear the Inactive Idle the quiet line has already latched. SR2's
    // INACTIVE bit is the OR of that latch and the live idling detector, so
    // without this the latch would keep the bit set and this test would be
    // measuring the wrong one of the two.
    t.begin_listening();

    // Inject a frame — once the byte trickle loads data into the RX FIFO,
    // INACTIVE should clear because there is pending frame data.
    t.backend.inject_rx_frame({0x01, 0x02, 0x03, 0x04});
    t.tick_byte_periods(1);  // Triggers rx_process_byte which fetches the frame

    uint8_t sr2 = t.adlc.read(1);
    CHECK_FALSE(sr2 & Mc6854::SR2_INACTIVE);
}

TEST_CASE("SR2: INACTIVE latch survives until software clears it",
          "[econet][mc6854][status]") {
    // The companion to the test above. SR2 INACTIVE is the OR of a stored
    // condition and the live idling detector; the stored half is what tells a
    // transmitting station that nobody answered, so it must persist across
    // the arrival of a frame and yield only to CLR Rx Status.
    TestFixture t;
    t.backend.set_connected(true);
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.tick();

    // A quiet line latches the condition.
    CHECK(t.adlc.sr2() & Mc6854::SR2_INACTIVE);

    // A frame arrives. The live detector drops, but the latch holds.
    t.backend.inject_rx_frame({0x01, 0x02, 0x03, 0x04});
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr2() & Mc6854::SR2_INACTIVE);

    // Only software clears it.
    t.begin_listening();
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_INACTIVE);
}

// =============================================================================
// TDRA — 2-byte transfer mode and DCD check
// =============================================================================

TEST_CASE("SR1: TDRA in 2-byte mode requires room for 2 bytes", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Enable 2-byte mode via CR2 and assert RTS (so CTS clears, allowing TDRA)
    t.adlc.write(1, Mc6854::CR2_2_1_BYTE | Mc6854::CR2_RTS);
    t.tick();

    // Empty FIFO: room for 2 → TDRA set
    CHECK(t.adlc.read(0) & Mc6854::SR1_TDRA);

    // Fill 1 entry: still room for 2 → TDRA set
    t.adlc.write(2, 0x01);
    t.tick();
    CHECK(t.adlc.read(0) & Mc6854::SR1_TDRA);

    // Fill 2 entries: room for only 1 more → TDRA clear (need room for 2)
    t.adlc.write(2, 0x02);
    t.tick();
    CHECK_FALSE(t.adlc.read(0) & Mc6854::SR1_TDRA);
}

TEST_CASE("SR1: TDRA in 1-byte mode requires room for 1 byte", "[econet][mc6854][status]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // 1-byte mode (CR2_2_1_BYTE not set), RTS asserted so CTS clears
    t.adlc.write(1, Mc6854::CR2_RTS);
    t.tick();

    // Empty FIFO → TDRA set
    CHECK(t.adlc.read(0) & Mc6854::SR1_TDRA);

    // Fill 2 entries: still room for 1 → TDRA set
    t.adlc.write(2, 0x01);
    t.adlc.write(2, 0x02);
    t.tick();
    CHECK(t.adlc.read(0) & Mc6854::SR1_TDRA);

    // Fill 3 entries (full): no room → TDRA clear
    t.adlc.write(2, 0x03);
    t.tick();
    CHECK_FALSE(t.adlc.read(0) & Mc6854::SR1_TDRA);
}

TEST_CASE("SR1: TDRA false when DCD present (no carrier)", "[econet][mc6854][status]") {
    TestFixture t;
    t.backend.set_connected(false);  // DCD present (no carrier)
    t.release_reset();
    t.tick();

    // TX FIFO empty, TX not in reset, but no carrier → TDRA false
    // (Both DCD and CTS block TDRA when backend disconnected)
    CHECK_FALSE(t.adlc.read(0) & Mc6854::SR1_TDRA);
}

// =============================================================================
// TX Frame Flush — Immediate completion on last-byte write
// =============================================================================

TEST_CASE("TX flush: frame completes immediately on last-byte write", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(128);  // Default (slow) period
    t.clear_ac();

    // Write a 3-byte frame — last byte triggers immediate flush
    t.adlc.write(2, 0xFF);  // Dest
    t.adlc.write(2, 0x01);  // Src
    t.adlc.write(3, 0x80);  // Last byte

    // Frame should be sent immediately, without any byte timer ticks
    REQUIRE(t.backend.sent_frame_count() == 1);
    auto& frame = t.backend.sent_frames()[0];
    REQUIRE(frame.size() == 3);
    CHECK(frame[0] == 0xFF);
    CHECK(frame[1] == 0x01);
    CHECK(frame[2] == 0x80);
}

TEST_CASE("TX flush: longer frame with partial byte-timer drain", "[econet][mc6854][frame]") {
    // Simulates the NFS ROM scenario: CPU writes bytes faster than the byte
    // timer can drain them. Some bytes are extracted by the timer, the rest
    // are flushed when the last byte is written.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // Write 3 bytes (fills FIFO)
    t.adlc.write(2, 0xFE);
    t.adlc.write(2, 0x65);
    t.adlc.write(2, 0x00);

    // Drain 2 bytes via timer (into frame buffer)
    t.tick_byte_periods(2);
    CHECK(t.backend.sent_frame_count() == 0);

    // Write 2 more continuation bytes (FIFO has room after drain)
    t.adlc.write(2, 0x01);
    t.adlc.write(2, 0x42);

    // Drain 1 more byte via timer
    t.tick_byte_periods(1);

    // Write last byte — triggers flush of remaining FIFO entries
    t.adlc.write(3, 0x99);

    REQUIRE(t.backend.sent_frame_count() == 1);
    auto& frame = t.backend.sent_frames()[0];
    REQUIRE(frame.size() == 6);
    CHECK(frame[0] == 0xFE);
    CHECK(frame[1] == 0x65);
    CHECK(frame[2] == 0x00);
    CHECK(frame[3] == 0x01);
    CHECK(frame[4] == 0x42);
    CHECK(frame[5] == 0x99);
}

TEST_CASE("TX flush: FIFO empty after flush", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(2, 0xAA);
    t.adlc.write(3, 0xBB);  // Last byte → flush

    CHECK(t.backend.sent_frame_count() == 1);
    CHECK(t.adlc.tx_fifo_empty());

    // Subsequent byte timer ticks should not cause underrun
    t.adlc.set_byte_period(4);
    t.tick_byte_periods(4);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TXU);
}

// =============================================================================
// TX_LAST_DATA via CR2 — NFS ROM frame termination
// =============================================================================

TEST_CASE("TX_LAST_DATA: CR2 terminates frame without offset 3 write", "[econet][mc6854][frame]") {
    // The NFS ROM writes all data bytes to offset 2 (TX continue) and then
    // sets CR2 TX_LAST_DATA to signal frame termination. This test verifies
    // that writing TX_LAST_DATA via CR2 marks the topmost FIFO entry as LAST
    // and flushes the frame.
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    // Write 3 continuation bytes to offset 2
    t.adlc.write(2, 0xFE);  // Dest
    t.adlc.write(2, 0x65);  // Src
    t.adlc.write(2, 0x80);  // Control

    CHECK(t.backend.sent_frame_count() == 0);

    // Set TX_LAST_DATA via CR2 — frame should complete and send
    t.adlc.write(1, Mc6854::CR2_TX_LAST_DATA | Mc6854::CR2_RTS);

    REQUIRE(t.backend.sent_frame_count() == 1);
    auto& frame = t.backend.sent_frames()[0];
    REQUIRE(frame.size() == 3);
    CHECK(frame[0] == 0xFE);
    CHECK(frame[1] == 0x65);
    CHECK(frame[2] == 0x80);
    CHECK(t.adlc.tx_fifo_empty());
}

TEST_CASE("TX_LAST_DATA: works with partially drained FIFO", "[econet][mc6854][frame]") {
    // Simulates the NFS ROM scenario: CPU writes bytes via NMI (2 at a time),
    // byte timer drains some into the frame buffer, then TX_LAST_DATA flushes
    // the rest. Must interleave writes and drains to respect the 3-entry FIFO.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();
    t.adlc.write(1, Mc6854::CR2_RTS);  // Assert RTS for CTS

    // Round 1: write 2 bytes
    t.adlc.write(2, 0xFE);
    t.adlc.write(2, 0x65);

    // Drain 1 byte via timer
    t.tick_byte_periods(1);

    // Round 2: write 2 more bytes (FIFO: 1 existing + 2 new = 3 = full)
    t.adlc.write(2, 0x00);
    t.adlc.write(2, 0x01);

    // Drain 2 bytes via timer
    t.tick_byte_periods(2);

    // Round 3: write 2 more bytes (FIFO: 1 existing + 2 new = 3 = full)
    t.adlc.write(2, 0x42);
    t.adlc.write(2, 0x99);

    CHECK(t.backend.sent_frame_count() == 0);

    // Now signal frame end via TX_LAST_DATA
    t.adlc.write(1, Mc6854::CR2_TX_LAST_DATA | Mc6854::CR2_RTS);

    REQUIRE(t.backend.sent_frame_count() == 1);
    auto& frame = t.backend.sent_frames()[0];
    REQUIRE(frame.size() == 6);
    CHECK(frame[0] == 0xFE);
    CHECK(frame[1] == 0x65);
    CHECK(frame[2] == 0x00);
    CHECK(frame[3] == 0x01);
    CHECK(frame[4] == 0x42);
    CHECK(frame[5] == 0x99);
}

TEST_CASE("TX_LAST_DATA: completes frame when FIFO already empty", "[econet][mc6854][frame]") {
    // Edge case: all bytes already drained by byte timer into frame buffer,
    // then TX_LAST_DATA is set. The flush should complete the frame from the
    // frame buffer directly.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);
    t.clear_ac();

    // Write 2 continuation bytes
    t.adlc.write(2, 0xAA);
    t.adlc.write(2, 0xBB);

    // Drain both via byte timer — they go into frame buffer, no LAST flag
    t.tick_byte_periods(2);
    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.backend.sent_frame_count() == 0);

    // Set TX_LAST_DATA — FIFO is empty, frame buffer has the 2 bytes
    t.adlc.write(1, Mc6854::CR2_TX_LAST_DATA);

    REQUIRE(t.backend.sent_frame_count() == 1);
    auto& frame = t.backend.sent_frames()[0];
    REQUIRE(frame.size() == 2);
    CHECK(frame[0] == 0xAA);
    CHECK(frame[1] == 0xBB);
}

TEST_CASE("TX_LAST_DATA: auto-clears in CR2", "[econet][mc6854][frame]") {
    TestFixture t;
    t.release_reset();
    t.clear_ac();

    t.adlc.write(1, Mc6854::CR2_TX_LAST_DATA | Mc6854::CR2_RTS);
    CHECK_FALSE(t.adlc.cr2() & Mc6854::CR2_TX_LAST_DATA);
    CHECK(t.adlc.cr2() & Mc6854::CR2_RTS);  // Other bits preserved
}

// =============================================================================
// ADLC + FourWayHandshake Integration: Fake Final Ack After Watchdog Timeout
// =============================================================================
//
// These tests wire up the full stack (TestBackend → FourWayHandshake → Mc6854)
// to verify that a fake final ack generated by the watchdog timeout is
// correctly delivered through the ADLC's RX FIFO and triggers an NMI.

namespace {

struct HandshakeFixture {
    TestBackend backend;
    FourWayHandshake handshake;
    Mc6854 adlc;

    HandshakeFixture() : handshake(backend), adlc(handshake) {
        // Use a short byte period for fast, deterministic tests
        adlc.set_byte_period(4);
    }

    void tick_rising() {
        handshake.tick();
        adlc.tick_rising();
    }

    void tick_falling() {
        adlc.tick_falling();
    }

    void tick() {
        tick_rising();
        tick_falling();
    }

    // Tick N half-cycles (alternating rising/falling, starting with rising)
    void tick_n(int half_cycles) {
        for (int i = 0; i < half_cycles; ++i) {
            if (i % 2 == 0) tick_rising();
            else tick_falling();
        }
    }

    // Tick through N byte periods (each byte_period half-cycles)
    void tick_byte_periods(int n) {
        tick_n(n * adlc.byte_period());
    }

    // Write a byte to the TX FIFO (offset 2 = continue), ticking to drain
    // the FIFO if it would overflow. On real hardware the NMI handler checks
    // TDRA before each write; here we simulate that by draining between writes.
    void write_tx_with_drain(uint8_t value) {
        if (adlc.tx_fifo_full()) {
            tick_byte_periods(1);
        }
        adlc.write(2, value);
    }

    // Drive the ADLC through a full TX handshake: scout → scout ack → data.
    // Mirrors the NFS ROM's register sequence:
    //   1. Reset ADLC (CR1=$C1, CR4, CR3, CR1=$82, CR2=$67)
    //   2. TX setup (CR2=$E7 with RTS, CR1=$44 with TIE|RX_RESET)
    //   3. Write scout frame via TX FIFO, last byte via TX_LAST_DATA
    //   4. Wait for scout ack timeout, read fake scout ack from RX FIFO
    //   5. Write data frame via TX FIFO, last byte via TX_LAST_DATA
    //   6. Switch to receive mode (CR1=$82 with TX_RESET|RIE)
    void send_scout_and_data(uint8_t dest_stn, uint8_t dest_net,
                             uint8_t src_stn, uint8_t src_net,
                             uint8_t ctrl, uint8_t port,
                             const std::vector<uint8_t>& data_payload) {
        // Reset ADLC (NFS ROM sub_c96dc)
        adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RX_RESET | Mc6854::CR1_AC);
        adlc.write(3, 0x1E);  // CR4
        adlc.write(1, 0x00);  // CR3
        adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE);  // CR1=$82
        adlc.write(1, Mc6854::CR2_CLR_RX_ST | Mc6854::CR2_CLR_TX_ST
                     | Mc6854::CR2_FLAG_IDLE | Mc6854::CR2_2_1_BYTE
                     | Mc6854::CR2_PSE);  // CR2=$67

        // TX setup: assert RTS, enable TIE, hold RX in reset
        adlc.write(1, Mc6854::CR2_RTS | Mc6854::CR2_CLR_RX_ST | Mc6854::CR2_CLR_TX_ST
                     | Mc6854::CR2_FLAG_IDLE | Mc6854::CR2_2_1_BYTE
                     | Mc6854::CR2_PSE);  // CR2=$E7
        adlc.write(0, Mc6854::CR1_TIE | Mc6854::CR1_RX_RESET);  // CR1=$44

        // Write scout frame: all bytes via continue, then TX_LAST_DATA
        write_tx_with_drain(dest_stn);
        write_tx_with_drain(dest_net);
        write_tx_with_drain(src_stn);
        write_tx_with_drain(src_net);
        write_tx_with_drain(ctrl);
        write_tx_with_drain(port);
        // Signal frame end via TX_LAST_DATA (as the NFS ROM does)
        adlc.write(1, Mc6854::CR2_TX_LAST_DATA | Mc6854::CR2_RTS
                     | Mc6854::CR2_CLR_RX_ST | Mc6854::CR2_FLAG_IDLE
                     | Mc6854::CR2_2_1_BYTE | Mc6854::CR2_PSE);

        // Wait for scout ack timeout. Handshake ticks happen on rising edges.
        // Each half-cycle pair (rising+falling) = 1 handshake tick.
        tick_n(FourWayHandshake::SCOUT_ACK_TIMEOUT * 2);

        // Clear RX_RESET to receive the fake scout ack
        adlc.write(0, Mc6854::CR1_TIE);
        adlc.write(1, Mc6854::CR2_RTS | Mc6854::CR2_CLR_RX_ST
                     | Mc6854::CR2_FLAG_IDLE | Mc6854::CR2_2_1_BYTE
                     | Mc6854::CR2_PSE);

        // Tick enough for all 4 scout ack bytes to be delivered
        tick_byte_periods(5);

        // Read and discard the 4 scout ack bytes from the RX FIFO
        for (int i = 0; i < 4; ++i) {
            adlc.read(2);
        }
        // Clear FV from the scout ack
        adlc.write(1, Mc6854::CR2_RTS | Mc6854::CR2_CLR_RX_ST
                     | Mc6854::CR2_FLAG_IDLE | Mc6854::CR2_2_1_BYTE
                     | Mc6854::CR2_PSE);

        // Put RX back in reset for data TX
        adlc.write(0, Mc6854::CR1_TIE | Mc6854::CR1_RX_RESET);

        // Write data frame: address bytes + payload, draining FIFO as needed
        write_tx_with_drain(dest_stn);
        write_tx_with_drain(dest_net);
        write_tx_with_drain(src_stn);
        write_tx_with_drain(src_net);
        for (auto byte : data_payload) {
            write_tx_with_drain(byte);
        }

        // TX_LAST_DATA + CLR_RX_ST (matching NFS ROM CR2=$3F, no RTS)
        adlc.write(1, Mc6854::CR2_TX_LAST_DATA | Mc6854::CR2_CLR_RX_ST
                     | Mc6854::CR2_FC_TDRA | Mc6854::CR2_FLAG_IDLE
                     | Mc6854::CR2_2_1_BYTE | Mc6854::CR2_PSE);

        // Switch to receive mode: CR1=$82 (TX_RESET | RIE)
        adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE);
    }
};

}  // namespace

TEST_CASE("ADLC+Handshake: fake final ack delivered after FINAL_ACK_TIMEOUT", "[econet][mc6854][handshake]") {
    HandshakeFixture t;

    t.send_scout_and_data(254, 0, 101, 0, 0x80, 0x99, {0xAA, 0xBB});

    CHECK(t.handshake.stage() == FourWayHandshake::Stage::DataSent);
    CHECK(t.adlc.cr1() == (Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE));

    // Verify FV is clear (CLR_RX_ST was in the TX_LAST_DATA write)
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK(t.adlc.rx_fifo_empty());

    // Tick until the final ack timer fires
    t.tick_n(FourWayHandshake::FINAL_ACK_TIMEOUT * 2);

    CHECK(t.handshake.stage() == FourWayHandshake::Stage::WaitForIdle);

    // Tick enough byte periods for the fake ack to be delivered to the RX FIFO
    t.tick_byte_periods(2);

    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    CHECK(t.adlc.irq_output());  // RIE enabled → IRQ should fire

    // Read the fake ack: dest_stn, dest_net (first 2 bytes in FIFO)
    uint8_t byte0 = t.adlc.read(2);
    uint8_t byte1 = t.adlc.read(2);

    // Tick to push remaining bytes into FIFO
    t.tick_byte_periods(3);

    uint8_t byte2 = t.adlc.read(2);
    uint8_t byte3 = t.adlc.read(2);

    CHECK(byte0 == 101);  // dest = us (our station)
    CHECK(byte1 == 0);    // dest net
    CHECK(byte2 == 254);  // src = them (remote station)
    CHECK(byte3 == 0);    // src net
}

TEST_CASE("ADLC+Handshake: FV blocks rx delivery until cleared", "[econet][mc6854][handshake]") {
    // Verify that fv_stored_ prevents a second frame from being pushed
    // into the RX FIFO, and that CLR_RX_ST unblocks delivery.
    // Uses direct TestBackend → Mc6854 (no handshake) because the handshake
    // only passes typed AUN packets, not raw frames.
    //
    // On real hardware, frames are longer than the 3-deep FIFO. The CPU
    // drains the FIFO via interrupt-driven reads as bytes arrive — exactly
    // as modelled here with interleaved tick/read sequences.
    TestFixture t;

    t.release_reset();
    t.adlc.write(0, Mc6854::CR1_RIE);
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_2_1_BYTE | Mc6854::CR2_FLAG_IDLE);

    // Inject two 4-byte raw frames into the backend
    t.backend.inject_rx_frame({0x65, 0x00, 0xFE, 0x00});  // Frame 1
    t.backend.inject_rx_frame({0xAA, 0xBB, 0xCC, 0xDD});  // Frame 2

    // Drain frame 1 byte-by-byte, interleaving ticks and reads as the
    // real CPU's NMI handler would. Check RDA via SR1 since PSE filtering
    // suppresses SR2_RDA when higher-priority bits (AP, FV) are present.
    std::vector<uint8_t> frame1_bytes;
    for (int i = 0; i < 4; ++i) {
        t.tick_byte_periods(1);
        CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
        frame1_bytes.push_back(t.adlc.read(2));
    }
    CHECK(frame1_bytes == std::vector<uint8_t>{0x65, 0x00, 0xFE, 0x00});

    // FV was set by byte timer (closing flag delay after last byte pushed)
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);

    // Ticks shouldn't deliver second frame while FV is stored
    t.tick_byte_periods(4);
    CHECK(t.adlc.rx_fifo_empty());

    // Clear FV via CLR_RX_ST
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST | Mc6854::CR2_PSE
                   | Mc6854::CR2_2_1_BYTE | Mc6854::CR2_FLAG_IDLE);

    // Now frame 2 should be delivered
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    uint8_t b0 = t.adlc.read(2);
    CHECK(b0 == 0xAA);
}

// =============================================================================
// Phase 7: NFS ROM hardware-behaviour tests
//
// These tests verify specific MC6854 behaviours that the NFS 3.34 ROM
// depends on for correct operation. Each test is derived directly from
// analysis of the NFS ROM disassembly at the referenced addresses.
// =============================================================================

TEST_CASE("NFS: CTS present in SR1 when connected and RTS not asserted", "[econet][mc6854][nfs]") {
    // The NFS ROM INACTIVE polling loop at $9C66-$9C6B checks SR1 bit4 (CTS)
    // after clearing status with CR2=$67. With RTS not asserted (bit7=0 in
    // CR2=$67), CTS input is HIGH because cts_input_ = !(connected && RTS).
    // CTS present feeds directly into SR1_CTS, so bit4 must be set for
    // the BNE $9CA2 to branch and start transmission.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Write CR2=$67: CLR_TX_ST | CLR_RX_ST | FC_TDRA | 2_1_BYTE | PSE
    // Note: RTS (bit7) is NOT set — this is the idle/listen configuration
    t.adlc.write(1, 0x67);
    t.tick_one_byte_period();

    // SR1 CTS bit should be set (CTS input HIGH = not clear to send,
    // because RTS is not asserted)
    CHECK(t.adlc.sr1() & Mc6854::SR1_CTS);
}

TEST_CASE("NFS: CTS clears in SR1 when RTS asserted", "[econet][mc6854][nfs]") {
    // The NFS ROM TX preparation at $9CA2 writes CR2=$E7 (with RTS bit7=1).
    // After asserting RTS, CTS input goes LOW (clear to send), so CTS
    // should NOT appear in SR1. This ensures TDRA can report TX readiness.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Assert RTS: CR2=$E7 (RTS | CLR_TX_ST | CLR_RX_ST | FC_TDRA | 2_1_BYTE | PSE)
    t.adlc.write(1, 0xE7);
    t.tick_one_byte_period();

    // CTS bit should NOT be set (CTS input LOW = clear to send)
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_CTS);
}

TEST_CASE("NFS: TDRA and IRQ set when TIE enabled and TX active", "[econet][mc6854][nfs]") {
    // The NFS ROM TX path writes CR1=$44 (TIE enabled, RX reset) and
    // CR2=$E7 (RTS asserted). The NMI TX handler at $9D4C checks:
    //   BIT $FEA0  ; reads SR1, V=bit6(TDRA), N=bit7(IRQ)
    //   BVC $9D76  ; if TDRA not set, error
    // So TDRA (SR1 bit6) and IRQ (SR1 bit7) must be set when TX FIFO
    // is empty, TIE enabled, TX not in reset, CTS low, and carrier present.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Prepare TX: CR2=$E7 (RTS, clear status)
    t.adlc.write(1, 0xE7);
    // Enable TX: CR1=$44 (RX_RESET | TIE, TX released from reset)
    t.adlc.write(0, 0x44);
    t.tick_one_byte_period();

    uint8_t sr1 = t.adlc.sr1();
    CHECK(sr1 & Mc6854::SR1_TDRA);  // TX FIFO empty -> TDRA set
    CHECK(sr1 & Mc6854::SR1_IRQ);   // TIE enabled + TDRA -> IRQ asserted
}

TEST_CASE("NFS: TDRA clears when TX FIFO full in 2-byte mode", "[econet][mc6854][nfs]") {
    // The NFS ROM TX handler writes 2 bytes per NMI. After writing 2 bytes
    // to a 3-deep FIFO (now 2 entries used), TDRA should clear in 2-byte
    // mode because only 1 slot remains (need room for 2).
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // TX setup: CR2=$E7 (RTS, 2_1_BYTE), CR1=$44 (TIE, RX_RESET)
    t.adlc.write(1, 0xE7);
    t.adlc.write(0, 0x44);
    t.tick_one_byte_period();

    // Write 1 byte — FIFO has 1 entry, room for 2 more -> TDRA set
    t.adlc.write(2, 0x65);
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);

    // Write 2nd byte — FIFO has 2 entries, room for 1 -> TDRA clear (need 2)
    t.adlc.write(2, 0x00);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TDRA);
}

TEST_CASE("NFS: TX to RX mode switch enables frame reception", "[econet][mc6854][nfs]") {
    // After TX completes, the NFS ROM at $9D94 writes CR1=$82 to switch
    // from TX mode (CR1=$44) to RX mode. CR1=$82 = TX_RESET | RIE.
    // A frame injected after this switch should be receivable.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Start in TX mode: CR1=$44, CR2=$E7
    t.adlc.write(1, 0xE7);
    t.adlc.write(0, 0x44);
    t.tick_one_byte_period();

    // Write a scout frame (2 bytes + TX_LAST_DATA) and send it
    t.adlc.write(2, 0x65);
    t.adlc.write(2, 0x00);
    t.tick_byte_periods(2);
    // Terminate with TX_LAST_DATA: CR2=$3F
    t.adlc.write(1, 0x3F);
    t.tick_byte_periods(2);

    // Switch to RX mode: CR1=$82 (TX_RESET, RIE)
    t.adlc.write(0, 0x82);

    // Inject a reply frame
    t.backend.inject_rx_frame({0xFE, 0x00, 0x65, 0x00});
    t.tick_byte_periods(1);

    // RDA should be set and first byte readable
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    CHECK(t.adlc.read(2) == 0xFE);
}

TEST_CASE("NFS: PSE filtering suppresses RDA behind AP in SR2", "[econet][mc6854][nfs]") {
    // The NFS ROM RX handler at $974C reads SR2 and tests bit7 (RDA) via BPL.
    // With PSE enabled and AP set on the first received byte, RDA is
    // suppressed in SR2 (AP is P3, RDA is P4 = lowest priority).
    // SR1_RDA is unaffected by PSE, so the NFS ROM can still detect data
    // via SR1. This test verifies the PSE suppression is correct.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Configure with PSE: CR2=$67 (PSE | 2_1_BYTE | ...)
    t.adlc.write(0, Mc6854::CR1_RIE);
    t.adlc.write(1, 0x67);

    // Inject a frame — first byte will have AP flag
    t.backend.inject_rx_frame({0x65, 0x00, 0xFE, 0x00});
    t.tick_byte_periods(1);

    // SR1 shows RDA (not affected by PSE)
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);

    // SR2: AP is set (P3), which suppresses RDA (P4) under PSE
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_RDA);
}

TEST_CASE("NFS: IRQ asserted when RIE enabled and RDA present", "[econet][mc6854][nfs]") {
    // The NFS ROM relies on NMI being asserted when received data is
    // available and RIE is enabled (CR1=$82). IRQ = RIE AND (RDA OR S2RQ).
    // This drives the NMI line which triggers the NFS ROM's NMI handler.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // RX listen mode: CR1=$82 (TX_RESET | RIE), CR2=$67
    t.adlc.write(0, 0x82);
    t.adlc.write(1, 0x67);

    // No data yet — IRQ should not be asserted
    t.tick_one_byte_period();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_IRQ);

    // Inject a frame and tick
    t.backend.inject_rx_frame({0x65, 0x00});
    t.tick_byte_periods(1);

    // IRQ should now be asserted (RIE enabled, RDA present)
    CHECK(t.adlc.sr1() & Mc6854::SR1_IRQ);
    CHECK(t.adlc.irq_output());
}

TEST_CASE("NFS: CLR_RX_ST in TX_LAST_DATA clears FV for subsequent reception", "[econet][mc6854][nfs]") {
    // At $9D88, the NFS ROM writes CR2=$3F which includes both TX_LAST_DATA
    // (bit4) and CLR_RX_ST (bit5). CLR_RX_ST clears fv_stored_, ensuring
    // the ADLC is ready to receive the reply frame after switching to RX mode
    // with CR1=$82. Without this, stale FV from a previous frame would
    // block RX delivery.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Set up RX and receive a frame to set FV
    t.adlc.write(0, Mc6854::CR1_RIE);
    t.adlc.write(1, 0x67);
    t.backend.inject_rx_frame({0x65, 0x00});
    t.tick_byte_periods(2);
    t.adlc.read(2);
    t.adlc.read(2);  // Last byte — FV promoted immediately
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);

    // Now simulate the TX_LAST_DATA write: CR2=$3F
    // This includes CLR_RX_ST which should clear FV
    t.adlc.write(1, 0x3F);
    t.tick_one_byte_period();
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // Subsequent RX should work (FV no longer blocking)
    t.adlc.write(0, 0x82);  // RX mode
    t.backend.inject_rx_frame({0xAA, 0xBB});
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    CHECK(t.adlc.read(2) == 0xAA);
}

// =============================================================================
// NFS ADLC Init Sequence (Group 1)
//
// The exact register sequence NFS writes at $96DC (adlc_full_reset) followed
// by $96EB (adlc_rx_listen). Tests verify the state after each step.
// =============================================================================

TEST_CASE("NFS Init: CR1=$C1 puts both sections in reset with AC set", "[econet][mc6854][nfs]") {
    // NFS ROM $96DC: STA adlc_cr1 with A=$C1
    // CR1=$C1 = TX_RESET | RX_RESET | AC
    TestFixture t;

    t.adlc.write(0, 0xC1);

    CHECK(t.adlc.cr1() == 0xC1);
    CHECK(t.adlc.cr1() & Mc6854::CR1_TX_RESET);
    CHECK(t.adlc.cr1() & Mc6854::CR1_RX_RESET);
    CHECK(t.adlc.cr1() & Mc6854::CR1_AC);
    CHECK(t.adlc.tx_fifo_empty());
    CHECK(t.adlc.rx_fifo_empty());
    CHECK_FALSE(t.adlc.irq_output());
}

TEST_CASE("NFS Init: CR4=$1E written via offset 3 while AC=1", "[econet][mc6854][nfs]") {
    // NFS ROM $96E0: STA adlc_cr4 with A=$1E while AC=1
    // $1E = ABT_EXT | TX_ABT | RX_WORD_8_6 | WORD_8_6
    // ABT_EXT (bit 4) auto-clears, so CR4 stores $0E
    TestFixture t;

    t.adlc.write(0, 0xC1);  // CR1 = TX_RESET | RX_RESET | AC
    t.adlc.write(3, 0x1E);  // CR4 via offset 3 (AC=1)

    CHECK(t.adlc.cr4() == 0x0E);  // ABT_EXT auto-cleared
    CHECK(t.adlc.cr4() & Mc6854::CR4_TX_ABT);
    CHECK(t.adlc.cr4() & Mc6854::CR4_RX_WORD_8_6);
    CHECK(t.adlc.cr4() & Mc6854::CR4_WORD_8_6);
    CHECK_FALSE(t.adlc.cr4() & Mc6854::CR4_ABT_EXT);
}

TEST_CASE("NFS Init: CR3=$00 written via offset 1 while AC=1", "[econet][mc6854][nfs]") {
    // NFS ROM $96E4: STA adlc_cr2 (offset 1) with A=$00 while AC=1
    // AC=1 routes offset 1 to CR3, not CR2
    TestFixture t;

    t.adlc.write(0, 0xC1);  // CR1 = TX_RESET | RX_RESET | AC
    t.adlc.write(3, 0x1E);  // CR4
    t.adlc.write(1, 0x00);  // CR3 via offset 1 (AC=1)

    CHECK(t.adlc.cr3() == 0x00);
    CHECK(t.adlc.cr4() == 0x0E);  // CR4 unaffected
}

TEST_CASE("NFS Init: CR1=$82 releases RX with RIE, TX stays in reset", "[econet][mc6854][nfs]") {
    // NFS ROM $96EB: STA adlc_cr1 with A=$82
    // CR1=$82 = TX_RESET | RIE. AC cleared, RX released from reset.
    TestFixture t;

    // Full init sequence
    t.adlc.write(0, 0xC1);
    t.adlc.write(3, 0x1E);
    t.adlc.write(1, 0x00);
    t.adlc.write(0, 0x82);  // CR1=$82

    CHECK(t.adlc.cr1() == 0x82);
    CHECK(t.adlc.cr1() & Mc6854::CR1_TX_RESET);
    CHECK_FALSE(t.adlc.cr1() & Mc6854::CR1_RX_RESET);
    CHECK(t.adlc.cr1() & Mc6854::CR1_RIE);
    CHECK_FALSE(t.adlc.cr1() & Mc6854::CR1_AC);
}

TEST_CASE("NFS Init: CR2=$67 clears all status and enables PSE/2-byte mode", "[econet][mc6854][nfs]") {
    // NFS ROM $96EF: STA adlc_cr2 with A=$67
    // $67 = CLR_TX_ST | CLR_RX_ST | FLAG_IDLE | 2_1_BYTE | PSE
    // Auto-clear bits (CLR_RX_ST=$20, CLR_TX_ST=$40) stripped: stored = $07
    TestFixture t;

    // Full init sequence
    t.adlc.write(0, 0xC1);
    t.adlc.write(3, 0x1E);
    t.adlc.write(1, 0x00);
    t.adlc.write(0, 0x82);
    t.adlc.write(1, 0x67);  // CR2=$67

    CHECK(t.adlc.cr2() == 0x07);  // Auto-clear bits stripped
    CHECK(t.adlc.cr2() & Mc6854::CR2_PSE);
    CHECK(t.adlc.cr2() & Mc6854::CR2_2_1_BYTE);
    CHECK(t.adlc.cr2() & Mc6854::CR2_FLAG_IDLE);
    CHECK_FALSE(t.adlc.cr2() & Mc6854::CR2_CLR_RX_ST);
    CHECK_FALSE(t.adlc.cr2() & Mc6854::CR2_CLR_TX_ST);

    // FV and CTS stored latches should be cleared
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);
}

// =============================================================================
// Scout Rejection via DISCONTINUE (Group 2)
//
// NFS writes CR1=$A2 (RIE|DISCONTINUE) to reject misaddressed scouts at $9723.
// =============================================================================

TEST_CASE("NFS DISCONTINUE: mid-frame clears RX FIFO and frame buffer", "[econet][mc6854][nfs]") {
    // DISCONTINUE discards current RX frame: FIFO cleared, frame field idle,
    // FV cleared, DISCONTINUE auto-clears leaving RIE set.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Inject a frame and let some bytes arrive
    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42, 0x99, 0xAA});
    t.tick_byte_periods(2);  // Push 2 bytes into FIFO

    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);

    // Write CR1=$A2: RIE | DISCONTINUE (NFS ROM $9723)
    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_DISCONTINUE | Mc6854::CR1_RIE);

    CHECK(t.adlc.rx_fifo_empty());
    CHECK(t.adlc.rx_frame_field() == Mc6854::FrameField::Idle);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);
    // DISCONTINUE auto-clears, RIE and TX_RESET remain
    CHECK(t.adlc.cr1() == (Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE));
    CHECK_FALSE(t.adlc.cr1() & Mc6854::CR1_DISCONTINUE);
}

TEST_CASE("NFS DISCONTINUE: followed by new frame reception works", "[econet][mc6854][nfs]") {
    // First frame discarded via DISCONTINUE, second frame receivable with AP.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // First frame — will be discarded
    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});
    // Second frame — should be receivable after DISCONTINUE
    t.backend.inject_rx_frame({0xAA, 0xBB, 0xCC, 0xDD});

    t.tick_byte_periods(2);  // Some bytes from frame 1

    // DISCONTINUE
    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_DISCONTINUE | Mc6854::CR1_RIE);

    // Now tick to receive the second frame
    t.tick_byte_periods(2);

    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);  // AP on first byte of new frame
    CHECK(t.adlc.read(2) == 0xAA);
}

TEST_CASE("NFS DISCONTINUE: clears FV from partially-received previous frame", "[econet][mc6854][nfs]") {
    // If FV was set from the previous frame (e.g. all bytes pushed),
    // DISCONTINUE should clear it.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Short frame — all bytes will be pushed, setting FV
    t.backend.inject_rx_frame({0xFF, 0x01});
    t.tick_byte_periods(3);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);

    // DISCONTINUE clears FV
    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_DISCONTINUE | Mc6854::CR1_RIE);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK(t.adlc.rx_fifo_empty());
}

// =============================================================================
// PSE Disable at Scout Completion (Group 3)
//
// NFS writes CR2=$66 (without PSE) at $9771 to read raw SR2 after scout data.
// =============================================================================

TEST_CASE("NFS PSE Disable: reveals unfiltered SR2 with FV and RDA together", "[econet][mc6854][nfs]") {
    // After clearing PSE bit, both FV and RDA are visible simultaneously.
    // Without PSE, P1 (FV) does not mask P4 (RDA).
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Start with PSE enabled
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_2_1_BYTE | Mc6854::CR2_FLAG_IDLE);

    // Inject a frame and push all bytes including last
    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});
    t.tick_byte_periods(3);  // Push 3 bytes

    // Read byte 0 — inline refill pushes byte 3 (LAST), FV set
    t.adlc.read(2);
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_RDA);  // PSE masks RDA behind FV

    // Disable PSE: CR2=$66 (without PSE bit, NFS ROM $9771)
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST | Mc6854::CR2_CLR_TX_ST
                   | Mc6854::CR2_2_1_BYTE | Mc6854::CR2_FLAG_IDLE);

    // FV was cleared by CLR_RX_ST. The last byte (byte 3) is still in FIFO
    // marked as LAST. We need to re-trigger FV by reading it.
    // Re-inject frame for a cleaner test of PSE-off behaviour.

    // Actually, let's test the simpler case: disable PSE while FV and RDA
    // are both present. Use a fresh setup.
    TestFixture t2;
    t2.release_reset();
    t2.adlc.set_byte_period(4);

    t2.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});
    t2.tick_byte_periods(3);
    t2.adlc.read(2);  // byte 0 — inline refill: byte 3 (LAST) → FV set

    // PSE not enabled — both FV and RDA should be visible
    CHECK(t2.adlc.sr2() & Mc6854::SR2_FV);
    CHECK(t2.adlc.sr2() & Mc6854::SR2_RDA);
}

TEST_CASE("NFS PSE Disable: FV and RDA visible together without PSE", "[econet][mc6854][nfs]") {
    // Without PSE, P1 (FV) does not mask P4 (RDA). Both bits visible
    // simultaneously when last byte is in FIFO and data remains.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // PSE NOT enabled (cr2 = 0 by default)
    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});

    // Push 3 bytes via timer, then read byte 0 (inline refill pushes byte 3 LAST)
    t.tick_byte_periods(3);
    t.adlc.read(2);  // byte 0 → inline refill: byte 3 (LAST), FV set

    // Without PSE, both FV and RDA should be visible
    uint8_t sr2 = t.adlc.sr2();
    CHECK(sr2 & Mc6854::SR2_FV);
    CHECK(sr2 & Mc6854::SR2_RDA);

    // Read remaining bytes
    CHECK(t.adlc.read(2) == 0x01);
    CHECK(t.adlc.read(2) == 0x80);
    CHECK(t.adlc.read(2) == 0x42);

    // After all read, FV still set, RDA clear
    sr2 = t.adlc.sr2();
    CHECK(sr2 & Mc6854::SR2_FV);
    CHECK_FALSE(sr2 & Mc6854::SR2_RDA);
}

// =============================================================================
// NFS TX Setup Sequence (Group 4)
//
// The exact TX init sequence from the BRIANX area ($9C40+).
// =============================================================================

TEST_CASE("NFS TX Setup: releasing TX_RESET with TIE+RIE fires IRQ from TDRA", "[econet][mc6854][nfs]") {
    // NFS ROM sequence: CR2=$E7 (RTS asserted first), then CR1=$44
    // (TIE|RX_RESET, releases TX_RESET). Releasing TX_RESET with RTS
    // asserted triggers immediate TDRA → IRQ via TIE.
    TestFixture t;

    // First assert RTS via CR2 — this ensures CTS is low before TIE
    // is enabled, matching the NFS ROM's actual sequence.
    t.adlc.write(1, Mc6854::CR2_RTS | Mc6854::CR2_CLR_RX_ST
                   | Mc6854::CR2_2_1_BYTE | Mc6854::CR2_FLAG_IDLE | Mc6854::CR2_PSE);
    t.tick();
    CHECK_FALSE(t.adlc.irq_output());  // No interrupt enables yet

    // CR1=$44: TIE + RX_RESET (releases TX_RESET, keeps RX in reset)
    t.adlc.write(0, Mc6854::CR1_TIE | Mc6854::CR1_RX_RESET);
    t.tick();

    // TDRA should fire immediately and trigger IRQ via TIE
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK(t.adlc.irq_output());
    CHECK(t.adlc.sr1() & Mc6854::SR1_IRQ);
}

TEST_CASE("NFS TX Setup: writing TX bytes in 2-byte mode decrements TDRA correctly", "[econet][mc6854][nfs]") {
    // In 2-byte mode (CR2 bit1), TDRA requires room for 2 bytes.
    // First byte: TDRA still set (2 slots remain); second byte: TDRA clears
    // (only 1 slot left, need room for 2).
    TestFixture t;

    t.adlc.write(0, Mc6854::CR1_TIE);  // TIE, release both resets
    t.adlc.write(1, Mc6854::CR2_RTS | Mc6854::CR2_2_1_BYTE);
    t.tick();

    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);

    // Write byte 1: FIFO has 1 entry, room for 2 → TDRA still set
    t.adlc.write(2, 0x65);
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);

    // Write byte 2: FIFO has 2 entries, room for 1 → TDRA clears (need 2)
    t.adlc.write(2, 0x00);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TDRA);
}

TEST_CASE("NFS TX Setup: TX_RESET re-asserted after frame stops TDRA IRQs", "[econet][mc6854][nfs]") {
    // NFS ROM writes CR1=$86 (TX_RESET|RIE|TIE) after sending a frame.
    // TX_RESET kills TDRA immediately, dropping the IRQ.
    TestFixture t;

    t.adlc.write(0, Mc6854::CR1_TIE);
    t.adlc.write(1, Mc6854::CR2_RTS | Mc6854::CR2_2_1_BYTE);
    t.tick();
    CHECK(t.adlc.irq_output());  // TDRA + TIE → IRQ

    // Send a frame
    t.adlc.write(2, 0x65);
    t.adlc.write(2, 0x00);
    t.clear_ac();
    t.adlc.write(3, 0x80);  // Last byte → flush

    // FIFO empty after flush → TDRA set again → IRQ
    CHECK(t.adlc.sr1() & Mc6854::SR1_TDRA);
    CHECK(t.adlc.irq_output());

    // Re-assert TX_RESET: CR1=$86
    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE | Mc6854::CR1_TIE);
    t.tick();

    // TDRA killed by TX_RESET → no IRQ (assuming no RX conditions)
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TDRA);
    // IRQ depends on whether S2RQ has any RX conditions. With RX released
    // from reset and an idle line, INACTIVE may trigger S2RQ via RIE.
    // The key point is TDRA is gone.
}

// =============================================================================
// S2RQ IRQ Participation (Group 5)
//
// NFS depends on NMI firing when AP or FV appear. S2RQ is the intermediate
// bit that gates these through to IRQ via RIE.
// =============================================================================

TEST_CASE("NFS S2RQ: AP present triggers S2RQ which fires IRQ via RIE", "[econet][mc6854][nfs]") {
    // First byte of frame has AP → S2RQ set → with RIE, IRQ fires.
    TestFixture t;
    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE);  // CR1=$82
    t.adlc.write(1, Mc6854::CR2_PSE | Mc6854::CR2_2_1_BYTE | Mc6854::CR2_FLAG_IDLE);
    t.adlc.set_byte_period(4);

    // Clear the Inactive Idle latched by the quiet line before
    // expecting a frame -- it sits above AP and RDA in the PSE tree.
    t.begin_listening(Mc6854::CR2_PSE);

    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});
    t.tick_byte_periods(1);

    // AP on first byte → S2RQ set
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);
    CHECK(t.adlc.irq_output());
    CHECK(t.adlc.sr1() & Mc6854::SR1_IRQ);
}

TEST_CASE("NFS S2RQ: FV triggers S2RQ which fires IRQ via RIE", "[econet][mc6854][nfs]") {
    // End of frame → FV → S2RQ → IRQ (via RIE).
    TestFixture t;
    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE);
    t.adlc.set_byte_period(4);

    t.backend.inject_rx_frame({0xFF, 0x01});
    t.tick_byte_periods(3);

    // Clear AP from first byte so S2RQ is only from FV
    t.adlc.read(2);  // byte 0
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.tick();

    // FV should re-assert when we read the last byte
    t.adlc.read(2);  // byte 1 (last) — re-triggers FV
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);
    CHECK(t.adlc.irq_output());
}

TEST_CASE("NFS S2RQ: RDA alone does NOT trigger S2RQ", "[econet][mc6854][nfs]") {
    // Data byte without AP: RDA is set but S2RQ should be clear.
    // IRQ still fires via the direct RDA path in RIE, but S2RQ itself is clear.
    TestFixture t;
    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE);
    t.adlc.set_byte_period(4);

    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42});
    t.tick_byte_periods(1);  // AP byte arrives

    // Read AP byte and clear status
    t.adlc.read(2);
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);

    // Tick to push non-AP data byte
    t.tick_byte_periods(1);

    // RDA should be set (data available), but S2RQ should be clear
    // because RDA does not participate in S2RQ
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_S2RQ);

    // IRQ still fires via RIE + RDA direct path
    CHECK(t.adlc.irq_output());
}

TEST_CASE("NFS S2RQ: INACTIVE does trigger S2RQ", "[econet][mc6854][nfs]") {
    // This test previously asserted the opposite, citing the datasheet. The
    // datasheet says otherwise: SR1 b1 is "All the status bits (stored
    // conditions) of status register #2 (except RDA bit) ... logically ORed",
    // and Inactive Idle Received is a stored condition of SR2. Only RDA is
    // excluded.
    //
    // It matters well beyond pedantry. This is the interrupt by which a
    // transmitting station learns that nobody answered its scout: NFS enables
    // the receiver and waits, and a line that falls idle is the signal it
    // turns into "not listening". Suppressing it here left the ROM waiting
    // for ever. See docs/discussion/aun-robustness.md defect 2.
    TestFixture t;
    t.release_reset();
    t.tick();

    // No data, RX active, line idle → Inactive Idle latched
    CHECK(t.adlc.sr2() & Mc6854::SR2_INACTIVE);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);

    // ... and it is a latch, so clearing status clears it, and a line that
    // simply stays idle does not re-assert it. That is what keeps a quiet
    // network from storming the CPU with NMIs.
    t.begin_listening();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_S2RQ);
    t.tick();
    t.tick();
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_S2RQ);
}

// =============================================================================
// TX-to-RX Mode Switch (Group 6)
//
// NFS transitions CR1=$86 then CR1=$82 after sending data.
// =============================================================================

TEST_CASE("NFS Mode Switch: CR1=$86 then CR1=$82 transitions cleanly", "[econet][mc6854][nfs]") {
    // CR1=$86 briefly has TIE|TX_RESET|RIE — TX_RESET prevents TDRA,
    // so no TDRA IRQ. Then CR1=$82 (TX_RESET|RIE) clears TIE for RX-only.
    TestFixture t;

    // Start in TX mode with active frame
    t.adlc.write(0, Mc6854::CR1_TIE);
    t.adlc.write(1, Mc6854::CR2_RTS | Mc6854::CR2_2_1_BYTE);
    t.tick();
    CHECK(t.adlc.irq_output());  // TDRA + TIE

    // Send a frame
    t.adlc.write(2, 0x65);
    t.adlc.write(2, 0x00);
    t.clear_ac();
    t.adlc.write(3, 0x80);
    REQUIRE(t.backend.sent_frame_count() == 1);

    // CR1=$86: TX_RESET asserted, TIE+RIE enabled
    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE | Mc6854::CR1_TIE);
    t.tick();

    CHECK(t.adlc.cr1() & Mc6854::CR1_TX_RESET);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_TDRA);  // TX in reset → no TDRA

    // CR1=$82: TX_RESET remains, TIE cleared, RIE only
    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE);
    t.tick();

    CHECK(t.adlc.cr1() == (Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE));
    CHECK_FALSE(t.adlc.cr1() & Mc6854::CR1_TIE);

    // Can now receive frames
    t.adlc.set_byte_period(4);
    t.backend.inject_rx_frame({0xAA, 0xBB, 0xCC, 0xDD});
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    CHECK(t.adlc.read(2) == 0xAA);
}

// =============================================================================
// Error Recovery (Group 7)
//
// NFS error path at $9737 performs full ADLC reset to recover from
// unexpected state.
// =============================================================================

TEST_CASE("NFS Error Recovery: full reset sequence recovers from mid-frame RX", "[econet][mc6854][nfs]") {
    // Frame partially received, then full NFS reset sequence applied.
    // Verify clean state and ability to receive new frames.
    TestFixture t;
    t.adlc.set_byte_period(4);

    // Start receiving
    t.adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE);
    t.adlc.write(1, 0x67);
    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42, 0x99, 0xAA});
    t.tick_byte_periods(2);
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);

    // Full NFS reset sequence (adlc_full_reset + adlc_rx_listen)
    t.adlc.write(0, 0xC1);  // CR1 = TX_RESET | RX_RESET | AC
    t.adlc.write(3, 0x1E);  // CR4
    t.adlc.write(1, 0x00);  // CR3
    t.adlc.write(0, 0x82);  // CR1 = TX_RESET | RIE
    t.adlc.write(1, 0x67);  // CR2

    // Verify clean state
    CHECK(t.adlc.rx_fifo_empty());
    CHECK(t.adlc.rx_frame_field() == Mc6854::FrameField::Idle);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);
    CHECK_FALSE(t.adlc.sr1() & Mc6854::SR1_RDA);

    // Can receive new frames
    t.backend.inject_rx_frame({0xBB, 0xCC, 0xDD, 0xEE});
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    CHECK(t.adlc.read(2) == 0xBB);
}

TEST_CASE("NFS Error Recovery: DISCONTINUE after overrun clears state for retry", "[econet][mc6854][nfs]") {
    // Force overrun (OVRN set) by filling the FIFO without reading,
    // then DISCONTINUE clears state for retry.
    TestFixture t;
    t.release_reset();
    t.adlc.set_byte_period(4);

    // Inject a long frame to cause overrun
    t.backend.inject_rx_frame({0xFF, 0x01, 0x80, 0x42, 0x99, 0xAA, 0xBB, 0xCC});

    // Push bytes until FIFO full without reading → overrun on 4th push
    t.tick_byte_periods(4);  // 3 bytes fill FIFO, 4th causes overrun
    CHECK(t.adlc.ovrn_stored());

    // DISCONTINUE to clear
    t.adlc.write(0, Mc6854::CR1_DISCONTINUE);

    CHECK(t.adlc.rx_fifo_empty());
    CHECK(t.adlc.rx_frame_field() == Mc6854::FrameField::Idle);
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_FV);

    // OVRN is a stored latch — DISCONTINUE doesn't clear it directly,
    // but CLR_RX_ST does
    t.adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    t.tick();
    CHECK_FALSE(t.adlc.sr2() & Mc6854::SR2_OVRN);

    // Can receive new frames
    t.backend.inject_rx_frame({0xDD, 0xEE});
    t.tick_byte_periods(1);
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
    CHECK(t.adlc.read(2) == 0xDD);
}

// =============================================================================
// Reply Scout / Ack Frame (Group 9)
//
// NFS reply scout handler at $9DB2 depends on AP being set on the first
// byte of ack frames.
// =============================================================================

TEST_CASE("NFS Ack Frame: AP set on first byte of fake final ack", "[econet][mc6854][nfs][handshake]") {
    // After TX handshake completes, the 4-byte ack frame has AP on byte 0.
    HandshakeFixture t;

    t.send_scout_and_data(254, 0, 101, 0, 0x80, 0x99, {0xAA, 0xBB});

    // Wait for final ack timeout
    t.tick_n(FourWayHandshake::FINAL_ACK_TIMEOUT * 2);

    // Tick byte periods to deliver the fake ack to the FIFO
    t.tick_byte_periods(2);

    // First byte should have AP set
    CHECK(t.adlc.sr2() & Mc6854::SR2_AP);
    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);
}

TEST_CASE("NFS Ack Frame: 4-byte ack frame sets FV after all bytes pushed", "[econet][mc6854][nfs][handshake]") {
    // FV is set after all 4 ack bytes enter FIFO. The 4-byte frame exceeds
    // the 3-deep FIFO, so reads must be interleaved with ticks to avoid
    // overrun (same as the real NFS ROM's NMI-driven drain pattern).
    HandshakeFixture t;

    t.send_scout_and_data(254, 0, 101, 0, 0x80, 0x99, {0xAA, 0xBB});

    // Wait for final ack timeout
    t.tick_n(FourWayHandshake::FINAL_ACK_TIMEOUT * 2);

    // Tick to push first byte into FIFO
    t.tick_byte_periods(2);

    CHECK(t.adlc.sr1() & Mc6854::SR1_RDA);

    // Read bytes interleaved with ticks (like the NFS ROM's NMI handler)
    uint8_t b0 = t.adlc.read(2);  // inline refill may push next byte
    t.tick_byte_periods(1);
    uint8_t b1 = t.adlc.read(2);
    t.tick_byte_periods(1);
    uint8_t b2 = t.adlc.read(2);
    t.tick_byte_periods(1);
    uint8_t b3 = t.adlc.read(2);

    // Verify it's a valid 4-byte ack frame
    CHECK(b0 == 101);  // dest = us
    CHECK(b1 == 0);    // dest net
    CHECK(b2 == 254);  // src = them
    CHECK(b3 == 0);    // src net

    // FV should be set after reading the last byte (re-triggered by was_last)
    CHECK(t.adlc.sr2() & Mc6854::SR2_FV);
}

// =============================================================================
// Unreachable destinations
// =============================================================================
//
// The four-way handshake synthesises its scout acknowledgement before anything
// has left the machine, so a transmission to a station that does not exist is
// reported to the guest as a success. Telling it the truth means letting the
// line fall idle, which the ADLC latches and turns into the interrupt NFS
// reads as "not listening".
//
// The transition is the whole point: transmitting makes the line busy, and it
// is the *fall* back to idle that latches. A transaction that is simply never
// started leaves the line idle throughout, produces no edge, and so produces
// no interrupt -- which is why suppressing the scout ack on its own merely
// hung the guest. See docs/discussion/aun-robustness.md defect 2.

namespace {

// A backend that can be told which stations exist.
class ReachabilityBackend : public TestBackend {
public:
    void set_reachable(bool reachable) { reachable_ = reachable; }
    bool is_reachable(uint8_t, uint8_t) const override { return reachable_; }

private:
    bool reachable_ = true;
};

// A HandshakeFixture whose backend can report a station absent.
struct UnreachableFixture {
    ReachabilityBackend backend;
    FourWayHandshake handshake;
    Mc6854 adlc;

    UnreachableFixture() : handshake(backend), adlc(handshake) {
        adlc.set_byte_period(4);
    }

    void tick() {
        handshake.tick();
        adlc.tick_rising();
        adlc.tick_falling();
    }

    void tick_n(int n) {
        for (int i = 0; i < n; ++i) tick();
    }

    // Put the ADLC in the listening posture the NFS ROM uses, with receive
    // interrupts enabled and the idle latched by the quiet line cleared.
    void begin_listening() {
        adlc.write(0, Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE);
        (void)adlc.sr2();
        adlc.write(1, Mc6854::CR2_CLR_RX_ST);
    }

    // Hand the handshake a scout frame directly, as the ADLC would once it
    // had assembled one.
    // Consume a frame the way the ROM's NMI handler does: read the bytes out
    // of the FIFO, then read status and clear it. Both halves matter. Until
    // the FIFO is drained the ADLC has nowhere to put new data, and until
    // Frame Valid is cleared it will not accept another frame at all -- so a
    // test that skips either one leaves the ADLC deaf to whatever the
    // transport says next.
    void consume_frame(int max_bytes = 16) {
        for (int i = 0; i < max_bytes; ++i) {
            if (!(adlc.sr1() & Mc6854::SR1_RDA)) {
                tick();
                if (!(adlc.sr1() & Mc6854::SR1_RDA)) break;
            }
            (void)adlc.read(2);
            tick();
        }
        (void)adlc.sr2();
        adlc.write(1, Mc6854::CR2_CLR_RX_ST);
        tick();
    }

    void transmit_scout(uint8_t dest_stn, uint8_t dest_net) {
        NetworkFrame frame;
        frame.type = FrameType::RawFrame;
        frame.data = {dest_stn, dest_net, 1, 0, 0x80, 0x99};
        handshake.send_frame(frame);
    }
};

}  // namespace

TEST_CASE("Unreachable: no scout ack is synthesised for an absent station",
          "[econet][mc6854][handshake][reachability]") {
    UnreachableFixture t;
    t.begin_listening();
    t.backend.set_reachable(false);

    t.transmit_scout(99, 0);
    t.tick_n(FourWayHandshake::SCOUT_ACK_TIMEOUT * 2);

    // Nothing was sent, and the guest was not told its scout was answered.
    CHECK(t.backend.sent_network_frames().empty());
    CHECK(t.handshake.scout_ack_generated_count() == 0);
}

TEST_CASE("Unreachable: the line falls idle, raising the interrupt NFS reads "
          "as not listening",
          "[econet][mc6854][handshake][reachability]") {
    UnreachableFixture t;
    t.begin_listening();
    t.backend.set_reachable(false);

    // Nothing latched yet: the listening posture cleared it, and a line that
    // merely stays idle does not re-assert.
    t.tick_n(4);
    REQUIRE_FALSE(t.adlc.sr1() & Mc6854::SR1_S2RQ);

    t.transmit_scout(99, 0);

    // The transmission holds the line, then it falls idle. That fall is the
    // edge the ADLC latches, and with RIE set it raises IRQ -- the signal the
    // ROM's NMI error path turns into "not listening".
    t.tick_n(FourWayHandshake::SCOUT_ACK_TIMEOUT);

    CHECK(t.adlc.sr2() & Mc6854::SR2_INACTIVE);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);
    CHECK(t.adlc.irq_output());
}

TEST_CASE("Unreachable: a reachable station is unaffected",
          "[econet][mc6854][handshake][reachability]") {
    UnreachableFixture t;
    t.begin_listening();
    t.backend.set_reachable(true);

    t.transmit_scout(254, 0);
    CHECK(t.handshake.stage() == FourWayHandshake::Stage::ScoutSent);

    t.tick_n(FourWayHandshake::SCOUT_ACK_TIMEOUT);
    CHECK(t.handshake.scout_ack_generated_count() == 1);
}

TEST_CASE("Unreachable: a transport that reports failure afterwards is believed",
          "[econet][mc6854][handshake][reachability]") {
    // Piconet cannot know a station is absent before trying -- its firmware
    // runs the wire handshake and reports the result afterwards -- so it says
    // so with a Nack. The handshake must abandon the transaction rather than
    // let its timer synthesise a successful acknowledgement.
    UnreachableFixture t;
    t.begin_listening();
    t.backend.set_reachable(true);

    t.transmit_scout(254, 0);
    t.tick_n(FourWayHandshake::SCOUT_ACK_TIMEOUT);
    t.consume_frame();  // the guest reads the scout ack and clears status
    REQUIRE(t.handshake.stage() == FourWayHandshake::Stage::ScoutAckReceived);

    // The guest sends its data, which reaches the wire and fails there.
    NetworkFrame data;
    data.type = FrameType::RawFrame;
    data.data = {254, 0, 1, 0, 0xAA};
    t.handshake.send_frame(data);
    REQUIRE(t.handshake.stage() == FourWayHandshake::Stage::DataSent);

    NetworkFrame nack;
    nack.type = FrameType::Nack;
    t.backend.inject_rx_network_frame(nack);

    // Well past the point at which a final ack would have been synthesised.
    t.tick_n(FourWayHandshake::FINAL_ACK_TIMEOUT * 2);

    CHECK(t.handshake.failed_tx_count() == 1);
    CHECK(t.handshake.stage() == FourWayHandshake::Stage::Idle);
    CHECK(t.adlc.sr1() & Mc6854::SR1_S2RQ);
    CHECK(t.adlc.irq_output());
}
