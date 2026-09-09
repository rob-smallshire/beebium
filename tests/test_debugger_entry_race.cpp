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

// Data-race regression tests for the debugger's breakpoint/watchpoint entry
// vectors. The emulation loop iterates those vectors every cycle (breakpoints
// at each instruction boundary, watchpoints at each bus access); a debugger
// service thread must not mutate them concurrently. Machine::set_*_entries and
// the coprocessor runner's equivalents must halt the executing thread across
// the swap. Built under ThreadSanitizer (BEEBIUM_ENABLE_TSAN) these fail
// loudly before the fix and are silent after; they also exercise the crash
// (freed vector storage under the iterating thread) in an ordinary build.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/extension/CpuDebugTarget.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include "SecondProcessor65C02Extension.hpp"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace beebium;

TEST_CASE("Host debugger entry vectors survive concurrent set from another thread",
          "[debugger][race][tsan]") {
    ModelB machine;
    machine.reset();

    std::atomic<bool> stop{false};

    // Emulation thread, mirroring the server loop: honour the debugger pause,
    // then run a batch. Every cycle it iterates the breakpoint and watchpoint
    // vectors.
    std::thread emulation([&] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused();
            machine.run(2000);
        }
    });

    // Hammer the entry vectors from this thread with alternating lists, so the
    // vector storage is repeatedly reallocated and freed while the emulation
    // thread iterates it.
    for (int i = 0; i < 3000; ++i) {
        std::vector<BreakpointEntry> bps;
        if (i & 1) {
            bps.push_back({1u, 0x3000u, 0x3001u, false, std::nullopt, 0});
        }
        machine.set_breakpoint_entries(std::move(bps));

        std::vector<WatchpointEntry> wps;
        if (i & 1) {
            wps.push_back({1u, 0x2000u, 0x2001u, WATCH_WRITE, false, std::nullopt, 0});
        }
        machine.set_watchpoint_entries(std::move(wps));
    }

    stop.store(true, std::memory_order_release);
    machine.request_shutdown();  // unblock the loop if it is parked in wait_if_paused
    emulation.join();

    SUCCEED();
}

TEST_CASE("Coprocessor debugger entry vectors survive concurrent set from another thread",
          "[debugger][race][tsan][coprocessor]") {
    const std::string rom_path =
        std::string(BEEBIUM_TUBE_ROM_DIR) + "/acorn-tube-6502_1_10.rom";
    if (!std::filesystem::exists(rom_path)) {
        SKIP("Tube 6502 ROM not available");
    }

    // A host machine with a 65C02 coprocessor installed via its extension. The
    // coprocessor executes only on the host emulation thread (Machine::step ->
    // run_coprocessor_until), so the server-supplied quiescer that halts it is
    // the host's own with_emulation_paused; wire it to this machine.
    ModelB machine;
    ExtensionContext ctx(nullptr, nullptr, &machine.state().memory.tube_socket);
    SecondProcessor65C02Extension ext;
    ext.set_config({{"id", "race"}, {"rom", rom_path}});
    ext.init(ctx);
    REQUIRE(ext.running());
    machine.reset();

    CpuDebugTarget* target = ext.debug_target();
    REQUIRE(target != nullptr);
    target->set_execution_quiescer(
        [&machine](const std::function<void()>& fn) { machine.with_emulation_paused(fn); });

    std::atomic<bool> stop{false};
    std::thread emulation([&] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused();
            machine.run(2000);  // drives the coprocessor via run_coprocessor_until
        }
    });

    // Hammer the coprocessor's entry vectors through the abstract interface, the
    // way the coprocessor debugger service does, while the host loop drives it.
    for (int i = 0; i < 3000; ++i) {
        std::vector<BreakpointEntry> bps;
        if (i & 1) {
            bps.push_back({1u, 0xF800u, 0xF801u, false, std::nullopt, 0});
        }
        target->set_breakpoint_entries(std::move(bps));

        std::vector<WatchpointEntry> wps;
        if (i & 1) {
            wps.push_back({1u, 0x1000u, 0x1001u, WATCH_WRITE, false, std::nullopt, 0});
        }
        target->set_watchpoint_entries(std::move(wps));
    }

    stop.store(true, std::memory_order_release);
    machine.request_shutdown();
    emulation.join();

    SUCCEED();
}
