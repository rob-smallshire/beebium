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

// Integration tests for the Acorn 65C02 coprocessor extension.
//
// These tests create the extension, provide it with a TubeSocket via
// ExtensionContext, and verify that the parasite boots and communicates
// with the host through the TubeUla bridge. The parasite is driven in host
// time via TubeSocket::run_coprocessor_until() in the single-threaded model.

#include <catch2/catch_test_macros.hpp>

#include "SecondProcessor65C02Extension.hpp"
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/tube/TubeSocket.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <cstdint>

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

using namespace beebium;

// Drive the coprocessor in host time via the TubeSocket until the parasite has
// reached the target cycle count, or the host-cycle budget is exhausted. Each
// host cycle produces 1 or 2 parasite cycles (the 3:2 ratio). host_cycle is a
// running monotonic host time -- as it is in Machine::step() -- carried across
// calls so the coprocessor's clock never sees time go backwards.
static void run_coprocessor_to(TubeSocket& socket, ParasiteRunner& runner,
                               uint64_t& host_cycle, uint64_t target_cycles,
                               uint64_t max_host_advance = 200000)
{
    const uint64_t limit = host_cycle + max_host_advance;
    while (host_cycle < limit && runner.cycle_count() < target_cycles) {
        socket.run_coprocessor_until(++host_cycle);
    }
}

TEST_CASE("65C02 extension: boots and produces R1 banner", "[tube][extension]") {
    // Set up a TubeSocket (as the host machine would have).
    TubeSocket tube_socket;

    // Create ExtensionContext with the TubeSocket.
    ExtensionContext ctx(nullptr, nullptr, &tube_socket);

    // Create and configure the extension.
    SecondProcessor65C02Extension ext;
    ext.set_config({
        {"id", "test-tube"},
        {"rom", std::string(BEEBIUM_TUBE_ROM_DIR) + "/acorn-tube-6502_1_10.rom"}
    });

    // Initialise -- installs parasite for single-threaded ticking.
    ext.init(ctx);
    REQUIRE(ext.running());
    REQUIRE(tube_socket.enabled());

    // Run the coprocessor until it has completed enough cycles for the boot
    // banner (the parasite writes 24 bytes to the R1 P-to-H FIFO via OSWRCH,
    // which takes ~100K parasite cycles).
    uint64_t host_cycle = 0;
    run_coprocessor_to(tube_socket, *ext.runner(), host_cycle, 100000);

    uint8_t status = tube_socket.peek(0);
    REQUIRE((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read the 24-byte banner through the host's TubeSocket interface.
    static constexpr uint8_t expected_banner[] = {
        0x0A,
        'A', 'c', 'o', 'r', 'n', ' ',
        'T', 'U', 'B', 'E', ' ',
        '6', '5', '0', '2', ' ',
        '6', '4', 'K',
        0x0A, 0x0A, 0x0D, 0x00
    };
    static_assert(sizeof(expected_banner) == 24);

    for (int i = 0; i < 24; ++i) {
        INFO("FIFO position: " << i);
        CHECK(tube_socket.read(1) == expected_banner[i]);
    }

    // Shutdown the extension.
    ext.shutdown();
    CHECK(!ext.running());
    CHECK(!tube_socket.enabled());
}

TEST_CASE("65C02 extension: coprocessor pauses through the Coprocessor interface", "[tube][extension]") {
    TubeSocket tube_socket;
    ExtensionContext ctx(nullptr, nullptr, &tube_socket);

    SecondProcessor65C02Extension ext;
    ext.set_config({
        {"id", "test-tube-xstop"},
        {"rom", std::string(BEEBIUM_TUBE_ROM_DIR) + "/acorn-tube-6502_1_10.rom"}
    });
    ext.init(ctx);
    REQUIRE(ext.running());

    // Boot the parasite.
    uint64_t host_cycle = 0;
    run_coprocessor_to(tube_socket, *ext.runner(), host_cycle, 100000);

    // The server pauses the coprocessor through the abstract Coprocessor
    // interface when a host breakpoint with stop_counterpart fires.
    REQUIRE(!ext.runner()->is_paused());
    ext.coprocessor()->pause();
    CHECK(ext.runner()->is_paused());

    // Running the coprocessor while paused advances host time but runs no
    // parasite cycles.
    auto cycles_before = ext.runner()->cycle_count();
    for (int i = 0; i < 1000; ++i) {
        tube_socket.run_coprocessor_until(++host_cycle);
    }
    CHECK(ext.runner()->cycle_count() == cycles_before);

    // Resume and verify the parasite runs again.
    ext.runner()->resume();
    CHECK(!ext.runner()->is_paused());
    for (int i = 0; i < 1000; ++i) {
        tube_socket.run_coprocessor_until(++host_cycle);
    }
    CHECK(ext.runner()->cycle_count() > cycles_before);

    ext.shutdown();
}

TEST_CASE("65C02 extension: shutdown is idempotent", "[tube][extension]") {
    TubeSocket tube_socket;
    ExtensionContext ctx(nullptr, nullptr, &tube_socket);

    SecondProcessor65C02Extension ext;
    ext.set_config({
        {"id", "test-tube-2"},
        {"rom", std::string(BEEBIUM_TUBE_ROM_DIR) + "/acorn-tube-6502_1_10.rom"}
    });

    ext.init(ctx);
    REQUIRE(ext.running());

    ext.shutdown();
    CHECK(!ext.running());

    // Second shutdown is a no-op.
    ext.shutdown();
    CHECK(!ext.running());
}
