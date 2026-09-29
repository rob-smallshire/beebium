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

// Drives the concrete DebuggerControlServiceImpl against the abstract
// CpuDebugTarget interface (not the concrete CoprocessorRunner), the way the
// server does. A CoprocessorRunner is supplied only as a CpuDebugTarget&, proving
// the service works through the interface: the register model, memory,
// stepping and the machine_type() accessor.

#include <catch2/catch_test_macros.hpp>

#include <beebium/extension/CpuDebugTarget.hpp>
#include <beebium/service/DebuggerService.hpp>
#include <beebium/tube/CoprocessorRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>

using namespace beebium;

namespace {

std::array<uint8_t, 4096> make_nop_rom(uint16_t entry = 0xF800) {
    std::array<uint8_t, 4096> rom{};
    rom.fill(0xEA);  // NOP
    rom[0xFFC] = static_cast<uint8_t>(entry & 0xFF);
    rom[0xFFD] = static_cast<uint8_t>(entry >> 8);
    rom[0xFFE] = 0x00; rom[0xFFF] = 0xF9; rom[0x900] = 0x40;  // IRQ -> RTI
    rom[0xFFA] = 0x80; rom[0xFFB] = 0xF9; rom[0x980] = 0x40;  // NMI -> RTI
    return rom;
}

}  // namespace

TEST_CASE("DebuggerControlServiceImpl drives a CoprocessorRunner through CpuDebugTarget",
          "[coprocessor][debugger][coprocessor]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorRunner runner(tube, rom);
    runner.reset();
    // Finish the reset sequence: registers are writable only at an
    // instruction boundary.
    runner.step_instruction();

    // The server sees only the abstract interface.
    CpuDebugTarget& target = runner;
    CHECK(target.cpu_descriptor().family == "6502");
    service::DebuggerControlServiceImpl impl(target);

    // Helper: find a register's value in a CpuState by name.
    auto reg = [](const CpuState& s, const std::string& name) -> uint64_t {
        for (const auto& rv : s.registers()) {
            if (rv.name() == name) return rv.value();
        }
        FAIL("register not present: " << name);
        return 0;
    };

    // Registers: set through the service, read back.
    {
        CpuState req;
        auto* a = req.add_registers();
        a->set_name("A");
        a->set_value(0x42);
        auto* x = req.add_registers();
        x->set_name("X");
        x->set_value(0x37);
        CpuState resp;
        auto status = impl.SetCpuState(nullptr, &req, &resp);
        REQUIRE(status.ok());
        CHECK(runner.a() == 0x42);
        CHECK(runner.x() == 0x37);
        CHECK(reg(resp, "A") == 0x42);
        CHECK(reg(resp, "X") == 0x37);

        Empty greq;
        CpuState gresp;
        REQUIRE(impl.GetCpuState(nullptr, &greq, &gresp).ok());
        CHECK(reg(gresp, "A") == 0x42);
        CHECK(reg(gresp, "X") == 0x37);
    }

    // An unknown register name is rejected, naming the offender.
    {
        CpuState req;
        auto* bad = req.add_registers();
        bad->set_name("ZZ");
        bad->set_value(1);
        CpuState resp;
        auto status = impl.SetCpuState(nullptr, &req, &resp);
        CHECK_FALSE(status.ok());
        CHECK(status.error_message().find("ZZ") != std::string::npos);
    }

    // Memory: write through the service, read back.
    {
        WriteMemoryRequest wreq;
        wreq.set_address(0x0070);
        wreq.set_data(std::string(1, static_cast<char>(0xAB)));
        WriteMemoryResponse wresp;
        REQUIRE(impl.WriteMemory(nullptr, &wreq, &wresp).ok());

        ReadMemoryRequest rreq;
        rreq.set_address(0x0070);
        rreq.set_length(1);
        ReadMemoryResponse rresp;
        REQUIRE(impl.ReadMemory(nullptr, &rreq, &rresp).ok());
        REQUIRE(rresp.data().size() == 1);
        CHECK(static_cast<uint8_t>(rresp.data()[0]) == 0xAB);
    }

    // Stepping advances the cycle counter. Stepping requires a stopped target.
    {
        Empty stop_req;
        StopResponse stop_resp;
        REQUIRE(impl.Stop(nullptr, &stop_req, &stop_resp).ok());
        REQUIRE(target.is_paused());

        const uint64_t before = runner.cycle_count();
        StepRequest sreq;
        sreq.set_count(1);
        StepResponse sresp;
        REQUIRE(impl.StepInstruction(nullptr, &sreq, &sresp).ok());
        CHECK(sresp.success());
        CHECK(sresp.cycles_executed() > 0);
        CHECK(runner.cycle_count() > before);
    }

    // Memory regions carry the machine type via the machine_type() accessor
    // that replaced the static MACHINE_TYPE member.
    {
        GetMemoryRegionsRequest req;
        GetMemoryRegionsResponse resp;
        REQUIRE(impl.GetMemoryRegions(nullptr, &req, &resp).ok());
        CHECK(resp.machine_type() == "Tube65C02");
        CHECK(resp.regions_size() > 0);
    }

    // Breakpoints plumb through the interface.
    {
        AddBreakpointRequest req;
        req.set_start_address(0xF810);
        AddBreakpointResponse resp;
        REQUIRE(impl.AddBreakpoint(nullptr, &req, &resp).ok());

        Empty lreq;
        ListBreakpointsResponse lresp;
        REQUIRE(impl.ListBreakpoints(nullptr, &lreq, &lresp).ok());
        CHECK(lresp.breakpoints_size() == 1);
    }
}

