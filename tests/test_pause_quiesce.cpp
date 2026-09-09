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

// Correctness and data-race tests for Machine's pause/quiesce primitive, the
// contract every service and extension relies on when it mutates state the
// emulation thread touches. The primitive separates a logical pause (pause()/
// resume(), reported by is_paused()) from quiescing (with_emulation_paused,
// which parks the emulation thread across a body and restores the prior logical
// state afterwards). These tests pin four properties that the older primitive
// got wrong; under ThreadSanitizer the racy ones fail loudly before the fix and
// are silent after.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/service/DebuggerService.hpp>
#include <beebium/service/HostDebugTarget.hpp>
#include "debugger.pb.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace beebium;
using namespace std::chrono_literals;

namespace {

// Spin until `pred` holds, so a test step waits on an observable condition
// rather than a fixed sleep. Bounded so a broken build fails instead of hanging.
template <typename Pred>
bool spin_until(Pred&& pred, std::chrono::milliseconds budget = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

}  // namespace

// (i) A logical pause (Stop) that lands while another caller is quiescing the
// machine must survive the quiesce. The old primitive drove pause state through
// a single flag that with_emulation_paused exchanged and then restored, so a
// pause() in that window was discarded and the machine ran on while the
// debugger believed it had stopped. Deterministic: the quiescing body holds the
// machine open, pause() lands during it, and afterwards the machine must be
// paused and must not advance.
TEST_CASE("A pause during a quiesce survives the quiesce", "[pause][quiesce][tsan]") {
    ModelB machine;
    machine.reset();

    std::atomic<bool> stop{false};
    std::thread emulation([&] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused();
            machine.run(2000);
        }
    });

    std::atomic<bool> body_running{false};
    std::atomic<bool> release_body{false};

    // Quiescer on its own thread: parks the emulation loop and holds it parked
    // until the test lets go, so pause() below is guaranteed to land mid-quiesce.
    std::thread quiescer([&] {
        machine.with_emulation_paused([&] {
            body_running.store(true, std::memory_order_release);
            while (!release_body.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        });
    });

    REQUIRE(spin_until([&] { return body_running.load(std::memory_order_acquire); }));

    // The debugger stops the machine while the quiesce is in progress.
    machine.pause();

    // Let the quiescer finish. Its restore must not clobber the logical pause.
    release_body.store(true, std::memory_order_release);
    quiescer.join();

    CHECK(machine.is_paused());

    // And the emulation loop must stay parked: the cycle count must not advance.
    const uint64_t settled = machine.cycle_count();
    std::this_thread::sleep_for(50ms);
    CHECK(machine.cycle_count() == settled);

    // Tidy up: resume and let the loop exit.
    stop.store(true, std::memory_order_release);
    machine.resume();
    machine.request_shutdown();
    emulation.join();
}

// (ii) Two callers quiescing the machine at once must serialise: only one body
// runs at a time. The old primitive held no lock across the body, so two
// service threads mutating machine state believed they each had it to
// themselves. A recursive quiesce mutex now serialises distinct callers while
// still permitting the same thread to re-enter (a quiescing body that calls a
// Machine mutator which itself quiesces).
TEST_CASE("Concurrent quiescers serialise", "[pause][quiesce][tsan]") {
    ModelB machine;
    machine.reset();

    std::atomic<bool> stop{false};
    std::thread emulation([&] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused();
            machine.run(2000);
        }
    });

    std::atomic<int> concurrent{0};
    std::atomic<int> max_concurrent{0};
    // A plain (non-atomic) accumulator touched inside the body: if two bodies
    // ran at once TSan would flag the unsynchronised access directly.
    uint64_t guarded_work = 0;

    auto hammer = [&] {
        for (int i = 0; i < 400; ++i) {
            machine.with_emulation_paused([&] {
                int now = concurrent.fetch_add(1, std::memory_order_acq_rel) + 1;
                int prev_max = max_concurrent.load(std::memory_order_relaxed);
                while (now > prev_max &&
                       !max_concurrent.compare_exchange_weak(prev_max, now)) {
                }
                for (int k = 0; k < 50; ++k) guarded_work += k;
                concurrent.fetch_sub(1, std::memory_order_acq_rel);
            });
        }
    };

    std::thread a(hammer);
    std::thread b(hammer);
    a.join();
    b.join();

    CHECK(max_concurrent.load() == 1);
    CHECK(guarded_work > 0);  // keep the accumulator observably live

    // The same thread may re-enter (nesting is real: a quiescing body may call a
    // Machine mutator that itself quiesces). This must not deadlock.
    bool nested_ran = false;
    machine.with_emulation_paused([&] {
        machine.with_emulation_paused([&] { nested_ran = true; });
    });
    CHECK(nested_ran);

    stop.store(true, std::memory_order_release);
    machine.request_shutdown();
    emulation.join();
}

