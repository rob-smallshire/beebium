// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// Test gRPC DebuggerControl and Debugger6502 services
//
// These tests verify the debugger service implementations by acting as gRPC clients.
// They create a local server, connect to it, and verify debugger functionality.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "debugger.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <fstream>
#include <vector>

namespace {

// Helper to load ROM file
std::vector<uint8_t> load_rom(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open ROM: " + filepath);
    }
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// Test fixture that sets up a machine and server with debugger services
class DebuggerTestFixture {
public:
    DebuggerTestFixture() {
        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/OS12.ROM");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/BASIC2.ROM");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        std::copy(basic.begin(), basic.end(), machine_.state().memory.basic_rom.data());
#endif
        machine_.reset();

        // Start server on a unique port for debugger tests
        server_ = std::make_unique<beebium::service::Server>(machine_, "127.0.0.1", 50054);
        server_->start();

        // Create client channel
        channel_ = grpc::CreateChannel("127.0.0.1:50054",
                                       grpc::InsecureChannelCredentials());
        debugger_stub_ = beebium::DebuggerControl::NewStub(channel_);
        cpu_stub_ = beebium::Debugger6502::NewStub(channel_);
    }

    ~DebuggerTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::DebuggerControl::Stub& debugger() { return *debugger_stub_; }
    beebium::Debugger6502::Stub& cpu() { return *cpu_stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::DebuggerControl::Stub> debugger_stub_;
    std::unique_ptr<beebium::Debugger6502::Stub> cpu_stub_;
};

} // anonymous namespace

//////////////////////////////////////////////////////////////////////////////
// Execution Control Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("DebuggerControl GetState returns execution state", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext context;
    beebium::Empty request;
    beebium::ExecutionState response;

    auto status = fixture.debugger().GetState(&context, request, &response);

    REQUIRE(status.ok());
    // Machine starts not paused (is_running = true)
    CHECK(response.is_running());
    CHECK(response.cycle_count() == 0);
    CHECK(response.halt_reason().empty());
    CHECK(response.sequence() > 0);  // Sequence is non-zero after reset
}

TEST_CASE("DebuggerControl Stop pauses execution", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext context;
    beebium::Empty request;
    beebium::StopResponse response;

    auto status = fixture.debugger().Stop(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());
    CHECK(response.state().is_running() == false);
    CHECK(response.state().halt_reason() == "stopped by debugger");

    // Verify machine is paused
    CHECK(fixture.machine().is_paused());
}

TEST_CASE("DebuggerControl Run resumes after Stop", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // First stop the machine
    {
        grpc::ClientContext context;
        beebium::Empty request;
        beebium::StopResponse response;
        fixture.debugger().Stop(&context, request, &response);
    }
    REQUIRE(fixture.machine().is_paused());

    // Now resume
    {
        grpc::ClientContext context;
        beebium::Empty request;
        beebium::RunResponse response;
        auto status = fixture.debugger().Run(&context, request, &response);

        REQUIRE(status.ok());
        CHECK(response.success());
    }

    CHECK_FALSE(fixture.machine().is_paused());
}

TEST_CASE("DebuggerControl Run when already running returns error", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Machine is already running (not paused)
    grpc::ClientContext context;
    beebium::Empty request;
    beebium::RunResponse response;

    auto status = fixture.debugger().Run(&context, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.success());
    CHECK(response.error() == "already running");
}

TEST_CASE("DebuggerControl Reset resets the machine", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Run some cycles first
    fixture.machine().run(100);
    auto cycles_before = fixture.machine().cycle_count();
    REQUIRE(cycles_before > 0);

    grpc::ClientContext context;
    beebium::Empty request;
    beebium::ResetResponse response;

    auto status = fixture.debugger().Reset(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());
    CHECK(fixture.machine().cycle_count() == 0);
}

TEST_CASE("DebuggerControl StepInstruction steps one instruction", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // First stop the machine
    {
        grpc::ClientContext context;
        beebium::Empty request;
        beebium::StopResponse response;
        fixture.debugger().Stop(&context, request, &response);
    }

    grpc::ClientContext context;
    beebium::StepRequest request;
    request.set_count(1);
    beebium::StepResponse response;

    auto status = fixture.debugger().StepInstruction(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());
    CHECK(response.instructions_executed() == 1);
    CHECK(response.cycles_executed() > 0);
}

TEST_CASE("DebuggerControl StepInstruction fails when running", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Machine is not paused
    grpc::ClientContext context;
    beebium::StepRequest request;
    request.set_count(1);
    beebium::StepResponse response;

    auto status = fixture.debugger().StepInstruction(&context, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.success());
    CHECK(response.error() == "machine is running");
}

