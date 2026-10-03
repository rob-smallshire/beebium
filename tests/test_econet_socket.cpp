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

#include "beebium/econet/EconetSocket.hpp"
#include "beebium/econet/EconetConcepts.hpp"
#include "beebium/econet/TestBackend.hpp"
#include "beebium/econet/Mc6854.hpp"
#include "beebium/econet/SpeedGate.hpp"

#include <atomic>
#include <optional>
#include <thread>

using namespace beebium;

// ===========================================================================
// Empty socket behaviour (no Econet hardware fitted)
// ===========================================================================

TEST_CASE("EconetSocket: default-constructed socket is disabled", "[econet][socket]") {
    EconetSocket socket;
    CHECK_FALSE(socket.enabled());
    CHECK(socket.adlc() == nullptr);
}

TEST_CASE("EconetSocket: empty socket station ID read returns 0x00", "[econet][socket]") {
    EconetSocket socket;
    // Slow 1MHz open bus returns 0x00 (pull-down resistors)
    CHECK(socket.read_station_id(0x00) == 0x00);
    CHECK(socket.read_station_id(0x07) == 0x00);
}

TEST_CASE("EconetSocket: empty socket ADLC read returns last bus value", "[econet][socket]") {
    EconetSocket socket;
    uint8_t bus_value = 0xA5;
    socket.set_last_bus_value_ptr(&bus_value);

    CHECK(socket.read_adlc(0x00) == 0xA5);
    CHECK(socket.read_adlc(0x03) == 0xA5);

    bus_value = 0x42;
    CHECK(socket.read_adlc(0x00) == 0x42);
}

TEST_CASE("EconetSocket: empty socket ADLC read returns 0x00 without bus pointer", "[econet][socket]") {
    EconetSocket socket;
    CHECK(socket.read_adlc(0x00) == 0x00);
}

TEST_CASE("EconetSocket: empty socket ADLC write is harmless", "[econet][socket]") {
    EconetSocket socket;
    socket.write_adlc(0x00, 0xFF);  // Should not crash
    socket.write_adlc(0x03, 0x55);
    CHECK_FALSE(socket.enabled());
}

TEST_CASE("EconetSocket: empty socket NMI never pending", "[econet][socket]") {
    EconetSocket socket;
    CHECK_FALSE(socket.nmi_pending());
}

TEST_CASE("EconetSocket: empty socket tick does nothing", "[econet][socket]") {
    EconetSocket socket;
    socket.tick_rising();   // Should not crash
    socket.tick_falling();
    CHECK_FALSE(socket.nmi_pending());
}

TEST_CASE("EconetSocket: empty socket reset does nothing", "[econet][socket]") {
    EconetSocket socket;
    socket.reset();  // Should not crash
    CHECK_FALSE(socket.enabled());
}

// ===========================================================================
// Enable/disable
// ===========================================================================

TEST_CASE("EconetSocket: enable creates ADLC and sets station ID", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(42, std::move(backend));

    CHECK(socket.enabled());
    CHECK(socket.station_id() == 42);
    CHECK(socket.adlc() != nullptr);
}

TEST_CASE("EconetSocket: disable returns to empty state", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(42, std::move(backend));
    CHECK(socket.enabled());

    socket.disable();

    CHECK_FALSE(socket.enabled());
    CHECK(socket.adlc() == nullptr);
}

TEST_CASE("EconetSocket: disable clears NMI enable flip-flop", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(42, std::move(backend));
    socket.on_inton();
    CHECK(socket.nmi_enable_ff());

    socket.disable();
    CHECK_FALSE(socket.nmi_enable_ff());
}

// ===========================================================================
// Station ID region (&FE18-&FE1F)
// ===========================================================================

TEST_CASE("EconetSocket: enabled socket station ID read returns station number", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(254, std::move(backend));

    CHECK(socket.read_station_id(0x00) == 254);
    CHECK(socket.read_station_id(0x07) == 254);
}

TEST_CASE("EconetSocket: station ID read triggers INTOFF", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend));

    socket.on_inton();
    CHECK(socket.nmi_enable_ff());

    socket.read_station_id(0x00);
    CHECK_FALSE(socket.nmi_enable_ff());
}

