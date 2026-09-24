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

// Test gRPC DebuggerControl service
//
// These tests verify the debugger service implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify debugger functionality.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "debugger.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <chrono>
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
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif
        machine_.reset();

        // Start server on a dynamically allocated port
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(machine_, "127.0.0.1", 0);
        server_->start({}, {});

        // Create client channel using the actual bound port
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        debugger_stub_ = beebium::DebuggerControl::NewStub(channel_);
        device_inspection_stub_ = beebium::DeviceInspection::NewStub(channel_);
    }

    ~DebuggerTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::DebuggerControl::Stub& debugger() { return *debugger_stub_; }
    beebium::DeviceInspection::Stub& device_inspection() { return *device_inspection_stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::DebuggerControl::Stub> debugger_stub_;
    std::unique_ptr<beebium::DeviceInspection::Stub> device_inspection_stub_;
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
    CHECK(fixture.machine().cycle_count() == 7);  // Reset runs 7 cycles to complete reset sequence
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
    request.set_start_address(0xD9CD);
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
        request.set_start_address(0x1000);
        beebium::AddBreakpointResponse response;
        fixture.debugger().AddBreakpoint(&context, request, &response);
    }
    {
        grpc::ClientContext context;
        beebium::AddBreakpointRequest request;
        request.set_start_address(0x2000);
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
    CHECK(response.breakpoints(0).start_address() == 0x1000);
    CHECK(response.breakpoints(1).start_address() == 0x2000);
}