TEST_CASE("DebuggerControl StepCycle steps one cycle", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // First stop the machine
    {
        grpc::ClientContext context;
        beebium::Empty request;
        beebium::StopResponse response;
        fixture.debugger().Stop(&context, request, &response);
    }

    grpc::ClientContext context;
    beebium::StepRequest request;
    request.set_count(10);
    beebium::StepResponse response;

    auto status = fixture.debugger().StepCycle(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());
    CHECK(response.cycles_executed() == 10);
}

//////////////////////////////////////////////////////////////////////////////
// Memory Access Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("DebuggerControl ReadMemory reads from machine memory", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Write some known data
    fixture.machine().write(0x1000, 0x42);
    fixture.machine().write(0x1001, 0xAB);
    fixture.machine().write(0x1002, 0xCD);

    grpc::ClientContext context;
    beebium::ReadMemoryRequest request;
    request.set_address(0x1000);
    request.set_length(3);
    beebium::ReadMemoryResponse response;

    auto status = fixture.debugger().ReadMemory(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 3);
    CHECK(static_cast<uint8_t>(response.data()[0]) == 0x42);
    CHECK(static_cast<uint8_t>(response.data()[1]) == 0xAB);
    CHECK(static_cast<uint8_t>(response.data()[2]) == 0xCD);
}

TEST_CASE("DebuggerControl WriteMemory writes to machine memory", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext context;
    beebium::WriteMemoryRequest request;
    request.set_address(0x2000);
    request.set_data(std::string("\x11\x22\x33", 3));
    beebium::WriteMemoryResponse response;

    auto status = fixture.debugger().WriteMemory(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());

    // Verify data was written
    CHECK(fixture.machine().peek(0x2000) == 0x11);
    CHECK(fixture.machine().peek(0x2001) == 0x22);
    CHECK(fixture.machine().peek(0x2002) == 0x33);
}

TEST_CASE("DebuggerControl PeekMemory is side-effect-free", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Set up VIA timer with very short count to fire immediately
    auto& via = fixture.machine().state().memory.system_via;
    via.write(0x04, 0x01);  // T1L-L = 1 (very short timer)
    via.write(0x06, 0x00);  // T1L-H = 0
    via.write(0x05, 0x00);  // T1C-H = 0 (starts timer with counter = 1)

    // Tick until timer fires - need both rising and falling edges
    for (int i = 0; i < 10; i++) {
        via.tick_rising();
        via.tick_falling();
    }

    // Enable T1 interrupt
    via.write(0x0E, 0xC0);  // IER: set bit 6 (T1) with bit 7 high

    // Timer should have fired, IFR bit 6 set
    uint8_t ifr_before = via.peek(0x0D);  // Use peek to check without clearing
    REQUIRE((ifr_before & 0x40) != 0);  // T1 interrupt flag set

    // Read IFR using gRPC peek - should NOT clear the flag
    grpc::ClientContext context;
    beebium::PeekMemoryRequest request;
    request.set_address(0xFE4D);  // System VIA IFR
    request.set_length(1);
    beebium::PeekMemoryResponse response;

    auto status = fixture.debugger().PeekMemory(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 1);
    CHECK((static_cast<uint8_t>(response.data()[0]) & 0x40) != 0);

    // IFR should still have the flag set (peek doesn't clear it)
    uint8_t ifr_after = via.peek(0x0D);
    CHECK((ifr_after & 0x40) != 0);
}

//////////////////////////////////////////////////////////////////////////////
// Breakpoint Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("DebuggerControl AddBreakpoint creates breakpoint", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext context;
    beebium::AddBreakpointRequest request;
    request.set_address(0xD9CD);
    beebium::AddBreakpointResponse response;

    auto status = fixture.debugger().AddBreakpoint(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());
    CHECK(response.id() == 1);  // First breakpoint gets ID 1
}

TEST_CASE("DebuggerControl ListBreakpoints returns added breakpoints", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Add two breakpoints
    {
        grpc::ClientContext context;
        beebium::AddBreakpointRequest request;
        request.set_address(0x1000);
        beebium::AddBreakpointResponse response;
        fixture.debugger().AddBreakpoint(&context, request, &response);
    }
    {
        grpc::ClientContext context;
        beebium::AddBreakpointRequest request;
        request.set_address(0x2000);
        beebium::AddBreakpointResponse response;
        fixture.debugger().AddBreakpoint(&context, request, &response);
    }

    grpc::ClientContext context;
    beebium::Empty request;
    beebium::ListBreakpointsResponse response;

    auto status = fixture.debugger().ListBreakpoints(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.breakpoints_size() == 2);

    // Check breakpoint data
    CHECK(response.breakpoints(0).address() == 0x1000);
    CHECK(response.breakpoints(1).address() == 0x2000);
}