TEST_CASE("EconetSocket: station ID write triggers INTOFF", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend));

    socket.on_inton();
    CHECK(socket.nmi_enable_ff());

    socket.write_station_id(0x00, 0xFF);
    CHECK_FALSE(socket.nmi_enable_ff());
}

TEST_CASE("EconetSocket: station ID read triggers INTOFF even when disabled", "[econet][socket]") {
    EconetSocket socket;
    socket.on_inton();
    CHECK(socket.nmi_enable_ff());

    socket.read_station_id(0x00);
    CHECK_FALSE(socket.nmi_enable_ff());
}

// ===========================================================================
// ADLC region (&FEA0-&FEBF) — delegation
// ===========================================================================

TEST_CASE("EconetSocket: enabled socket ADLC read delegates to Mc6854", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend));

    // Reading offset 0 returns SR1 from the ADLC
    uint8_t sr1 = socket.read_adlc(0x00);
    // After hard_reset(), SR1 should be 0 (TX/RX in reset, but nothing asserted)
    CHECK(sr1 == socket.adlc()->sr1());
}

TEST_CASE("EconetSocket: enabled socket ADLC write delegates to Mc6854", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend));

    // Write to CR1 (offset 0) — clear resets
    socket.write_adlc(0x00, 0x00);
    CHECK(socket.adlc()->cr1() == 0x00);
}

// ===========================================================================
// INTON/INTOFF NMI gating flip-flop
// ===========================================================================

TEST_CASE("EconetSocket: INTON sets NMI enable flip-flop", "[econet][socket]") {
    EconetSocket socket;
    CHECK_FALSE(socket.nmi_enable_ff());

    socket.on_inton();
    CHECK(socket.nmi_enable_ff());
}

TEST_CASE("EconetSocket: INTOFF via station ID read clears flip-flop", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend));

    socket.on_inton();
    CHECK(socket.nmi_enable_ff());

    socket.read_station_id(0x00);
    CHECK_FALSE(socket.nmi_enable_ff());
}

TEST_CASE("EconetSocket: multiple INTON calls are idempotent", "[econet][socket]") {
    EconetSocket socket;
    socket.on_inton();
    socket.on_inton();
    socket.on_inton();
    CHECK(socket.nmi_enable_ff());
}

// ===========================================================================
// NMI output — requires all three conditions
// ===========================================================================

TEST_CASE("EconetSocket: NMI requires enabled + nmi_ff + ADLC IRQ", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend));

    // Release ADLC from reset and enable TIE so TDRA can fire IRQ
    socket.write_adlc(0x00, Mc6854::CR1_TIE);  // CR1: TIE, clear resets
    // Tick to update status
    socket.tick_rising();

    // ADLC should now have IRQ due to TDRA
    REQUIRE(socket.adlc()->irq_output());

    SECTION("all conditions met → NMI pending") {
        socket.on_inton();
        CHECK(socket.nmi_pending());
    }

    SECTION("NMI enable flip-flop clear → no NMI") {
        // Don't call on_inton()
        CHECK_FALSE(socket.nmi_pending());
    }
}

TEST_CASE("EconetSocket: NMI not pending when disabled even with IRQ conditions", "[econet][socket]") {
    EconetSocket socket;
    socket.on_inton();
    // No ADLC exists, so nmi_pending should be false
    CHECK_FALSE(socket.nmi_pending());
}

TEST_CASE("EconetSocket: NMI not pending when ADLC has no IRQ", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend));

    // ADLC in reset — no IRQ
    socket.on_inton();
    CHECK_FALSE(socket.nmi_pending());
}

// ===========================================================================
// Clock delegation
// ===========================================================================