TEST_CASE("DebuggerControl RemoveBreakpoint removes a breakpoint", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Add a breakpoint
    uint32_t bp_id;
    {
        grpc::ClientContext context;
        beebium::AddBreakpointRequest request;
        request.set_start_address(0x3000);
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
        request.set_start_address(0x1000 + i * 0x100);
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
// CPU 6502 State Tests
//////////////////////////////////////////////////////////////////////////////

// Helpers over the family-agnostic CpuState: registers and signals are
// name/value pairs, so tests look them up by name.
namespace {
uint64_t reg_of(const beebium::CpuState& s, const std::string& name) {
    for (const auto& rv : s.registers()) {
        if (rv.name() == name) return rv.value();
    }
    FAIL("register not present: " << name);
    return 0;
}
const beebium::SignalState* signal_of(const beebium::CpuState& s, const std::string& name) {
    for (const auto& ss : s.signals()) {
        if (ss.name() == name) return &ss;
    }
    return nullptr;
}
void set_reg(beebium::CpuState& s, const std::string& name, uint64_t value) {
    auto* rv = s.add_registers();
    rv->set_name(name);
    rv->set_value(value);
}
}  // namespace

TEST_CASE("DebuggerControl GetCpuState returns CPU registers", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    // Set some known values
    fixture.machine().set_a(0x42);
    fixture.machine().set_x(0x11);
    fixture.machine().set_y(0x22);
    fixture.machine().set_sp(0xFF);
    fixture.machine().set_p(0x24);  // N=0, V=0, B=1, D=0, I=1, Z=0, C=0

    grpc::ClientContext context;
    beebium::Empty request;
    beebium::CpuState response;

    auto status = fixture.debugger().GetCpuState(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(reg_of(response, "A") == 0x42);
    CHECK(reg_of(response, "X") == 0x11);
    CHECK(reg_of(response, "Y") == 0x22);
    CHECK(reg_of(response, "SP") == 0xFF);
    CHECK(reg_of(response, "P") == 0x24);
}

TEST_CASE("DebuggerControl SetCpuState sets individual registers", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext context;
    beebium::CpuState request;
    set_reg(request, "A", 0xAA);
    set_reg(request, "X", 0xBB);
    beebium::CpuState response;

    auto status = fixture.debugger().SetCpuState(&context, request, &response);

    REQUIRE(status.ok());

    // The response is the atomically read-back state reflecting the writes.
    CHECK(reg_of(response, "A") == 0xAA);
    CHECK(reg_of(response, "X") == 0xBB);

    // Check the values were set
    CHECK(fixture.machine().a() == 0xAA);
    CHECK(fixture.machine().x() == 0xBB);
}

TEST_CASE("DebuggerControl SetCpuState can set PC", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext context;
    beebium::CpuState request;
    set_reg(request, "PC", 0xC000);
    beebium::CpuState response;

    auto status = fixture.debugger().SetCpuState(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(reg_of(response, "PC") == 0xC000);
    CHECK(fixture.machine().pc() == 0xC000);
}

TEST_CASE("DebuggerControl SetCpuState rejects an unknown register", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext context;
    beebium::CpuState request;
    set_reg(request, "ZZ", 1);
    beebium::CpuState response;

    auto status = fixture.debugger().SetCpuState(&context, request, &response);

    CHECK_FALSE(status.ok());
    CHECK(status.error_message().find("ZZ") != std::string::npos);
}

TEST_CASE("DebuggerControl GetCpuState returns interrupt signal state", "[grpc][debugger]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext context;
    beebium::Empty request;
    beebium::CpuState response;

    auto status = fixture.debugger().GetCpuState(&context, request, &response);

    REQUIRE(status.ok());

    // The 6502 reports IRQ and NMI signals.
    const auto* irq = signal_of(response, "IRQ");
    const auto* nmi = signal_of(response, "NMI");
    REQUIRE(irq != nullptr);
    REQUIRE(nmi != nullptr);

    // At rest, not inside any interrupt handler and no NMI edge latched.
    CHECK_FALSE(nmi->in_handler());
    CHECK_FALSE(irq->in_handler());
    CHECK_FALSE(nmi->pending());
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
        beebium::CpuState request;
        set_reg(request, "A", 0x99);
        beebium::CpuState response;
        fixture.debugger().SetCpuState(&context, request, &response);
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

//////////////////////////////////////////////////////////////////////////////
// Memory Region Access Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("DebuggerControl GetMemoryRegions returns regions for ModelB", "[grpc][debugger][regions]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext context;
    beebium::GetMemoryRegionsRequest request;
    beebium::GetMemoryRegionsResponse response;

    auto status = fixture.debugger().GetMemoryRegions(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.machine_type() == "model-b");

    // ModelB should have: main_ram, mos_rom, bank_0 through bank_15
    REQUIRE(response.regions_size() >= 18);

    // Check main_ram region
    bool found_main_ram = false;
    for (const auto& region : response.regions()) {
        if (region.name() == "main_ram") {
            found_main_ram = true;
            CHECK(region.size() == 32768);
            CHECK(region.readable());
            CHECK(region.writable());
            CHECK(region.populated());
        }
    }
    CHECK(found_main_ram);

    // Check mos_rom region
    bool found_mos_rom = false;
    for (const auto& region : response.regions()) {
        if (region.name() == "mos_rom") {
            found_mos_rom = true;
            CHECK(region.size() == 16384);
            CHECK(region.readable());
            CHECK_FALSE(region.writable());  // ROM is not writable
            CHECK(region.populated());
        }
    }
    CHECK(found_mos_rom);

    // Check that bank_0 exists and is active (default bank)
    bool found_bank_0 = false;
    for (const auto& region : response.regions()) {
        if (region.name() == "bank_0") {
            found_bank_0 = true;
            CHECK(region.size() == 16384);
            CHECK(region.active());  // Bank 0 is selected by default
        }
    }
    CHECK(found_bank_0);
}

TEST_CASE("DebuggerControl PeekRegion reads from main_ram", "[grpc][debugger][regions]") {
    DebuggerTestFixture fixture;

    // Write known data to main RAM using WriteMemory
    {
        grpc::ClientContext context;
        beebium::WriteMemoryRequest request;
        request.set_address(0x1234);
        request.set_data(std::string("\xDE\xAD\xBE\xEF", 4));
        beebium::WriteMemoryResponse response;
        fixture.debugger().WriteMemory(&context, request, &response);
    }

    // Read back using PeekRegion (absolute address)
    grpc::ClientContext context;
    beebium::RegionAccessRequest request;
    request.set_region_name("main_ram");
    request.set_address(0x1234);  // Absolute address (main_ram base is 0x0000)
    request.set_length(4);
    beebium::RegionAccessResponse response;

    auto status = fixture.debugger().PeekRegion(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 4);
    CHECK(static_cast<uint8_t>(response.data()[0]) == 0xDE);
    CHECK(static_cast<uint8_t>(response.data()[1]) == 0xAD);
    CHECK(static_cast<uint8_t>(response.data()[2]) == 0xBE);
    CHECK(static_cast<uint8_t>(response.data()[3]) == 0xEF);
}

TEST_CASE("DebuggerControl WriteRegion writes to main_ram", "[grpc][debugger][regions]") {
    DebuggerTestFixture fixture;

    // Write using WriteRegion (absolute address)
    {
        grpc::ClientContext context;
        beebium::WriteRegionRequest request;
        request.set_region_name("main_ram");
        request.set_address(0x5678);  // Absolute address (main_ram base is 0x0000)
        request.set_data(std::string("\xCA\xFE\xBA\xBE", 4));
        beebium::WriteRegionResponse response;

        auto status = fixture.debugger().WriteRegion(&context, request, &response);

        REQUIRE(status.ok());
        CHECK(response.success());
    }

    // Read back using PeekMemory (at address 0x5678)
    grpc::ClientContext context;
    beebium::PeekMemoryRequest request;
    request.set_address(0x5678);
    request.set_length(4);
    beebium::PeekMemoryResponse response;

    auto status = fixture.debugger().PeekMemory(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 4);
    CHECK(static_cast<uint8_t>(response.data()[0]) == 0xCA);
    CHECK(static_cast<uint8_t>(response.data()[1]) == 0xFE);
    CHECK(static_cast<uint8_t>(response.data()[2]) == 0xBA);
    CHECK(static_cast<uint8_t>(response.data()[3]) == 0xBE);
}

TEST_CASE("DebuggerControl PeekRegion can access sideways bank", "[grpc][debugger][regions]") {
    DebuggerTestFixture fixture;

    // Bank 0 should contain BASIC ROM (mapped at 0x8000-0xBFFF)
    grpc::ClientContext context;
    beebium::RegionAccessRequest request;
    request.set_region_name("bank_0");
    request.set_address(0x8000);  // Absolute address (banks are mapped at 0x8000)
    request.set_length(4);
    beebium::RegionAccessResponse response;

    auto status = fixture.debugger().PeekRegion(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 4);
    // BASIC ROM should have some content
}

//////////////////////////////////////////////////////////////////////////////
// PC-Context Memory Access Tests (for B+ shadow RAM routing)
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("DebuggerControl PeekMemory accepts simulated_pc field", "[grpc][debugger][pc-context]") {
    DebuggerTestFixture fixture;

    // Write some known data
    fixture.machine().write(0x5000, 0x42);

    // PeekMemory with simulated_pc should work
    grpc::ClientContext context;
    beebium::PeekMemoryRequest request;
    request.set_address(0x5000);
    request.set_length(1);
    request.set_simulated_pc(0xD000);  // MOS code PC
    beebium::PeekMemoryResponse response;

    auto status = fixture.debugger().PeekMemory(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 1);
    CHECK(static_cast<uint8_t>(response.data()[0]) == 0x42);
}

TEST_CASE("DebuggerControl ReadMemory accepts simulated_pc field", "[grpc][debugger][pc-context]") {
    DebuggerTestFixture fixture;

    // Write some known data
    fixture.machine().write(0x5000, 0xAA);

    // ReadMemory with simulated_pc should work
    grpc::ClientContext context;
    beebium::ReadMemoryRequest request;
    request.set_address(0x5000);
    request.set_length(1);
    request.set_simulated_pc(0xD000);  // MOS code PC
    beebium::ReadMemoryResponse response;

    auto status = fixture.debugger().ReadMemory(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 1);
    CHECK(static_cast<uint8_t>(response.data()[0]) == 0xAA);
}

TEST_CASE("DebuggerControl WriteMemory accepts simulated_pc field", "[grpc][debugger][pc-context]") {
    DebuggerTestFixture fixture;

    // WriteMemory with simulated_pc should work
    grpc::ClientContext context;
    beebium::WriteMemoryRequest request;
    request.set_address(0x5000);
    request.set_data(std::string("\x55", 1));
    request.set_simulated_pc(0xD000);  // MOS code PC
    beebium::WriteMemoryResponse response;

    auto status = fixture.debugger().WriteMemory(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success());

    // Verify data was written
    CHECK(fixture.machine().peek(0x5000) == 0x55);
}

TEST_CASE("Model B ignores simulated_pc (no shadow RAM)", "[grpc][debugger][pc-context]") {
    DebuggerTestFixture fixture;

    // Write known data at 0x5000 in RAM
    fixture.machine().write(0x5000, 0x42);

    // Reading with user code PC (would see main RAM on B+)
    grpc::ClientContext ctx1;
    beebium::PeekMemoryRequest req1;
    req1.set_address(0x5000);
    req1.set_length(1);
    req1.set_simulated_pc(0x1000);  // User code PC
    beebium::PeekMemoryResponse resp1;
    fixture.debugger().PeekMemory(&ctx1, req1, &resp1);

    // Reading with MOS code PC (would see shadow RAM on B+)
    grpc::ClientContext ctx2;
    beebium::PeekMemoryRequest req2;
    req2.set_address(0x5000);
    req2.set_length(1);
    req2.set_simulated_pc(0xD000);  // MOS code PC
    beebium::PeekMemoryResponse resp2;
    fixture.debugger().PeekMemory(&ctx2, req2, &resp2);

    // On Model B, both should return the same value (no shadow RAM)
    CHECK(static_cast<uint8_t>(resp1.data()[0]) == 0x42);
    CHECK(static_cast<uint8_t>(resp2.data()[0]) == 0x42);
}

TEST_CASE("PeekMemory without simulated_pc still works", "[grpc][debugger][pc-context]") {
    DebuggerTestFixture fixture;

    // Write known data
    fixture.machine().write(0x5000, 0xBB);

    // PeekMemory without simulated_pc (backward compatible)
    grpc::ClientContext context;
    beebium::PeekMemoryRequest request;
    request.set_address(0x5000);
    request.set_length(1);
    // NOT setting simulated_pc - using default behavior
    beebium::PeekMemoryResponse response;

    auto status = fixture.debugger().PeekMemory(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 1);
    CHECK(static_cast<uint8_t>(response.data()[0]) == 0xBB);
}

//////////////////////////////////////////////////////////////////////////////
// Inline Breakpoint Tests (Phase 1) -- with 6502 code
//////////////////////////////////////////////////////////////////////////////

// Helper to plant 6502 code at an address
static void plant_code(beebium::ModelB& machine, uint16_t addr,
                       const std::vector<uint8_t>& code) {
    for (size_t i = 0; i < code.size(); ++i) {
        machine.write(static_cast<uint16_t>(addr + i), code[i]);
    }
}

// Helper to prepare the machine for 6502 code execution.
// Call sequence: prepare_for_code() -> plant_code() -> set_pc()
// reset() clears RAM, so code must be planted after prepare.
// set_pc() peeks the opcode from memory, so it must be called after planting.
static void prepare_for_code(beebium::ModelB& machine) {
    machine.reset();
    machine.step_instruction();
    machine.pause();
}

TEST_CASE("Full-range breakpoint with cycle condition", "[debugger][breakpoint][6502]") {
    beebium::ModelB machine;
#ifdef BEEBIUM_ROM_DIR
    auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
    std::copy(mos.begin(), mos.end(), machine.state().memory.mos_rom.data());
#endif
    machine.reset();
    machine.step_instruction();

    uint64_t start = machine.cycle_count();
    uint64_t target = start + 1000;

    auto cond = beebium::compile("cycles >= " + std::to_string(target));
    REQUIRE(std::holds_alternative<beebium::CompiledExpression>(cond));

    machine.set_breakpoint_entries({beebium::BreakpointEntry{
        1, 0x0000, 0x10000u,
        false,
        std::get<beebium::CompiledExpression>(std::move(cond)),
        0
    }});

    bool hit = false;
    machine.set_breakpoint_hit_callback(
        [&](const beebium::BreakpointEntry& bp, uint16_t /*pc*/) {
            auto& mutable_bp = const_cast<beebium::BreakpointEntry&>(bp);
            ++mutable_bp.hit_count;
            bool should_stop = true;
            if (bp.condition) {
                beebium::ExprCpuState cpu_state{
                    machine.a(), machine.x(), machine.y(),
                    machine.sp(), machine.p(),
                    machine.cpu().opcode_pc.w, machine.cycle_count(),
                    bp.hit_count
                };
                should_stop = beebium::evaluate(*bp.condition, cpu_state, nullptr, nullptr) != 0;
            }
            if (should_stop) {
                hit = true;
                machine.pause();
            }
        });

    machine.resume();
    machine.run(100000);

    CHECK(hit);
    CHECK(machine.cycle_count() >= target);
    CHECK(machine.cycle_count() < target + 20);  // shouldn't overshoot much
}

TEST_CASE("6502 step_instruction sets registers correctly", "[debugger][6502]") {
    beebium::ModelB machine;
#ifdef BEEBIUM_ROM_DIR
    auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
    std::copy(mos.begin(), mos.end(), machine.state().memory.mos_rom.data());
#endif
    machine.reset();
    machine.step_instruction();  // complete reset

    // Plant LDA #$42 ; NOP at $0400
    machine.write(0x0400, 0xA9);
    machine.write(0x0401, 0x42);
    machine.write(0x0402, 0xEA);
    machine.set_pc(0x0400);

    CHECK(M6502_IsAboutToExecute(&machine.cpu()));

    // Execute LDA #$42 via step_instruction
    machine.step_instruction();
    CHECK(machine.pc() == 0x0402);
    CHECK(static_cast<int>(machine.a()) == 0x42);
}

TEST_CASE("Direct inline breakpoint without gRPC", "[debugger][breakpoint][6502]") {
    beebium::ModelB machine;
#ifdef BEEBIUM_ROM_DIR
    auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
    std::copy(mos.begin(), mos.end(), machine.state().memory.mos_rom.data());
#endif
    machine.reset();
    machine.step_instruction();

    // Plant LDA #$42 ; NOP at $0400
    machine.write(0x0400, 0xA9);
    machine.write(0x0401, 0x42);
    machine.write(0x0402, 0xEA);
    machine.write(0x0403, 0xEA);
    machine.set_pc(0x0400);

    CHECK(M6502_IsAboutToExecute(&machine.cpu()));

    // Set breakpoint at $0402
    machine.set_breakpoint_entries({beebium::BreakpointEntry{1, 0x0402, 0x0403}});
    bool hit = false;
    uint16_t hit_pc = 0;
    machine.set_breakpoint_hit_callback([&](const beebium::BreakpointEntry&, uint16_t pc) {
        hit = true;
        hit_pc = pc;
        machine.pause();
    });

    machine.run(100);

    CHECK(hit);
    CHECK(hit_pc == 0x0402);
    CHECK(machine.is_paused());
    CHECK(machine.pc() == 0x0402);
    CHECK(static_cast<int>(machine.a()) == 0x42);
}

TEST_CASE("Breakpoint stops at correct PC with 6502 code", "[grpc][debugger][breakpoint][6502]") {
    DebuggerTestFixture fixture;

    // Prepare: reset, run reset sequence, pause, set PC
    prepare_for_code(fixture.machine());

    // Plant code at $0400 (after reset, so RAM is clear):
    //   LDA #$42     ; A9 42
    //   STA $0500    ; 8D 00 05
    //   LDA #$00     ; A9 00
    //   NOP          ; EA
    plant_code(fixture.machine(), 0x0400, {
        0xA9, 0x42,       // LDA #$42
        0x8D, 0x00, 0x05, // STA $0500
        0xA9, 0x00,       // LDA #$00
        0xEA,             // NOP
    });
    fixture.machine().set_pc(0x0400);


    // Set breakpoint at the STA instruction ($0402)
    uint32_t bp_id;
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0402);
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
        bp_id = resp.id();
    }

    // Resume and run for enough cycles to execute the LDA and hit the STA
    fixture.machine().resume();
    fixture.machine().run(100);

    // Machine should be paused at $0402 (breakpoint hit before executing STA)
    CHECK(fixture.machine().is_paused());
    CHECK(fixture.machine().pc() == 0x0402);
    // A should be $42 (LDA #$42 executed)
    CHECK(fixture.machine().a() == 0x42);
    // $0500 should NOT have been written yet (STA not executed)
    CHECK(fixture.machine().peek(0x0500) == 0x00);

    // Clean up
    {
        grpc::ClientContext ctx;
        beebium::RemoveBreakpointRequest req;
        req.set_id(bp_id);
        beebium::RemoveBreakpointResponse resp;
        fixture.debugger().RemoveBreakpoint(&ctx, req, &resp);
    }
}

TEST_CASE("Breakpoint fires in a loop", "[grpc][debugger][breakpoint][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    // Code at $0400: increment X and loop back
    //   LDX #$00     ; A2 00
    //   INX          ; E8      ($0402)
    //   JMP $0402    ; 4C 02 04
    plant_code(fixture.machine(), 0x0400, {
        0xA2, 0x00,       // LDX #$00
        0xE8,             // INX
        0x4C, 0x02, 0x04, // JMP $0402
    });
    fixture.machine().set_pc(0x0400);


    // Breakpoint on the INX at $0402
    uint32_t bp_id;
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0402);
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        bp_id = resp.id();
    }

    // First hit: X should be 0 (LDX #0 executed, INX not yet)
    fixture.machine().resume();
    fixture.machine().run(100);
    CHECK(fixture.machine().is_paused());
    CHECK(fixture.machine().pc() == 0x0402);
    CHECK(fixture.machine().x() == 0x00);

    // Step one instruction (executes INX), then resume to hit breakpoint again
    fixture.machine().step_instruction();
    CHECK(fixture.machine().x() == 0x01);

    fixture.machine().resume();
    fixture.machine().run(100);
    CHECK(fixture.machine().is_paused());
    CHECK(fixture.machine().pc() == 0x0402);
    CHECK(fixture.machine().x() == 0x01);

    // Clean up
    {
        grpc::ClientContext ctx;
        beebium::RemoveBreakpointRequest req;
        req.set_id(bp_id);
        beebium::RemoveBreakpointResponse resp;
        fixture.debugger().RemoveBreakpoint(&ctx, req, &resp);
    }
}