TEST_CASE("CoprocessorRunner refuses register writes mid-instruction", "[coprocessor][debugger][boundary]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorRunner runner(tube, rom);
    runner.reset();
    CpuDebugTarget& target = runner;
    service::DebuggerControlServiceImpl impl(target);

    // The reset sequence is an instruction in flight.
    CHECK(target.mid_instruction());
    target.step_instruction();
    REQUIRE_FALSE(target.mid_instruction());

    // One cycle into a NOP leaves it part-way through.
    target.step();
    REQUIRE(target.mid_instruction());

    CpuState req;
    auto* pc = req.add_registers();
    pc->set_name("PC");
    pc->set_value(0xF900);
    CpuState refused;
    auto status = impl.SetCpuState(nullptr, &req, &refused);
    CHECK(status.error_code() == grpc::StatusCode::FAILED_PRECONDITION);

    // Completing the in-flight instruction reaches a boundary; the write lands.
    target.step_instruction();
    REQUIRE_FALSE(target.mid_instruction());
    CpuState accepted;
    REQUIRE(impl.SetCpuState(nullptr, &req, &accepted).ok());
    CHECK(runner.pc() == 0xF900);
}

TEST_CASE("CoprocessorRunner stops land on instruction boundaries", "[coprocessor][debugger][boundary]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorRunner runner(tube, rom);
    runner.reset();
    runner.step_instruction();
    CpuDebugTarget& target = runner;
    service::DebuggerControlServiceImpl impl(target);

    SECTION("Stop completes an instruction left part-way through") {
        target.step();  // one cycle into a NOP
        REQUIRE(target.mid_instruction());
        Empty req;
        StopResponse resp;
        REQUIRE(impl.Stop(nullptr, &req, &resp).ok());
        CHECK(target.is_paused());
        CHECK_FALSE(target.mid_instruction());
    }

    SECTION("A watchpoint stop in run_until completes the instruction") {
        // &0400 JSR &0410; &0410 NOP. JSR pushes the return address in two
        // writes part-way through; a stack-page write watchpoint fires on the
        // first, and the second, made while completing the JSR, must not fire.
        WriteMemoryRequest code;
        code.set_address(0x0400);
        code.set_data(std::string("\x20\x10\x04", 3));
        WriteMemoryResponse code_resp;
        REQUIRE(impl.WriteMemory(nullptr, &code, &code_resp).ok());
        WriteMemoryRequest nop;
        nop.set_address(0x0410);
        nop.set_data(std::string("\xEA\xEA", 2));
        WriteMemoryResponse nop_resp;
        REQUIRE(impl.WriteMemory(nullptr, &nop, &nop_resp).ok());
        CpuState pc_req;
        auto* pc = pc_req.add_registers();
        pc->set_name("PC");
        pc->set_value(0x0400);
        CpuState pc_resp;
        REQUIRE(impl.SetCpuState(nullptr, &pc_req, &pc_resp).ok());

        AddWatchpointRequest wreq;
        wreq.set_start_address(0x0100);
        wreq.set_end_address(0x0200);
        wreq.set_type(WATCHPOINT_WRITE);
        AddWatchpointResponse wresp;
        REQUIRE(impl.AddWatchpoint(nullptr, &wreq, &wresp).ok());
        REQUIRE(wresp.success());

        runner.run_until(0);  // establish the time origin at host cycle 0
        runner.run_until(1000);

        REQUIRE(target.is_paused());
        CHECK_FALSE(target.mid_instruction());
        CHECK(runner.pc() == 0x0410);

        Empty lreq;
        ListWatchpointsResponse lresp;
        REQUIRE(impl.ListWatchpoints(nullptr, &lreq, &lresp).ok());
        REQUIRE(lresp.watchpoints_size() == 1);
        CHECK(lresp.watchpoints(0).hit_count() == 1);
    }
}