TEST_CASE("EconetSocket: tick delegates to ADLC when enabled", "[econet][socket]") {
    EconetSocket socket;
    auto backend_ptr = std::make_unique<TestBackend>();
    auto* backend = backend_ptr.get();
    socket.enable(1, std::move(backend_ptr));

    // Release from reset, enable TIE
    socket.write_adlc(0x00, Mc6854::CR1_TIE);

    // Write a 3-byte frame to TX FIFO: addr, addr, last
    socket.write_adlc(0x02, 0xFF);  // TX FIFO: address byte 1
    socket.write_adlc(0x02, 0x01);  // TX FIFO: address byte 2
    socket.write_adlc(0x03, 0x80);  // TX FIFO: last byte (frame terminate)

    // Tick enough times for the byte trickle to drain the FIFO and send the frame
    int byte_period = socket.adlc()->byte_period();
    for (int i = 0; i < byte_period * 6; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }

    // The backend should have received the frame
    CHECK(backend->sent_frame_count() == 1);
    CHECK(backend->sent_frames()[0] == std::vector<uint8_t>{0xFF, 0x01, 0x80});
}

// ===========================================================================
// Reset
// ===========================================================================

TEST_CASE("EconetSocket: reset clears NMI flip-flop", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend));

    socket.on_inton();
    CHECK(socket.nmi_enable_ff());

    socket.reset();
    CHECK_FALSE(socket.nmi_enable_ff());
}

TEST_CASE("EconetSocket: reset hard-resets ADLC", "[econet][socket]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend));

    // Clear ADLC resets, set some state
    socket.write_adlc(0x00, Mc6854::CR1_TIE);
    CHECK(socket.adlc()->cr1() == Mc6854::CR1_TIE);

    socket.reset();

    // After hard_reset, CR1 should be back to TX+RX reset
    CHECK(socket.adlc()->cr1() == (Mc6854::CR1_RX_RESET | Mc6854::CR1_TX_RESET));
}

TEST_CASE("EconetSocket: reset on empty socket is harmless", "[econet][socket]") {
    EconetSocket socket;
    socket.on_inton();
    socket.reset();
    CHECK_FALSE(socket.nmi_enable_ff());
}

// ===========================================================================
// HasEconetSocket concept and econet_present() helper
// ===========================================================================

namespace {

struct HardwareWithEconet {
    EconetSocket econet_socket;
};

struct HardwareWithoutEconet {
    int some_field = 0;
};

}  // namespace

TEST_CASE("EconetConcepts: HasEconetSocket detects econet_socket member", "[econet][concepts]") {
    CHECK(HasEconetSocket<HardwareWithEconet>);
    CHECK_FALSE(HasEconetSocket<HardwareWithoutEconet>);
}

TEST_CASE("EconetConcepts: econet_present returns false for hardware without socket", "[econet][concepts]") {
    HardwareWithoutEconet hw;
    CHECK_FALSE(econet_present(hw));
}

TEST_CASE("EconetConcepts: econet_present returns false when socket disabled", "[econet][concepts]") {
    HardwareWithEconet hw;
    CHECK_FALSE(econet_present(hw));
}

TEST_CASE("EconetConcepts: econet_present returns true when socket enabled", "[econet][concepts]") {
    HardwareWithEconet hw;
    auto backend = std::make_unique<TestBackend>();
    hw.econet_socket.enable(1, std::move(backend));
    CHECK(econet_present(hw));
}

// ===========================================================================
// NFS NMI Gating (Group 8)
//
// NFS relies on INTOFF/INTON to prevent NMI re-entry during the NMI handler.
// ===========================================================================

TEST_CASE("EconetSocket NFS: INTOFF at NMI entry prevents re-entry during handler", "[econet][socket][nfs]") {
    // The NFS ROM's NMI handler reads the station ID register at entry
    // (INTOFF), performs register operations, then calls INTON before exit.
    // During the handler, nmi_pending should be false even though the ADLC
    // IRQ remains asserted.
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(42, std::move(backend));

    // Release ADLC from reset and enable TIE so TDRA fires IRQ
    socket.write_adlc(0x00, Mc6854::CR1_TIE);
    socket.tick_rising();
    REQUIRE(socket.adlc()->irq_output());

    // INTON — NMI is now armed
    socket.on_inton();
    CHECK(socket.nmi_pending());

    // INTOFF via station ID read (NMI handler entry)
    uint8_t station = socket.read_station_id(0x00);
    CHECK(station == 42);
    CHECK_FALSE(socket.nmi_enable_ff());

    // ADLC IRQ still asserted, but NMI should NOT be pending
    REQUIRE(socket.adlc()->irq_output());
    CHECK_FALSE(socket.nmi_pending());

    // Various register operations during handler don't change NMI state
    socket.write_adlc(0x01, 0x67);  // CR2 write
    socket.read_adlc(0x00);         // SR1 read
    socket.read_adlc(0x01);         // SR2 read
    CHECK_FALSE(socket.nmi_pending());

    // INTON restores NMI
    socket.on_inton();
    CHECK(socket.nmi_enable_ff());
    CHECK(socket.nmi_pending());  // IRQ still active → NMI fires again
}