TEST_CASE("Multiple breakpoints fire correctly", "[grpc][debugger][breakpoint][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    // Code at $0400:
    //   NOP          ; EA      ($0400)
    //   NOP          ; EA      ($0401)
    //   NOP          ; EA      ($0402)
    //   NOP          ; EA      ($0403)
    plant_code(fixture.machine(), 0x0400, {0xEA, 0xEA, 0xEA, 0xEA});
    fixture.machine().set_pc(0x0400);


    // Set breakpoints at $0401 and $0403
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0401);
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
    }
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0403);
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
    }

    // Run: should stop at $0401 (first breakpoint)
    fixture.machine().resume();
    fixture.machine().run(100);
    CHECK(fixture.machine().pc() == 0x0401);

    // Step past the breakpoint, then resume to hit the second breakpoint
    fixture.machine().step_instruction();

    // Resume: should stop at $0403 (second breakpoint, skipping $0402 NOP)
    fixture.machine().resume();
    fixture.machine().run(100);
    CHECK(fixture.machine().pc() == 0x0403);

    // Clean up
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ClearBreakpointsResponse resp;
        fixture.debugger().ClearBreakpoints(&ctx, req, &resp);
    }
}

TEST_CASE("No breakpoints: run completes normally", "[grpc][debugger][breakpoint][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    // Simple code at $0400: three NOPs
    plant_code(fixture.machine(), 0x0400, {0xEA, 0xEA, 0xEA});
    fixture.machine().set_pc(0x0400);


    // Run with no breakpoints
    fixture.machine().resume();
    uint64_t before = fixture.machine().cycle_count();
    fixture.machine().run(100);

    // Should NOT be paused (no breakpoints to hit)
    CHECK(!fixture.machine().is_paused());
    CHECK(fixture.machine().cycle_count() >= before + 100);
}

