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

// The opposite of the Step 1b "unserved family" test: a coprocessor of a
// completely made-up CPU family, with register and signal names no server code
// knows, is served in full -- descriptor, state, and set -- through the one
// family-agnostic debugger. Nothing here is a 6502.

#include <catch2/catch_test_macros.hpp>

#include <beebium/extension/CpuDebugTarget.hpp>
#include <beebium/service/DebuggerService.hpp>
#include "debugger.pb.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace beebium;

namespace {

// A fictional CPU: two data registers, a 24-bit instruction pointer, a stack
// pointer, and a flags register, plus two interrupt lines. None of these names
// appears anywhere in the server or the protos.
class StubCpuDebugTarget : public CpuDebugTarget {
public:
    StubCpuDebugTarget() {
        descriptor_.family = "quux-9000";
        descriptor_.address_bits = 16;
        descriptor_.little_endian = false;
        descriptor_.registers = {
            {"D0", 16, cpu::RegisterRole::None, {}},
            {"D1", 16, cpu::RegisterRole::None, {}},
            {"IP", 24, cpu::RegisterRole::ProgramCounter, {}},
            {"USP", 16, cpu::RegisterRole::StackPointer, {}},
            {"FLG", 8, cpu::RegisterRole::Flags, {"CARRY", "OVER", "", "HALT"}},
        };
        descriptor_.signals = {"INT", "TRAP"};
        registers_.resize(descriptor_.registers.size(), 0);
    }

    const cpu::CpuDescriptor& cpu_descriptor() const override { return descriptor_; }
    uint64_t register_value(size_t index) const override { return registers_.at(index); }
    void set_register_value(size_t index, uint64_t value) override {
        registers_.at(index) = value;
        ++sequence_;
    }
    cpu::SignalStateValue signal_state(size_t index) const override { return signals_.at(index); }

    uint64_t cycle_count() const override { return cycle_count_; }
    uint64_t sequence() const override { return sequence_; }
    bool is_paused() const override { return paused_; }
    void pause() override { paused_ = true; }
    void resume() override { paused_ = false; }
    void reset() override { registers_.assign(registers_.size(), 0); ++sequence_; }
    void step() override { ++cycle_count_; ++sequence_; }
    uint64_t step_instruction() override { cycle_count_ += 2; ++sequence_; return 2; }
    void prepare_for_step() override {}
    void wait_until_idle() override {}

    uint8_t read(uint16_t addr) override { return memory_[addr]; }
    uint8_t peek(uint16_t addr) const override { return memory_[addr]; }
    void write(uint16_t addr, uint8_t value) override { memory_[addr] = value; ++sequence_; }

    std::vector<MemoryRegionDescriptor> get_memory_regions() const override {
        MemoryRegionDescriptor ram;
        ram.name = "ram";
        ram.base_address = 0;
        ram.size = 0x10000;
        ram.flags = RegionFlags::Readable | RegionFlags::Writable;
        return {ram};
    }
    uint8_t peek_region(std::string_view, uint32_t address) const override { return memory_[address & 0xFFFF]; }
    uint8_t read_region(std::string_view, uint32_t address) override { return memory_[address & 0xFFFF]; }
    void write_region(std::string_view, uint32_t address, uint8_t value) override {
        memory_[address & 0xFFFF] = value;
    }
    std::string_view machine_type() const override { return "quux-machine"; }

    const std::vector<BreakpointEntry>& breakpoint_entries() const override { return breakpoints_; }
    void set_breakpoint_entries(std::vector<BreakpointEntry> entries) override {
        breakpoints_ = std::move(entries);
    }
    void set_breakpoint_hit_callback(BreakpointHitCallback) override {}

    const std::vector<WatchpointEntry>& watchpoint_entries() const override { return watchpoints_; }
    void set_watchpoint_entries(std::vector<WatchpointEntry> entries) override {
        watchpoints_ = std::move(entries);
    }
    void set_watchpoint_hit_callback(WatchpointHitCallback) override {}