TEST_CASE("EconetSocket NFS: INTOFF via station ID read also returns station number", "[econet][socket][nfs]") {
    // Simultaneous side-effect test: station ID read returns the correct
    // station number AND clears the NMI enable flip-flop.
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(42, std::move(backend));

    socket.on_inton();
    CHECK(socket.nmi_enable_ff());

    uint8_t val = socket.read_station_id(0x00);
    CHECK(val == 42);
    CHECK_FALSE(socket.nmi_enable_ff());
}

TEST_CASE("EconetSocket NFS: IRQ toggle during NMI handler does not cause re-entry", "[econet][socket][nfs]") {
    // INTOFF, clear conditions (IRQ falls), new frame arrives (IRQ rises),
    // still no nmi_pending until INTON.
    EconetSocket socket;
    auto backend_ptr = std::make_unique<TestBackend>();
    auto* backend = backend_ptr.get();
    socket.enable(1, std::move(backend_ptr));

    // Set up: RIE enabled, inject frame to trigger IRQ
    socket.write_adlc(0x00, Mc6854::CR1_TX_RESET | Mc6854::CR1_RIE);
    socket.adlc()->set_byte_period(4);
    backend->inject_rx_frame({0xFF, 0x01, 0x80, 0x42});

    // Tick to push first byte
    for (int i = 0; i < 4; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }
    REQUIRE(socket.adlc()->irq_output());

    // Arm NMI and verify
    socket.on_inton();
    CHECK(socket.nmi_pending());

    // INTOFF — NMI handler entry
    socket.read_station_id(0x00);
    CHECK_FALSE(socket.nmi_pending());

    // Read data and clear status to drop IRQ
    socket.read_adlc(0x02);  // Read byte from FIFO
    socket.write_adlc(0x01, Mc6854::CR2_CLR_RX_ST);

    // Drain remaining bytes to clear RDA
    socket.read_adlc(0x02);
    socket.read_adlc(0x02);
    socket.read_adlc(0x02);
    socket.write_adlc(0x01, Mc6854::CR2_CLR_RX_ST);

    // Inject a NEW frame — this will cause IRQ to rise again on next tick
    backend->inject_rx_frame({0xAA, 0xBB});
    for (int i = 0; i < 4; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }

    // IRQ should be asserted again (new data), but NMI still blocked
    REQUIRE(socket.adlc()->irq_output());
    CHECK_FALSE(socket.nmi_pending());

    // INTON restores NMI
    socket.on_inton();
    CHECK(socket.nmi_pending());
}

TEST_CASE("EconetSocket: a non-real-time transport is never gated by speed",
          "[econet][socket][speed]") {
    EconetSocket socket;
    socket.enable(42, std::make_unique<TestBackend>(), /*aun_mode=*/false,
                  /*requires_real_time=*/false);
    REQUIRE_FALSE(socket.requires_real_time());

    socket.set_emulation_speed(2.0);
    CHECK_FALSE(socket.gated_by_speed());
    socket.set_emulation_speed(0.0);  // unlimited
    CHECK_FALSE(socket.gated_by_speed());
}

TEST_CASE("EconetSocket: a real-time transport is gated when speed != 1x",
          "[econet][socket][speed]") {
    EconetSocket socket;
    socket.enable(42, std::make_unique<TestBackend>(), /*aun_mode=*/false,
                  /*requires_real_time=*/true);
    REQUIRE(socket.requires_real_time());
    CHECK_FALSE(socket.gated_by_speed());  // default real time

    socket.set_emulation_speed(2.0);
    CHECK(socket.gated_by_speed());
    socket.set_emulation_speed(0.5);
    CHECK(socket.gated_by_speed());
    socket.set_emulation_speed(0.0);  // unlimited also gates
    CHECK(socket.gated_by_speed());

    socket.set_emulation_speed(1.0);  // back to real time
    CHECK_FALSE(socket.gated_by_speed());
}