//////////////////////////////////////////////////////////////////////////////
// Watchpoint Tests (Phase 2) -- with 6502 code
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Write watchpoint fires on STA", "[grpc][debugger][watchpoint][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    // LDA #$42 ; STA $0500
    plant_code(fixture.machine(), 0x0400, {
        0xA9, 0x42,       // LDA #$42
        0x8D, 0x00, 0x05, // STA $0500
        0xEA,             // NOP (should not reach)
    });
    fixture.machine().set_pc(0x0400);


    // Add write watchpoint on $0500 (single address)
    uint32_t wp_id;
    {
        grpc::ClientContext ctx;
        beebium::AddWatchpointRequest req;
        req.set_start_address(0x0500);
        req.set_end_address(0x0501);  // exclusive
        req.set_type(beebium::WATCHPOINT_WRITE);
        beebium::AddWatchpointResponse resp;
        fixture.debugger().AddWatchpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
        wp_id = resp.id();
    }

    fixture.machine().resume();
    fixture.machine().run(100);

    // Machine should have stopped due to watchpoint
    CHECK(fixture.machine().is_paused());
    // $0500 should have been written (the STA executed, then watchpoint fired)
    CHECK(fixture.machine().peek(0x0500) == 0x42);

    // Clean up
    {
        grpc::ClientContext ctx;
        beebium::RemoveWatchpointRequest req;
        req.set_id(wp_id);
        beebium::RemoveWatchpointResponse resp;
        fixture.debugger().RemoveWatchpoint(&ctx, req, &resp);
    }
}

