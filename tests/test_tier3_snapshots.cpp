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

// Tier 3 witness: gRPC inspection getters read live device state that the
// emulation thread mutates every cycle. Read unsynchronised, a snapshot tears;
// taken through with_emulation_paused it is consistent and race-free.
// DeviceInspectionService::GetSystemViaState stands in for the pattern -- it
// reads the system VIA, whose T1/T2 timers free-run every cycle from reset, so
// its state changes continuously whether or not any ROM is present.
//
// The default test drives the real (now paused) handler and is clean under
// ThreadSanitizer. The hidden "[.][race]" test reads the same VIA state
// directly with no pause -- the pre-fix shape -- and races.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/service/DeviceInspectionService.hpp>
#include "debugger.pb.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace beebium;

namespace {

// Faithful miniature of the server loop: park while paused, else do the
// per-iteration work under the busy scope.
template <typename F>
std::thread run_emulation(ModelB& machine, std::atomic<bool>& stop, F&& body) {
    return std::thread([&machine, &stop, body = std::forward<F>(body)] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused();
            ModelB::EmulationBusyScope busy(machine);
            if (!busy.active()) continue;
            body();
        }
    });
}

}  // namespace

TEST_CASE("DeviceInspection GetSystemViaState is race-free against the loop",
          "[tier3][quiesce][tsan]") {
    ModelB machine;
    machine.reset();
    service::DeviceInspectionServiceImpl<ModelB> inspection(machine);

    std::atomic<bool> stop{false};
    auto emulation = run_emulation(machine, stop, [&] { machine.run(2000); });

    for (int i = 0; i < 3000; ++i) {
        GetSystemViaStateRequest req;
        ViaState resp;
        inspection.GetSystemViaState(nullptr, &req, &resp);
    }

    stop.store(true, std::memory_order_release);
    machine.request_shutdown();
    emulation.join();
    SUCCEED();
}

// Measures the emulation-thread stall an inspection getter imposes: the quiesce
// plus a register snapshot, with the loop running. Hidden; run with "[bench]".
TEST_CASE("Inspection getter quiesce cost", "[.][bench][tier3]") {
    ModelB machine;
    machine.reset();
    service::DeviceInspectionServiceImpl<ModelB> inspection(machine);

    std::atomic<bool> stop{false};
    auto emulation = run_emulation(machine, stop, [&] { machine.run(2000); });

    constexpr int kReps = 5000;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kReps; ++i) {
        GetSystemViaStateRequest req;
        ViaState resp;
        inspection.GetSystemViaState(nullptr, &req, &resp);
    }
    auto elapsed = std::chrono::steady_clock::now() - start;

    stop.store(true, std::memory_order_release);
    machine.request_shutdown();
    emulation.join();

    double us = std::chrono::duration<double, std::micro>(elapsed).count() / kReps;
    WARN("DeviceInspection getter quiesce+snapshot: " << us << " microseconds per call"
         << " (release build, ModelB, emulation loop running)");
    SUCCEED();
}

TEST_CASE("Reading VIA state unpaused races the loop (before-witness)",
          "[.][race][tier3][tsan]") {
    ModelB machine;
    machine.reset();

    std::atomic<bool> stop{false};
    auto emulation = run_emulation(machine, stop, [&] { machine.run(2000); });

    // The pre-fix shape: read the live VIA counters directly on this (gRPC-
    // like) thread while the emulation loop ticks them. No quiesce.
    volatile uint32_t sink = 0;
    for (int i = 0; i < 2000000; ++i) {
        auto& via = machine.memory().system_via;
        sink = sink + via.effective_t1() + via.effective_t2() + via.state().ifr.value;
    }
    (void)sink;

    stop.store(true, std::memory_order_release);
    machine.request_shutdown();
    emulation.join();
    SUCCEED();
}