TEST_CASE("SpeedGate severs the wire while gated", "[econet][speed][gate]") {
    TestBackend inner;
    inner.set_connected(true);
    std::atomic<bool> gated{false};
    SpeedGate gate(inner, gated);

    CHECK(gate.is_connected());        // transparent pass-through when open
    CHECK_FALSE(gate.is_receiving_flags());

    gated.store(true);
    CHECK_FALSE(gate.is_connected());  // dead carrier while gated
    CHECK_FALSE(gate.receive_frame().has_value());

    gated.store(false);
    CHECK(gate.is_connected());
}

// ===========================================================================
// Station number in force (#153)
// ===========================================================================
//
// The number the guest is using: what it read from the station links at
// &FE18 on its first read since the last reset (or since Econet was fitted),
// and the configured number until it has read. A changed number -- the
// sidebar or SetStationId moving the links -- is in force only once the guest
// re-reads them after a Break; the INTOFF reads in between, which also read
// &FE18, do not count, since the filing system keeps the number it read at
// reset.

TEST_CASE("EconetSocket: no station is in force with no Econet fitted",
          "[econet][socket][station-in-force]") {
    EconetSocket socket;
    CHECK_FALSE(socket.station_in_force().has_value());
}

TEST_CASE("EconetSocket: the configured station is in force until the guest reads it",
          "[econet][socket][station-in-force]") {
    EconetSocket socket;
    socket.enable(42, std::make_unique<TestBackend>());
    CHECK(socket.station_in_force() == 42);

    // Before the first read, the configured number is what will be read:
    // a change (--station auto settling, say) is followed.
    socket.set_station_id(43);
    CHECK(socket.station_in_force() == 43);

    CHECK(socket.read_station_id(0) == 43);
    CHECK(socket.station_in_force() == 43);
}

TEST_CASE("EconetSocket: a station change is pending until the guest re-reads after a reset",
          "[econet][socket][station-in-force]") {
    EconetSocket socket;
    socket.enable(80, std::make_unique<TestBackend>());
    socket.read_station_id(0);  // the filing system reads its number at boot
    CHECK(socket.station_in_force() == 80);

    socket.set_station_id(81);  // the sidebar moves the links
    CHECK(socket.station_id() == 81);
    CHECK(socket.station_in_force() == 80);

    // INTOFF reads during traffic read the links too, but do not count.
    socket.read_station_id(0);
    socket.read_station_id(3);
    CHECK(socket.station_in_force() == 80);

    // Break: once the guest reads the links again, the new number is in force.
    socket.reset();
    CHECK(socket.station_in_force() == 81);
    CHECK(socket.read_station_id(0) == 81);
    CHECK(socket.station_in_force() == 81);

    socket.set_station_id(82);
    socket.read_station_id(0);
    CHECK(socket.station_in_force() == 81);
}

TEST_CASE("EconetSocket: disabling Econet leaves no station in force",
          "[econet][socket][station-in-force]") {
    EconetSocket socket;
    socket.enable(42, std::make_unique<TestBackend>());
    socket.read_station_id(0);
    socket.disable();
    CHECK_FALSE(socket.station_in_force().has_value());

    // Refitted: the new configuration is in force until read.
    socket.enable(7, std::make_unique<TestBackend>());
    CHECK(socket.station_in_force() == 7);
}

TEST_CASE("EconetSocket: the station in force is readable while another thread reads the links",
          "[econet][socket][station-in-force]") {
    EconetSocket socket;
    socket.enable(10, std::make_unique<TestBackend>());
    std::atomic<bool> done{false};
    std::thread guest([&] {
        for (int i = 0; i < 200000; ++i) {
            if (i % 1000 == 0) socket.reset();
            socket.read_station_id(0);
        }
        done = true;
    });
    int observed_bad = 0;
    while (!done) {
        auto in_force = socket.station_in_force();
        if (!in_force || *in_force != 10) ++observed_bad;
    }
    guest.join();
    CHECK(observed_bad == 0);
}