TEST_CASE("Read watchpoint fires on LDA from memory", "[grpc][debugger][watchpoint][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    // Put a value at $0500 and read it
    fixture.machine().write(0x0500, 0xAB);

    // LDA $0500
    plant_code(fixture.machine(), 0x0400, {
        0xAD, 0x00, 0x05, // LDA $0500
        0xEA,             // NOP
    });
    fixture.machine().set_pc(0x0400);


    // Add read watchpoint on $0500
    uint32_t wp_id;
    {
        grpc::ClientContext ctx;
        beebium::AddWatchpointRequest req;
        req.set_start_address(0x0500);
        req.set_end_address(0x0501);
        req.set_type(beebium::WATCHPOINT_READ);
        beebium::AddWatchpointResponse resp;
        fixture.debugger().AddWatchpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
        wp_id = resp.id();
    }

    fixture.machine().resume();
    fixture.machine().run(100);

    CHECK(fixture.machine().is_paused());
    // The watchpoint fires during the bus access cycle, before the
    // instruction completes. The value was read (it's on the data bus)
    // but A hasn't been updated yet (that happens in the next tfn).
    // Verify the address $0500 still contains the expected value.
    CHECK(fixture.machine().peek(0x0500) == 0xAB);

    // Clean up
    {
        grpc::ClientContext ctx;
        beebium::RemoveWatchpointRequest req;
        req.set_id(wp_id);
        beebium::RemoveWatchpointResponse resp;
        fixture.debugger().RemoveWatchpoint(&ctx, req, &resp);
    }
}

TEST_CASE("Write-only watchpoint does not fire on read", "[grpc][debugger][watchpoint][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    fixture.machine().write(0x0500, 0x77);

    // LDA $0500 (read, not write) ; NOP ; NOP
    plant_code(fixture.machine(), 0x0400, {
        0xAD, 0x00, 0x05, // LDA $0500
        0xEA, 0xEA,
    });
    fixture.machine().set_pc(0x0400);


    // Write-only watchpoint
    {
        grpc::ClientContext ctx;
        beebium::AddWatchpointRequest req;
        req.set_start_address(0x0500);
        req.set_end_address(0x0501);
        req.set_type(beebium::WATCHPOINT_WRITE);
        beebium::AddWatchpointResponse resp;
        fixture.debugger().AddWatchpoint(&ctx, req, &resp);
    }

    fixture.machine().resume();
    fixture.machine().run(30);

    // Should NOT have stopped (read doesn't fire write watchpoint)
    CHECK(!fixture.machine().is_paused());

    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ClearWatchpointsResponse resp;
        fixture.debugger().ClearWatchpoints(&ctx, req, &resp);
    }
}

TEST_CASE("Watchpoint on address range fires for any address in range", "[grpc][debugger][watchpoint][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    // STA $0503 (write to middle of range $0500-$0510)
    plant_code(fixture.machine(), 0x0400, {
        0xA9, 0x11,       // LDA #$11
        0x8D, 0x03, 0x05, // STA $0503
        0xEA,
    });
    fixture.machine().set_pc(0x0400);


    // Watchpoint on range [$0500, $0510)
    {
        grpc::ClientContext ctx;
        beebium::AddWatchpointRequest req;
        req.set_start_address(0x0500);
        req.set_end_address(0x0510);
        req.set_type(beebium::WATCHPOINT_WRITE);
        beebium::AddWatchpointResponse resp;
        fixture.debugger().AddWatchpoint(&ctx, req, &resp);
    }

    fixture.machine().resume();
    fixture.machine().run(100);

    CHECK(fixture.machine().is_paused());
    CHECK(fixture.machine().peek(0x0503) == 0x11);

    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ClearWatchpointsResponse resp;
        fixture.debugger().ClearWatchpoints(&ctx, req, &resp);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Conditional Breakpoint Tests (Phase 4)
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Conditional breakpoint fires only when A == 0x42", "[grpc][debugger][breakpoint][conditional][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    // Loop: LDA values from a table, INX, BNE loop
    //   $0400: LDX #$00
    //   $0402: LDA $0420,X   ; load from table at $0420
    //   $0405: INX
    //   $0406: CPX #$04      ; 4 entries
    //   $0408: BNE $0402
    //   $040A: NOP
    plant_code(fixture.machine(), 0x0400, {
        0xA2, 0x00,             // LDX #$00
        0xBD, 0x20, 0x04,       // LDA $0420,X
        0xE8,                   // INX
        0xE0, 0x04,             // CPX #$04
        0xD0, 0xF8,             // BNE $0402 (relative -8)
        0xEA,                   // NOP
    });
    fixture.machine().set_pc(0x0400);
    // Table at $0420: values to load
    fixture.machine().write(0x0420, 0x10);  // iteration 0: A=$10
    fixture.machine().write(0x0421, 0x42);  // iteration 1: A=$42 <- should fire
    fixture.machine().write(0x0422, 0x99);  // iteration 2: A=$99
    fixture.machine().write(0x0423, 0xFF);  // iteration 3: A=$FF


    // Conditional breakpoint at LDA instruction: stop only when A == 0x42
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0402);
        req.set_condition("A == 0x42");
        beebium::AddBreakpointResponse resp;
        auto status = fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        REQUIRE(status.ok());
        REQUIRE(resp.success());
    }

    fixture.machine().resume();
    fixture.machine().run(500);

    CHECK(fixture.machine().is_paused());
    // The breakpoint fires BEFORE executing the instruction at $0402.
    // A was set to $0x42 by the previous iteration's LDA, and the condition
    // evaluated to true, so we stopped. X should be 2 (two INX executed).
    CHECK(static_cast<int>(fixture.machine().a()) == 0x42);
    CHECK(static_cast<int>(fixture.machine().x()) == 0x02);

    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ClearBreakpointsResponse resp;
        fixture.debugger().ClearBreakpoints(&ctx, req, &resp);
    }
}

TEST_CASE("Breakpoint with hits == 3 fires on third hit", "[grpc][debugger][breakpoint][conditional][hits][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    // Tight loop: INX, JMP back
    //   $0400: LDX #$00
    //   $0402: INX
    //   $0403: JMP $0402
    plant_code(fixture.machine(), 0x0400, {
        0xA2, 0x00,             // LDX #$00
        0xE8,                   // INX
        0x4C, 0x02, 0x04,       // JMP $0402
    });
    fixture.machine().set_pc(0x0400);


    // Breakpoint at $0402 with condition "hits == 3"
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0402);
        req.set_condition("hits == 3");
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    fixture.machine().resume();
    fixture.machine().run(500);

    CHECK(fixture.machine().is_paused());
    // X should be 2: the breakpoint fires before the 3rd INX executes.
    // hits 1 (X=0), hits 2 (X=1), hits 3 (X=2) -> stops
    CHECK(static_cast<int>(fixture.machine().x()) == 0x02);

    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ClearBreakpointsResponse resp;
        fixture.debugger().ClearBreakpoints(&ctx, req, &resp);
    }
}