    // Test hooks to drive signal state directly.
    void set_signal(size_t index, cpu::SignalStateValue s) { signals_.at(index) = s; }

private:
    cpu::CpuDescriptor descriptor_;
    std::vector<uint64_t> registers_;
    std::array<cpu::SignalStateValue, 2> signals_{};
    std::array<uint8_t, 0x10000> memory_{};
    std::vector<BreakpointEntry> breakpoints_;
    std::vector<WatchpointEntry> watchpoints_;
    uint64_t cycle_count_ = 0;
    uint64_t sequence_ = 0;
    bool paused_ = true;
};

uint64_t reg_of(const CpuState& s, const std::string& name) {
    for (const auto& rv : s.registers()) {
        if (rv.name() == name) return rv.value();
    }
    FAIL("register not present: " << name);
    return 0;
}

}  // namespace

TEST_CASE("A made-up CPU family is fully described through the interface", "[debugger][coprocessor]") {
    StubCpuDebugTarget stub;
    service::DebuggerControlServiceImpl impl(stub);

    ::beebium::CpuDescriptor desc;
    REQUIRE(impl.GetCpuDescriptor(nullptr, nullptr, &desc).ok());

    CHECK(desc.family() == "quux-9000");
    CHECK(desc.address_bits() == 16);
    CHECK(desc.little_endian() == false);
    REQUIRE(desc.registers_size() == 5);
    CHECK(desc.registers(0).name() == "D0");
    CHECK(desc.registers(2).name() == "IP");
    CHECK(desc.registers(2).role() == ::beebium::PROGRAM_COUNTER);
    CHECK(desc.registers(3).role() == ::beebium::STACK_POINTER);
    CHECK(desc.registers(4).role() == ::beebium::FLAGS);
    REQUIRE(desc.registers(4).flag_names_size() == 4);
    CHECK(desc.registers(4).flag_names(0) == "CARRY");
    CHECK(desc.registers(4).flag_names(3) == "HALT");
    REQUIRE(desc.signals_size() == 2);
    CHECK(desc.signals(0) == "INT");
    CHECK(desc.signals(1) == "TRAP");
}

TEST_CASE("A made-up CPU family round-trips its state through the interface", "[debugger][coprocessor]") {
    StubCpuDebugTarget stub;
    cpu::SignalStateValue trap;
    trap.asserted = true;
    trap.in_handler = true;
    stub.set_signal(1, trap);
    service::DebuggerControlServiceImpl impl(stub);

    // Set a subset of registers by name.
    {
        CpuState req;
        auto* d1 = req.add_registers();
        d1->set_name("D1");
        d1->set_value(0xBEEF);
        auto* ip = req.add_registers();
        ip->set_name("IP");
        ip->set_value(0x123456);
        CpuState resp;
        REQUIRE(impl.SetCpuState(nullptr, &req, &resp).ok());
        CHECK(reg_of(resp, "D1") == 0xBEEF);
        CHECK(reg_of(resp, "IP") == 0x123456);
        CHECK(reg_of(resp, "D0") == 0);  // untouched
    }

    // Read the full state back and see the signals too.
    {
        CpuState resp;
        REQUIRE(impl.GetCpuState(nullptr, nullptr, &resp).ok());
        CHECK(reg_of(resp, "IP") == 0x123456);
        REQUIRE(resp.signals_size() == 2);
        CHECK(resp.signals(0).name() == "INT");
        CHECK(resp.signals(1).name() == "TRAP");
        CHECK(resp.signals(1).asserted());
        CHECK(resp.signals(1).in_handler());
    }

    // An unknown register name is rejected, naming the offender.
    {
        CpuState req;
        auto* bad = req.add_registers();
        bad->set_name("A");  // a 6502 name this CPU does not have
        bad->set_value(1);
        CpuState resp;
        auto status = impl.SetCpuState(nullptr, &req, &resp);
        CHECK_FALSE(status.ok());
        CHECK(status.error_message().find("A") != std::string::npos);
    }
}
