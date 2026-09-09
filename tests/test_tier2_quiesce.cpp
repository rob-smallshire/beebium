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

// Tier 2 witnesses for the two new mechanisms that let a gRPC-thread caller
// mutate emulation-owned state safely:
//
//  1. The extension-framework bus quiescer. A dispatcher runs on a gRPC worker
//     thread; the server injects a quiescer (set_bus_quiescer) that halts the
//     emulation thread, and the dispatcher mutates device state through
//     with_bus_stopped(). This is the plugin-side analogue of the coprocessor
//     debug target's execution quiescer.
//
//  2. The Econet enable/disable lifetime model. Fitting/removing the hardware
//     builds and tears down the ADLC/handshake/backend the emulation thread
//     ticks every cycle; a gRPC reader (a SubscribeEconetEvents stream, a
//     SystemService advertisement read) may be using the recorder or backend at
//     the same time. Enable/disable run with the emulation thread parked, and
//     the recorder/backend are handed out as co-owning handles so a reader
//     never dereferences freed storage.
//
// The default tests exercise the fixed paths and are clean under
// ThreadSanitizer. Each is paired with a hidden "[.][race]" test that removes
// the fix (no quiescer / a retained raw pointer) so the race can be witnessed
// under TSan before the fix; those are excluded from a normal ctest run.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/econet/TestBackend.hpp>
#include <beebium/extension/ExtensionRpc.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace beebium;

namespace {

// A minimal dispatcher that exposes the protected with_bus_stopped() helper so
// a test can drive a device mutation through the framework quiescer.
class TestDispatcher : public ExtensionRpcDispatcher {
public:
    std::string_view service_name() const override { return "TestQuiesce"; }
    RpcStatus invoke(std::string_view, std::string_view, std::string&,
                     RpcContext&) override {
        return RpcStatus::ok();
    }
    // Run a device mutation the way a real handler would.
    void mutate(const std::function<void()>& fn) { with_bus_stopped(fn); }
};

}  // namespace

// (1) A dispatcher mutation routed through the bus quiescer is serialised
// against the emulation loop's per-iteration device work. The loop appends to a
// shared "device" inside its busy scope; the dispatcher rewrites it under
// with_bus_stopped(). With the server-supplied quiescer wired in they never
// overlap.
TEST_CASE("Bus quiescer serialises a dispatcher mutation against the loop",
          "[tier2][quiesce][tsan]") {
    ModelB machine;
    machine.reset();

    std::vector<int> device;  // stands in for device state the loop touches
    TestDispatcher dispatcher;
    // The server wires this; an author never does.
    dispatcher.set_bus_quiescer(
        [&machine](const std::function<void()>& fn) { machine.with_emulation_paused(fn); });

    std::atomic<bool> stop{false};
    std::thread emulation([&] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused();
            ModelB::EmulationBusyScope busy(machine);
            if (!busy.active()) continue;
            for (int k = 0; k < 500; ++k) {
                device.push_back(k);
                if (device.size() > 4096) device.clear();
            }
            machine.run(500);
        }
    });

    for (int i = 0; i < 400; ++i) {
        dispatcher.mutate([&] {
            device.clear();
            for (int k = 0; k < 500; ++k) device.push_back(-k);
        });
    }

    stop.store(true, std::memory_order_release);
    machine.request_shutdown();
    emulation.join();
    SUCCEED();
}

// The before-witness: with no quiescer wired, with_bus_stopped() runs the
// mutation directly on the gRPC thread, racing the emulation loop's device
// access -- exactly the pre-fix behaviour of an unguarded dispatcher. Hidden
// from a normal run; execute under TSan to see the race.
TEST_CASE("Bus quiescer unset races the loop (before-witness)",
          "[.][race][tier2][tsan]") {
    ModelB machine;
    machine.reset();

    std::vector<int> device;
    TestDispatcher dispatcher;  // deliberately no set_bus_quiescer

    std::atomic<bool> stop{false};
    std::thread emulation([&] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused();
            ModelB::EmulationBusyScope busy(machine);
            if (!busy.active()) continue;
            for (int k = 0; k < 500; ++k) {
                device.push_back(k);
                if (device.size() > 4096) device.clear();
            }
            machine.run(500);
        }
    });

    for (int i = 0; i < 400; ++i) {
        dispatcher.mutate([&] {
            device.clear();
            for (int k = 0; k < 500; ++k) device.push_back(-k);
        });
    }

    stop.store(true, std::memory_order_release);
    machine.request_shutdown();
    emulation.join();
    SUCCEED();
}

