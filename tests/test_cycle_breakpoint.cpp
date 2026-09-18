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

// Machine-level tests for cycle-budget breakpoints (issue #79).
//
// A "cycle budget" is a whole-address-space breakpoint [0x0000, 0x10000) with a
// `cycles >= N` condition. On a running CPU it is evaluated at instruction
// boundaries like any breakpoint (stopping at the first boundary at or after N,
// never mid-instruction). While the CPU is halted -- Break held, or jammed on a
// KIL opcode -- there are no instruction boundaries, so such a breakpoint is
// evaluated every cycle instead and stops at exactly cycle N. Unconditional or
// partial-range breakpoints are NOT evaluated while halted (the PC is static and
// they would fire spuriously).

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/debugger/Expression.hpp"

#include <string>
#include <variant>

using namespace beebium;

namespace {

// A whole-address-space breakpoint carrying a `cycles >= target` condition.
BreakpointEntry cycle_budget_bp(uint64_t target, uint32_t id = 1) {
    auto compiled = compile("cycles >= " + std::to_string(target));
    REQUIRE(std::holds_alternative<CompiledExpression>(compiled));
    return BreakpointEntry{id, 0x0000, 0x10000u, false,
                           std::get<CompiledExpression>(std::move(compiled)), 0};
}

// Wire a stop callback that mirrors the debugger service: evaluate the
// condition (if any) against live CPU state and pause when it is satisfied.
// `fires` counts stops; `halted_evals` counts evaluations made while the CPU was
// NOT at an instruction boundary (the halted per-cycle path).
struct Counters {
    int fires = 0;
    int running_evals = 0;
    int halted_evals = 0;
};

void install_stop_callback(ModelB& m, Counters& c) {
    m.set_breakpoint_hit_callback(
        [&m, &c](const BreakpointEntry& bp, uint16_t /*pc*/) {
            auto& mutable_bp = const_cast<BreakpointEntry&>(bp);
            ++mutable_bp.hit_count;
            if (M6502_IsAboutToExecute(&m.cpu())) {
                ++c.running_evals;
            } else {
                ++c.halted_evals;
            }
            bool should_stop = true;
            if (bp.condition) {
                ExprCpuState cpu_state{
                    m.a(), m.x(), m.y(), m.sp(), m.p(),
                    m.cpu().opcode_pc.w, m.cycle_count(), bp.hit_count};
                should_stop = evaluate(*bp.condition, cpu_state, nullptr, nullptr) != 0;
            }
            if (should_stop) {
                ++c.fires;
                m.pause();
            }
        });
}

// Plant bytes at addr and point the CPU at them, ready to run. reset() arms the
// reset sequence, so run it out with step_instruction() before set_pc takes
// effect (as the debugger fixture's prepare_for_code does).
void plant_and_point(ModelB& m, uint16_t addr, std::initializer_list<uint8_t> bytes) {
    m.reset();
    m.step_instruction();
    uint16_t a = addr;
    for (uint8_t b : bytes) m.write(a++, b);
    m.set_pc(addr);
}

} // namespace

TEST_CASE("Cycle breakpoint stops a jammed CPU at exactly cycle N (#79)",
          "[cycle][breakpoint]") {
    ModelB machine;
    plant_and_point(machine, 0x2000, {0x02});  // KIL: jams immediately
    machine.resume();
    machine.run(20);  // execute the KIL and settle into the halted state
    REQUIRE(M6502_IsHalted(&machine.cpu()));

    Counters c;
    const uint64_t target = machine.cycle_count() + 1000;
    machine.set_breakpoint_entries({cycle_budget_bp(target)});
    install_stop_callback(machine, c);

    machine.resume();
    machine.run(100000);

    CHECK(machine.is_paused());
    CHECK(machine.cycle_count() == target);  // exact: no instruction in flight
    CHECK(c.fires == 1);
}

TEST_CASE("Cycle breakpoint stops a Break-held CPU at exactly cycle N (#79)",
          "[cycle][breakpoint]") {
    ModelB machine;
    machine.reset();
    machine.break_down();  // hold the reset line: halted
    REQUIRE(M6502_IsHalted(&machine.cpu()));

    Counters c;
    const uint64_t target = machine.cycle_count() + 1000;
    machine.set_breakpoint_entries({cycle_budget_bp(target)});
    install_stop_callback(machine, c);

    machine.resume();
    machine.run(100000);

    CHECK(machine.is_paused());
    CHECK(machine.cycle_count() == target);
    CHECK(c.fires == 1);
}