TEST_CASE("Invalid condition string returns gRPC error", "[grpc][debugger][breakpoint][conditional]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext ctx;
    beebium::AddBreakpointRequest req;
    req.set_start_address(0x1000);
    req.set_condition("invalid garbage !@#");
    beebium::AddBreakpointResponse resp;

    auto status = fixture.debugger().AddBreakpoint(&ctx, req, &resp);
    CHECK(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_CASE("Invalid watchpoint condition returns gRPC error", "[grpc][debugger][watchpoint][conditional]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext ctx;
    beebium::AddWatchpointRequest req;
    req.set_start_address(0x1000);
    req.set_end_address(0x1001);
    req.set_condition("foo bar");
    beebium::AddWatchpointResponse resp;

    auto status = fixture.debugger().AddWatchpoint(&ctx, req, &resp);
    CHECK(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_CASE("Conditional watchpoint fires only when value matches", "[grpc][debugger][watchpoint][conditional][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    // Write three different values to $0500
    //   LDA #$10 ; STA $0500
    //   LDA #$42 ; STA $0500   <- watchpoint should fire here (value $42)
    //   LDA #$99 ; STA $0500
    //   NOP
    plant_code(fixture.machine(), 0x0400, {
        0xA9, 0x10,             // LDA #$10
        0x8D, 0x00, 0x05,       // STA $0500
        0xA9, 0x42,             // LDA #$42
        0x8D, 0x00, 0x05,       // STA $0500
        0xA9, 0x99,             // LDA #$99
        0x8D, 0x00, 0x05,       // STA $0500
        0xEA,                   // NOP
    });
    fixture.machine().set_pc(0x0400);


    // Write watchpoint on $0500 with condition: stop only when A == 0x42
    {
        grpc::ClientContext ctx;
        beebium::AddWatchpointRequest req;
        req.set_start_address(0x0500);
        req.set_end_address(0x0501);
        req.set_type(beebium::WATCHPOINT_WRITE);
        req.set_condition("A == 0x42");
        beebium::AddWatchpointResponse resp;
        fixture.debugger().AddWatchpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    fixture.machine().resume();
    fixture.machine().run(500);

    CHECK(fixture.machine().is_paused());
    // The first STA (A=$10) matched the address but the condition was false.
    // The second STA (A=$42) matched and condition was true -> stopped.
    CHECK(fixture.machine().peek(0x0500) == 0x42);

    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ClearWatchpointsResponse resp;
        fixture.debugger().ClearWatchpoints(&ctx, req, &resp);
    }
}

TEST_CASE("Multiple breakpoints at same address with different conditions", "[grpc][debugger][breakpoint][conditional][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    // Loop: increment A from 0, breakpoint at loop top
    //   $0400: LDA #$00
    //   $0402: CLC ; ADC #$01 ; JMP $0402
    plant_code(fixture.machine(), 0x0400, {
        0xA9, 0x00,             // LDA #$00
        0x18,                   // CLC
        0x69, 0x01,             // ADC #$01
        0x4C, 0x02, 0x04,       // JMP $0402
    });
    fixture.machine().set_pc(0x0400);


    // Two breakpoints at $0402 with different conditions
    uint32_t bp1_id;
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0402);
        req.set_condition("A == 0x03");
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
        bp1_id = resp.id();
    }
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0402);
        req.set_condition("A == 0x05");
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    // A counts up: 0, 1, 2, 3... Should stop when A==3 (first matching condition)
    fixture.machine().resume();
    fixture.machine().run(500);

    CHECK(fixture.machine().is_paused());
    int a_val = static_cast<int>(fixture.machine().a());
    CHECK((a_val == 0x03 || a_val == 0x05));

    if (a_val == 0x03) {
        // Remove the A==3 breakpoint and resume to hit A==5
        grpc::ClientContext ctx;
        beebium::RemoveBreakpointRequest req;
        req.set_id(bp1_id);
        beebium::RemoveBreakpointResponse resp;
        fixture.debugger().RemoveBreakpoint(&ctx, req, &resp);
    } else {
        // Remove the A==5 breakpoint and resume to hit A==3... wait, we passed it.
        // This case shouldn't happen since A==3 comes before A==5 chronologically.
        FAIL("A==5 fired before A==3 -- unexpected ordering");
    }

    fixture.machine().step_instruction();
    fixture.machine().resume();
    fixture.machine().run(500);

    CHECK(fixture.machine().is_paused());
    CHECK(static_cast<int>(fixture.machine().a()) == 0x05);

    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ClearBreakpointsResponse resp;
        fixture.debugger().ClearBreakpoints(&ctx, req, &resp);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Original watchpoint lifecycle test
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Watchpoint lifecycle: add/remove/list/clear", "[grpc][debugger][watchpoint]") {
    DebuggerTestFixture fixture;

    // Add two watchpoints
    uint32_t wp1_id, wp2_id;
    {
        grpc::ClientContext ctx;
        beebium::AddWatchpointRequest req;
        req.set_start_address(0x1000);
        req.set_end_address(0x1010);
        req.set_type(beebium::WATCHPOINT_READ);
        beebium::AddWatchpointResponse resp;
        fixture.debugger().AddWatchpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
        wp1_id = resp.id();
    }
    {
        grpc::ClientContext ctx;
        beebium::AddWatchpointRequest req;
        req.set_start_address(0x2000);
        req.set_end_address(0x2001);
        req.set_type(beebium::WATCHPOINT_WRITE);
        beebium::AddWatchpointResponse resp;
        fixture.debugger().AddWatchpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
        wp2_id = resp.id();
    }

    // List
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListWatchpointsResponse resp;
        fixture.debugger().ListWatchpoints(&ctx, req, &resp);
        CHECK(resp.watchpoints_size() == 2);
    }

    // Remove first
    {
        grpc::ClientContext ctx;
        beebium::RemoveWatchpointRequest req;
        req.set_id(wp1_id);
        beebium::RemoveWatchpointResponse resp;
        fixture.debugger().RemoveWatchpoint(&ctx, req, &resp);
        CHECK(resp.success());
    }

    // List again
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListWatchpointsResponse resp;
        fixture.debugger().ListWatchpoints(&ctx, req, &resp);
        CHECK(resp.watchpoints_size() == 1);
        CHECK(resp.watchpoints(0).id() == wp2_id);
    }

    // Clear
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ClearWatchpointsResponse resp;
        fixture.debugger().ClearWatchpoints(&ctx, req, &resp);
        CHECK(resp.count_removed() == 1);
    }

    // List should be empty
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListWatchpointsResponse resp;
        fixture.debugger().ListWatchpoints(&ctx, req, &resp);
        CHECK(resp.watchpoints_size() == 0);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Enable/Disable Breakpoints
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("EnableBreakpoint toggles enabled state", "[grpc][debugger][breakpoint]") {
    DebuggerTestFixture fixture;

    // Add a breakpoint
    uint32_t bp_id;
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x1000);
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
        bp_id = resp.id();
    }

    // Verify it starts enabled
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListBreakpointsResponse resp;
        fixture.debugger().ListBreakpoints(&ctx, req, &resp);
        REQUIRE(resp.breakpoints_size() == 1);
        CHECK(resp.breakpoints(0).enabled() == true);
    }

    // Disable it
    {
        grpc::ClientContext ctx;
        beebium::EnableBreakpointRequest req;
        req.set_id(bp_id);
        req.set_enabled(false);
        beebium::EnableBreakpointResponse resp;
        fixture.debugger().EnableBreakpoint(&ctx, req, &resp);
        CHECK(resp.success());
    }

    // Verify it's disabled
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListBreakpointsResponse resp;
        fixture.debugger().ListBreakpoints(&ctx, req, &resp);
        REQUIRE(resp.breakpoints_size() == 1);
        CHECK(resp.breakpoints(0).enabled() == false);
    }

    // Re-enable it
    {
        grpc::ClientContext ctx;
        beebium::EnableBreakpointRequest req;
        req.set_id(bp_id);
        req.set_enabled(true);
        beebium::EnableBreakpointResponse resp;
        fixture.debugger().EnableBreakpoint(&ctx, req, &resp);
        CHECK(resp.success());
    }

    // Verify it's enabled again
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListBreakpointsResponse resp;
        fixture.debugger().ListBreakpoints(&ctx, req, &resp);
        REQUIRE(resp.breakpoints_size() == 1);
        CHECK(resp.breakpoints(0).enabled() == true);
    }
}

