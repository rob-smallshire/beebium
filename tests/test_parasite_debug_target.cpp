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

// Drives DebuggerControlServiceImpl instantiated against the abstract
// Cpu6502DebugTarget interface (not the concrete ParasiteRunner), the way the
// server does after Step 1b. A ParasiteRunner is supplied only as a
// Cpu6502DebugTarget&, proving the template instantiates and works through the
// interface: registers, memory, stepping and the machine_type() accessor that
// replaced the former static MACHINE_TYPE member.

#include <catch2/catch_test_macros.hpp>

#include <beebium/extension/Cpu6502DebugTarget.hpp>
#include <beebium/service/DebuggerService.hpp>
#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>

using namespace beebium;

namespace {

std::array<uint8_t, 2048> make_nop_rom(uint16_t entry = 0xF800) {
    std::array<uint8_t, 2048> rom{};
    rom.fill(0xEA);  // NOP
    rom[0x7FC] = static_cast<uint8_t>(entry & 0xFF);
    rom[0x7FD] = static_cast<uint8_t>(entry >> 8);
    rom[0x7FE] = 0x00; rom[0x7FF] = 0xF9; rom[0x100] = 0x40;  // IRQ -> RTI
    rom[0x7FA] = 0x80; rom[0x7FB] = 0xF9; rom[0x180] = 0x40;  // NMI -> RTI
    return rom;
}

}  // namespace

TEST_CASE("DebuggerControlServiceImpl drives a ParasiteRunner through Cpu6502DebugTarget",
          "[parasite][debugger][coprocessor]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    ParasiteRunner runner(tube, rom);
    runner.reset();

    // The server sees only the abstract interface.
    Cpu6502DebugTarget& target = runner;
    CHECK(target.cpu_family() == "6502");
    service::DebuggerControlServiceImpl<Cpu6502DebugTarget> impl(target);

    // Registers: set through the service, read back.
    {
        Set6502StateRequest req;
        req.set_a(0x42);
        req.set_x(0x37);
        Cpu6502State resp;
        auto status = impl.Set6502State(nullptr, &req, &resp);
        REQUIRE(status.ok());
        CHECK(runner.a() == 0x42);
        CHECK(runner.x() == 0x37);

        Get6502StateRequest greq;
        Cpu6502State gresp;
        REQUIRE(impl.Get6502State(nullptr, &greq, &gresp).ok());
        CHECK(gresp.a() == 0x42);
        CHECK(gresp.x() == 0x37);
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