// (2) Fitting and removing Econet hardware while the emulation loop ticks it,
// with a subscriber concurrently reading the event recorder and a reader taking
// the backend. enable()/disable() run with the loop parked; observable() and
// backend_shared() hand out co-owning handles, so nothing races the teardown or
// dereferences freed storage.
TEST_CASE("Econet enable/disable is race-free against loop and readers",
          "[tier2][quiesce][econet][tsan]") {
    ModelB machine;
    machine.reset();
    auto& econet = machine.state().memory.econet_socket;

    std::atomic<bool> stop{false};
    std::thread emulation([&] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused();
            ModelB::EmulationBusyScope busy(machine);
            if (!busy.active()) continue;
            machine.run(500);  // ticks the ADLC when Econet is fitted
        }
    });

    // A subscriber holding the recorder handle across its reads, as
    // SubscribeEconetEvents does for the life of a stream.
    std::thread subscriber([&] {
        while (!stop.load(std::memory_order_acquire)) {
            std::shared_ptr<ObservableBackend> obs = econet.observable();
            if (obs) {
                uint64_t seq = obs->next_sequence();
                auto events = obs->collect_since(seq > 8 ? seq - 8 : 0);
                (void)events;
            }
            std::this_thread::yield();
        }
    });

    for (int i = 0; i < 200; ++i) {
        machine.with_emulation_paused([&] {
            auto backend = std::make_unique<TestBackend>();
            econet.enable(254, std::move(backend), /*aun_mode=*/true);
        });
        // A backend read racing the teardown, as SystemService does.
        auto held = econet.backend_shared();
        (void)(held && held->is_connected());
        machine.with_emulation_paused([&] { econet.disable(); });
    }

    stop.store(true, std::memory_order_release);
    subscriber.join();
    machine.request_shutdown();
    emulation.join();
    SUCCEED();
}

// Pins the ownership-model invariant on the Econet hot path: the emulation
// thread's per-cycle work (Machine::step -> nmi_pending, tick_rising/falling)
// must never lock the socket's lifetime mutex -- that mutex guards only the
// shared_ptr members' copy/assign for gRPC readers, and a lock on the per-cycle
// path is exactly the TypeAheadQueue defect (a6bb9a01). Mirrors
// tests/test_type_ahead_queue.cpp "idle tick takes no lock": hold the mutex for
// the whole run; if step() ever took it, the worker blocks and the future never
// becomes ready.
TEST_CASE("Econet hot path takes no lifetime lock", "[tier2][econet][hotpath]") {
    ModelB machine;
    machine.reset();
    auto& econet = machine.state().memory.econet_socket;

    // Fit the hardware so the per-cycle econet tick and NMI path are live.
    auto backend = std::make_unique<TestBackend>();
    econet.enable(254, std::move(backend), /*aun_mode=*/true);

    // Hold the lifetime mutex for the whole run.
    std::unique_lock<std::mutex> held(econet.lifetime_mutex_for_test());

    auto worker = std::async(std::launch::async, [&] {
        for (int i = 0; i < 200000; ++i) machine.step();
    });
    const auto status = worker.wait_for(std::chrono::seconds(10));
    held.unlock();
    REQUIRE(status == std::future_status::ready);
    worker.get();
}

// Measures the emulation-thread stall the sideways header scanner imposes: the
// 16 KiB bank copy done under with_emulation_paused, once a second per RAM
// slot, while the emulation loop runs. Hidden from a normal run; execute with
// the "[bench]" filter to print the figure. Justifies choosing a quiesced
// snapshot over a change-driven publish (see SidewaysService::scan_slot).
TEST_CASE("Sideways scanner bank copy cost under quiesce", "[.][bench][tier2]") {
    ModelBRomRamBoard machine;
    machine.reset();
    auto& memory = machine.state().memory;
    memory.configure_slot_as_ram(0);

    std::atomic<bool> stop{false};
    std::thread emulation([&] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused();
            ModelBRomRamBoard::EmulationBusyScope busy(machine);
            if (!busy.active()) continue;
            machine.run(2000);
        }
    });

    // Warm up, then time many 16 KiB copies exactly as scan_slot does.
    constexpr int kReps = 200;
    std::vector<uint8_t> bytes(16384);
    volatile uint8_t sink = 0;
    auto start = std::chrono::steady_clock::now();
    for (int r = 0; r < kReps; ++r) {
        machine.with_emulation_paused([&] {
            for (uint16_t i = 0; i < 16384; ++i) {
                bytes[i] = memory.sideways.peek_bank(0, i);
            }
        });
        sink = static_cast<uint8_t>(sink + bytes[r % 16384]);
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    (void)sink;

    stop.store(true, std::memory_order_release);
    machine.request_shutdown();
    emulation.join();

    double us = std::chrono::duration<double, std::micro>(elapsed).count() / kReps;
    WARN("Sideways scanner per-slot quiesce+copy: " << us << " microseconds"
         << " (once per second per RAM slot, only while a client monitors headers)");
    SUCCEED();
}