TEST_CASE("Cycle breakpoint on a running CPU stops at the first boundary at or after N (#79)",
          "[cycle][breakpoint]") {
    ModelB machine;
    // A tight loop of NOPs then JMP back, so the CPU keeps fetching opcodes.
    plant_and_point(machine, 0x2000,
                    {0xEA, 0xEA, 0xEA, 0xEA, 0x4C, 0x00, 0x20});  // NOP*4; JMP $2000
    for (int i = 0; i < 4; ++i) machine.step_instruction();  // settle in the loop
    REQUIRE(M6502_IsAboutToExecute(&machine.cpu()));

    Counters c;
    const uint64_t target = machine.cycle_count() + 1000;
    machine.set_breakpoint_entries({cycle_budget_bp(target)});
    install_stop_callback(machine, c);

    machine.resume();
    machine.run(100000);

    CHECK(machine.is_paused());
    CHECK(M6502_IsAboutToExecute(&machine.cpu()));  // stopped AT a boundary
    CHECK(machine.cycle_count() >= target);          // at or after N
    CHECK(machine.cycle_count() < target + 8);        // within one instruction
    CHECK(c.fires == 1);
}

TEST_CASE("An unconditional full-range breakpoint does not fire while halted (#79)",
          "[cycle][breakpoint]") {
    auto run_case = [](ModelB& machine) {
        Counters c;
        // Full-range, but NO condition: would stop at every instruction while
        // running; must NOT fire while halted (the PC is static).
        machine.set_breakpoint_entries({BreakpointEntry{1, 0x0000, 0x10000u, false, {}, 0}});
        install_stop_callback(machine, c);
        machine.resume();
        machine.run(100000);
        CHECK(!machine.is_paused());
        CHECK(c.fires == 0);
        CHECK(c.halted_evals == 0);
    };

    SECTION("jammed") {
        ModelB machine;
        plant_and_point(machine, 0x2000, {0x02});
        machine.resume();
        machine.run(20);
        REQUIRE(M6502_IsHalted(&machine.cpu()));
        run_case(machine);
    }
    SECTION("break held") {
        ModelB machine;
        machine.reset();
        machine.break_down();
        REQUIRE(M6502_IsHalted(&machine.cpu()));
        run_case(machine);
    }
}

TEST_CASE("A conditional partial-range breakpoint does not fire while halted (#79)",
          "[cycle][breakpoint]") {
    ModelB machine;
    machine.reset();
    machine.break_down();
    REQUIRE(M6502_IsHalted(&machine.cpu()));

    Counters c;
    // A single-address breakpoint with a cycle condition: not whole-address-space,
    // so it must not be evaluated while halted even though the cycle target passes.
    auto compiled = compile("cycles >= " + std::to_string(machine.cycle_count() + 1000));
    REQUIRE(std::holds_alternative<CompiledExpression>(compiled));
    machine.set_breakpoint_entries({BreakpointEntry{
        1, 0x2000, 0x2001u, false,
        std::get<CompiledExpression>(std::move(compiled)), 0}});
    install_stop_callback(machine, c);

    machine.resume();
    machine.run(100000);

    CHECK(!machine.is_paused());
    CHECK(c.fires == 0);
    CHECK(c.halted_evals == 0);
}

TEST_CASE("A running CPU pays no per-cycle halted evaluation (#79 cost)",
          "[cycle][breakpoint]") {
    ModelB machine;
    plant_and_point(machine, 0x2000,
                    {0xEA, 0xEA, 0xEA, 0xEA, 0x4C, 0x00, 0x20});  // NOP loop
    for (int i = 0; i < 4; ++i) machine.step_instruction();  // settle in the loop
    REQUIRE(M6502_IsAboutToExecute(&machine.cpu()));

    Counters c;
    // A full-range conditional breakpoint whose target is far away, so it is
    // evaluated but never stops during the run.
    machine.set_breakpoint_entries({cycle_budget_bp(machine.cycle_count() + 10'000'000)});
    install_stop_callback(machine, c);

    machine.resume();
    machine.run(100000);

    // The condition was evaluated only at instruction boundaries; the halted
    // per-cycle path was never entered while the CPU was running.
    CHECK(c.halted_evals == 0);
    CHECK(c.running_evals > 0);
    CHECK(!machine.is_paused());  // target far away: never stopped
}