TEST_CASE("EnableBreakpoint with non-existent ID returns failure", "[grpc][debugger][breakpoint]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext ctx;
    beebium::EnableBreakpointRequest req;
    req.set_id(9999);
    req.set_enabled(false);
    beebium::EnableBreakpointResponse resp;
    fixture.debugger().EnableBreakpoint(&ctx, req, &resp);
    CHECK_FALSE(resp.success());
}

TEST_CASE("AddBreakpoint with enabled=false creates disabled breakpoint", "[grpc][debugger][breakpoint]") {
    DebuggerTestFixture fixture;

    // Add a disabled breakpoint
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x1000);
        req.set_enabled(false);
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    // Verify it's disabled
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListBreakpointsResponse resp;
        fixture.debugger().ListBreakpoints(&ctx, req, &resp);
        REQUIRE(resp.breakpoints_size() == 1);
        CHECK(resp.breakpoints(0).enabled() == false);
    }
}

TEST_CASE("Disabled breakpoint does not fire", "[grpc][debugger][breakpoint][6502]") {
    DebuggerTestFixture fixture;

    prepare_for_code(fixture.machine());

    // NOP; NOP; NOP; NOP
    plant_code(fixture.machine(), 0x0400, {0xEA, 0xEA, 0xEA, 0xEA});
    fixture.machine().set_pc(0x0400);

    // Add breakpoint at NOP #2 (0x0401), then disable it via gRPC
    uint32_t bp_id;
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0401);
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
        bp_id = resp.id();
    }
    {
        grpc::ClientContext ctx;
        beebium::EnableBreakpointRequest req;
        req.set_id(bp_id);
        req.set_enabled(false);
        beebium::EnableBreakpointResponse resp;
        fixture.debugger().EnableBreakpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    // Add an enabled breakpoint at NOP #3 (0x0402)
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0402);
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    // Run directly -- should skip 0x0401 and stop at 0x0402
    fixture.machine().resume();
    fixture.machine().run(100);

    CHECK(fixture.machine().is_paused());
    CHECK(fixture.machine().pc() == 0x0402);
}

//////////////////////////////////////////////////////////////////////////////
// Enable/Disable Watchpoints
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("EnableWatchpoint toggles enabled state", "[grpc][debugger][watchpoint]") {
    DebuggerTestFixture fixture;

    // Add a watchpoint
    uint32_t wp_id;
    {
        grpc::ClientContext ctx;
        beebium::AddWatchpointRequest req;
        req.set_start_address(0x1000);
        req.set_end_address(0x1001);
        req.set_type(beebium::WATCHPOINT_WRITE);
        beebium::AddWatchpointResponse resp;
        fixture.debugger().AddWatchpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
        wp_id = resp.id();
    }

    // Verify it starts enabled
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListWatchpointsResponse resp;
        fixture.debugger().ListWatchpoints(&ctx, req, &resp);
        REQUIRE(resp.watchpoints_size() == 1);
        CHECK(resp.watchpoints(0).enabled() == true);
    }

    // Disable it
    {
        grpc::ClientContext ctx;
        beebium::EnableWatchpointRequest req;
        req.set_id(wp_id);
        req.set_enabled(false);
        beebium::EnableWatchpointResponse resp;
        fixture.debugger().EnableWatchpoint(&ctx, req, &resp);
        CHECK(resp.success());
    }

    // Verify it's disabled
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListWatchpointsResponse resp;
        fixture.debugger().ListWatchpoints(&ctx, req, &resp);
        REQUIRE(resp.watchpoints_size() == 1);
        CHECK(resp.watchpoints(0).enabled() == false);
    }

    // Re-enable it
    {
        grpc::ClientContext ctx;
        beebium::EnableWatchpointRequest req;
        req.set_id(wp_id);
        req.set_enabled(true);
        beebium::EnableWatchpointResponse resp;
        fixture.debugger().EnableWatchpoint(&ctx, req, &resp);
        CHECK(resp.success());
    }

    // Verify it's enabled again
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListWatchpointsResponse resp;
        fixture.debugger().ListWatchpoints(&ctx, req, &resp);
        REQUIRE(resp.watchpoints_size() == 1);
        CHECK(resp.watchpoints(0).enabled() == true);
    }
}

TEST_CASE("EnableWatchpoint with non-existent ID returns failure", "[grpc][debugger][watchpoint]") {
    DebuggerTestFixture fixture;

    grpc::ClientContext ctx;
    beebium::EnableWatchpointRequest req;
    req.set_id(9999);
    req.set_enabled(false);
    beebium::EnableWatchpointResponse resp;
    fixture.debugger().EnableWatchpoint(&ctx, req, &resp);
    CHECK_FALSE(resp.success());
}