TEST_CASE("DebuggerControl RemoveBreakpoint removes a breakpoint", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Add a breakpoint
    uint32_t bp_id;
    {
        grpc::ClientContext context;
        beebium::AddBreakpointRequest request;
        request.set_address(0x3000);
        beebium::AddBreakpointResponse response;
        fixture.debugger().AddBreakpoint(&context, request, &response);
        bp_id = response.id();
    }

    // Remove it
    {
        grpc::ClientContext context;
        beebium::RemoveBreakpointRequest request;
        request.set_id(bp_id);
        beebium::RemoveBreakpointResponse response;

        auto status = fixture.debugger().RemoveBreakpoint(&context, request, &response);

        REQUIRE(status.ok());
        CHECK(response.success());
    }

    // Verify it's gone
    {
        grpc::ClientContext context;
        beebium::Empty request;
        beebium::ListBreakpointsResponse response;
        fixture.debugger().ListBreakpoints(&context, request, &response);
        CHECK(response.breakpoints_size() == 0);
    }
}

TEST_CASE("DebuggerControl ClearBreakpoints removes all breakpoints", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Add several breakpoints
    for (int i = 0; i < 5; i++) {
        grpc::ClientContext context;
        beebium::AddBreakpointRequest request;
        request.set_address(0x1000 + i * 0x100);
        beebium::AddBreakpointResponse response;
        fixture.debugger().AddBreakpoint(&context, request, &response);
    }

    grpc::ClientContext context;
    beebium::Empty request;
    beebium::ClearBreakpointsResponse response;

    auto status = fixture.debugger().ClearBreakpoints(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.count_removed() == 5);

    // Verify all are gone
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListBreakpointsResponse resp;
        fixture.debugger().ListBreakpoints(&ctx, req, &resp);
        CHECK(resp.breakpoints_size() == 0);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Debugger6502 Register Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Debugger6502 ReadRegisters returns CPU registers", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Set some known values
    fixture.machine().set_a(0x42);
    fixture.machine().set_x(0x11);
    fixture.machine().set_y(0x22);
    fixture.machine().set_sp(0xFF);
    fixture.machine().set_p(0x24);  // N=0, V=0, B=1, D=0, I=1, Z=0, C=0

    grpc::ClientContext context;
    beebium::Empty request;
    beebium::Registers6502 response;

    auto status = fixture.cpu().ReadRegisters(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.a() == 0x42);
    CHECK(response.x() == 0x11);
    CHECK(response.y() == 0x22);
    CHECK(response.sp() == 0xFF);
    CHECK(response.p() == 0x24);
}

TEST_CASE("Debugger6502 WriteRegisters sets individual registers", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext context;
    beebium::WriteRegisters6502Request request;
    request.set_a(0xAA);
    request.set_x(0xBB);
    beebium::WriteRegistersResponse response;

    auto status = fixture.cpu().WriteRegisters(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());

    // Check the values were set
    CHECK(fixture.machine().a() == 0xAA);
    CHECK(fixture.machine().x() == 0xBB);
}

TEST_CASE("Debugger6502 WriteRegisters can set PC", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext context;
    beebium::WriteRegisters6502Request request;
    request.set_pc(0xC000);
    beebium::WriteRegistersResponse response;

    auto status = fixture.cpu().WriteRegisters(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());
    CHECK(fixture.machine().pc() == 0xC000);
}

//////////////////////////////////////////////////////////////////////////////
// Sequence Counter Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Sequence counter increments on step", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Stop the machine first
    {
        grpc::ClientContext context;
        beebium::Empty request;
        beebium::StopResponse response;
        fixture.debugger().Stop(&context, request, &response);
    }

    // Get initial sequence
    uint64_t seq1;
    {
        grpc::ClientContext context;
        beebium::Empty request;
        beebium::ExecutionState response;
        fixture.debugger().GetState(&context, request, &response);
        seq1 = response.sequence();
    }

    // Step
    {
        grpc::ClientContext context;
        beebium::StepRequest request;
        request.set_count(1);
        beebium::StepResponse response;
        fixture.debugger().StepInstruction(&context, request, &response);
    }

    // Get new sequence
    uint64_t seq2;
    {
        grpc::ClientContext context;
        beebium::Empty request;
        beebium::ExecutionState response;
        fixture.debugger().GetState(&context, request, &response);
        seq2 = response.sequence();
    }

    CHECK(seq2 > seq1);
}

TEST_CASE("Sequence counter increments on register write", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Get initial sequence
    uint64_t seq1;
    {
        grpc::ClientContext context;
        beebium::Empty request;
        beebium::ExecutionState response;
        fixture.debugger().GetState(&context, request, &response);
        seq1 = response.sequence();
    }

    // Write a register
    {
        grpc::ClientContext context;
        beebium::WriteRegisters6502Request request;
        request.set_a(0x99);
        beebium::WriteRegistersResponse response;
        fixture.cpu().WriteRegisters(&context, request, &response);
    }

    // Get new sequence
    uint64_t seq2;
    {
        grpc::ClientContext context;
        beebium::Empty request;
        beebium::ExecutionState response;
        fixture.debugger().GetState(&context, request, &response);
        seq2 = response.sequence();
    }

    CHECK(seq2 > seq1);
}