// (iii) A quiescing body that mutates a device (a disc controller being
// installed or ejected) must not run concurrently with the emulation loop's
// on_wake housekeeping, which ticks that same device while the machine is
// parked. The old wait_if_paused ran on_wake regardless of whether a quiescer
// held the machine, so the eject and the tick raced on the drive. on_wake must
// now stand down while a quiesce is in progress.
TEST_CASE("A quiesced device mutation excludes on_wake housekeeping", "[pause][quiesce][tsan]") {
    ModelB machine;
    machine.reset();

    // Stands in for a disc drive: a vector the "tick" appends to and the
    // "eject" clears. Unsynchronised concurrent access is a data race (and, with
    // the clear, a use-after-free of the vector storage) -- exactly the disc
    // controller shape.
    std::vector<int> drive;
    std::atomic<bool> stop{false};

    // on_wake ticks the drive on every wait pass, doing enough work to make an
    // overlap with a racing quiescer observable to TSan.
    auto tick_drive = [&] {
        for (int k = 0; k < 2000; ++k) {
            drive.push_back(k);
            if (drive.size() > 4096) drive.clear();
        }
    };

    std::thread emulation([&] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused(tick_drive);
            machine.run(2000);
        }
    });

    // Park the machine so the emulation loop sits in wait_if_paused running
    // tick_drive repeatedly.
    machine.pause();
    REQUIRE(spin_until([&] { return machine.is_paused(); }));

    // Hammer the drive from a quiescing body, the way a disc-controller install
    // or eject would. Under the fix these never overlap tick_drive.
    for (int i = 0; i < 200; ++i) {
        machine.with_emulation_paused([&] {
            drive.clear();
            for (int k = 0; k < 2000; ++k) drive.push_back(-k);
        });
        std::this_thread::sleep_for(1ms);
    }

    stop.store(true, std::memory_order_release);
    machine.resume();
    machine.request_shutdown();
    emulation.join();

    SUCCEED();
}

// (iii-b) The same device-mutation exclusion at the main-loop level: the
// server loop does per-iteration work outside run() (ticking drives, adjusting
// speed) that a quiescer must also exclude. EmulationBusyScope generalises
// run()'s guard to cover it, and rechecks the pause under the lock so a
// quiescer that lands in the gap since wait_if_paused returned makes the scope
// inactive rather than racing. This mirrors run_emulation_loop faithfully.
TEST_CASE("The emulation-busy scope excludes a quiescer from loop-body work",
          "[pause][quiesce][tsan]") {
    ModelB machine;
    machine.reset();

    std::vector<int> drive;
    std::atomic<bool> stop{false};

    auto tick_drive = [&] {
        for (int k = 0; k < 2000; ++k) {
            drive.push_back(k);
            if (drive.size() > 4096) drive.clear();
        }
    };

    // Faithful miniature of run_emulation_loop: park if paused, then do the
    // loop-body work under the busy scope, skipping it if a pause slipped in.
    std::thread emulation([&] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused();
            {
                ModelB::EmulationBusyScope busy(machine);
                if (!busy.active()) continue;
                tick_drive();          // work outside run()
                machine.run(2000);
            }
        }
    });

    // Hammer the drive from a quiescing body while the loop runs. The busy
    // scope must serialise these against tick_drive.
    for (int i = 0; i < 400; ++i) {
        machine.with_emulation_paused([&] {
            drive.clear();
            for (int k = 0; k < 2000; ++k) drive.push_back(-k);
        });
    }

    stop.store(true, std::memory_order_release);
    machine.request_shutdown();
    emulation.join();

    SUCCEED();
}

// (iv) WriteMemory on a running machine must halt the emulation thread across
// the write. The handler used to take only the service mutex and poke guest
// memory while the CPU was executing, racing every bus access. Routed through
// the quiesce primitive it is now serialised against the emulation loop.
TEST_CASE("WriteMemory on a running machine is serialised against the loop",
          "[pause][quiesce][tsan][debugger]") {
    ModelB machine;
    machine.reset();

    HostDebugTarget<ModelB> target(machine);
    service::DebuggerControlServiceImpl impl(target);

    std::atomic<bool> stop{false};
    std::thread emulation([&] {
        while (!stop.load(std::memory_order_acquire)) {
            machine.wait_if_paused();
            machine.run(2000);  // reads and writes guest RAM every cycle
        }
    });

    // Hammer guest RAM through the debugger while the machine runs.
    for (int i = 0; i < 1000; ++i) {
        WriteMemoryRequest req;
        req.set_address(0x2000);
        req.set_data(std::string(16, static_cast<char>(i & 0xFF)));
        WriteMemoryResponse resp;
        REQUIRE(impl.WriteMemory(nullptr, &req, &resp).ok());
        CHECK(resp.success());
    }

    stop.store(true, std::memory_order_release);
    machine.request_shutdown();
    emulation.join();
}