TEST_CASE("AddWatchpoint with enabled=false creates disabled watchpoint", "[grpc][debugger][watchpoint]") {
    DebuggerTestFixture fixture;

    // Add a disabled watchpoint
    {
        grpc::ClientContext ctx;
        beebium::AddWatchpointRequest req;
        req.set_start_address(0x1000);
        req.set_end_address(0x1001);
        req.set_type(beebium::WATCHPOINT_WRITE);
        req.set_enabled(false);
        beebium::AddWatchpointResponse resp;
        fixture.debugger().AddWatchpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    // Verify it's disabled
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListWatchpointsResponse resp;
        fixture.debugger().ListWatchpoints(&ctx, req, &resp);
        REQUIRE(resp.watchpoints_size() == 1);
        CHECK(resp.watchpoints(0).enabled() == false);
    }
}

TEST_CASE("Clear removes disabled breakpoints", "[grpc][debugger][breakpoint]") {
    DebuggerTestFixture fixture;

    // Add two breakpoints, disable one
    uint32_t bp1_id;
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x1000);
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        bp1_id = resp.id();
    }
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x2000);
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
    }
    {
        grpc::ClientContext ctx;
        beebium::EnableBreakpointRequest req;
        req.set_id(bp1_id);
        req.set_enabled(false);
        beebium::EnableBreakpointResponse resp;
        fixture.debugger().EnableBreakpoint(&ctx, req, &resp);
    }

    // Clear should remove both
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ClearBreakpointsResponse resp;
        fixture.debugger().ClearBreakpoints(&ctx, req, &resp);
        CHECK(resp.count_removed() == 2);
    }
    {
        grpc::ClientContext ctx;
        beebium::Empty req;
        beebium::ListBreakpointsResponse resp;
        fixture.debugger().ListBreakpoints(&ctx, req, &resp);
        CHECK(resp.breakpoints_size() == 0);
    }
}

// =============================================================================
// Issue #79: a cycle-budget stop must fire even while the CPU is halted
// =============================================================================
//
// The clients implement run_until_or_timeout by installing a full-range address
// breakpoint [0x0000, 0x10000) with a "cycles >= N" condition. Machine::run()
// evaluates breakpoints only at an opcode fetch (M6502_IsAboutToExecute), so
// while the CPU is halted -- by a jam (KIL) opcode or by holding Break -- there
// are no opcode fetches, the condition is never evaluated, and the stop never
// fires although cycle_count keeps advancing. The client then waits out its 30 s
// gRPC deadline.
//
// These assert the DESIRED behaviour: the machine stops once the cycle target is
// reached, whatever the CPU is doing. They are RED until #79 is fixed. Their
// final form depends on the fix chosen (fixing conditional-breakpoint evaluation
// vs a first-class temporal breakpoint), so treat them as the starting point.

TEST_CASE("Cycle-budget stop must fire while the CPU is jammed (#79)",
          "[grpc][debugger][cycle]") {
    DebuggerTestFixture fixture;
    prepare_for_code(fixture.machine());

    // A KIL/jam opcode at &0400: executing it halts the CPU (no more opcode
    // fetches), exactly the empty-bank crash shape from #78.
    plant_code(fixture.machine(), 0x0400, {0x02});
    fixture.machine().set_pc(0x0400);

    const uint64_t target = fixture.machine().cycle_count() + 1000;
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0000);
        req.set_end_address(0x10000);
        req.set_condition("cycles >= " + std::to_string(target));
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    fixture.machine().resume();
    fixture.machine().run(100000);

    // The machine kept ticking well past the target...
    CHECK(fixture.machine().cycle_count() >= target);
    // ...and the cycle-budget stop must have fired. RED today: it never does
    // while the CPU is jammed.
    CHECK(fixture.machine().is_paused());
}

TEST_CASE("Cycle-budget stop must fire while Break is held (#79)",
          "[grpc][debugger][cycle]") {
    DebuggerTestFixture fixture;
    prepare_for_code(fixture.machine());

    // Hold the Break/reset line: the CPU is halted, so it presents no opcode
    // fetches even though the peripherals and cycle_count keep advancing.
    fixture.machine().break_down();

    const uint64_t target = fixture.machine().cycle_count() + 1000;
    {
        grpc::ClientContext ctx;
        beebium::AddBreakpointRequest req;
        req.set_start_address(0x0000);
        req.set_end_address(0x10000);
        req.set_condition("cycles >= " + std::to_string(target));
        beebium::AddBreakpointResponse resp;
        fixture.debugger().AddBreakpoint(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    fixture.machine().resume();
    fixture.machine().run(100000);

    CHECK(fixture.machine().cycle_count() >= target);
    CHECK(fixture.machine().is_paused());  // RED today: never fires while halted
}

TEST_CASE("WatchExecutionState delivers events without waiting for its poll timeout",
          "[grpc][debugger][watch]") {
    // Each published event must wake the stream promptly. A lost wakeup leaves
    // the subscriber asleep until its 100 ms fallback timeout, so every event
    // arrives ~100 ms late. Take the fastest of several deliveries so a slow
    // host cannot fail the test, while a lost wakeup still does.
    DebuggerTestFixture fixture;

    grpc::ClientContext watch_context;
    // A missing event fails the test at the deadline instead of hanging it.
    watch_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(30));
    beebium::WatchExecutionStateRequest watch_request;
    auto reader = fixture.debugger().WatchExecutionState(&watch_context, watch_request);
    beebium::ExecutionStateEvent event;
    REQUIRE(reader->Read(&event));  // initial snapshot

    auto fastest = std::chrono::steady_clock::duration::max();
    for (int i = 0; i < 6; ++i) {
        const bool run = (i % 2) == 1;  // the machine starts running: stop first
        auto start = std::chrono::steady_clock::now();
        {
            grpc::ClientContext context;
            beebium::Empty request;
            if (run) {
                beebium::RunResponse response;
                REQUIRE(fixture.debugger().Run(&context, request, &response).ok());
            } else {
                beebium::StopResponse response;
                REQUIRE(fixture.debugger().Stop(&context, request, &response).ok());
            }
        }
        REQUIRE(reader->Read(&event));
        CHECK(event.state().is_running() == run);
        fastest = std::min(fastest, std::chrono::steady_clock::now() - start);
    }
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(fastest).count() < 50);

    watch_context.TryCancel();
    reader->Finish();
}
