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

// Breakpoint and watchpoint conditions name the machine's own latches, and a
// watchpoint's the access it judges (#186): each machine's DebugVariables,
// compiled and evaluated by the debugger service. Code runs from RAM, or from
// an all-NOP MOS image, so no ROM files are needed.

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/service/DebuggerService.hpp>
#include <beebium/service/HostDebugTarget.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace beebium;
using service::DebuggerControlServiceImpl;
using ::HostDebugTarget;

namespace {

constexpr uint8_t NOP = 0xEA;

// A machine running from an all-NOP MOS (patched by `mos_patches`), with the
// debugger service over it.
template <typename Machine>
struct Rig {
    Machine machine;
    HostDebugTarget<Machine> target{machine};
    std::unique_ptr<DebuggerControlServiceImpl> debugger;

    explicit Rig(const std::map<uint16_t, uint8_t>& mos_patches = {}) {
        std::vector<uint8_t> mos(0x4000, NOP);
        for (const auto& [addr, byte] : mos_patches) mos[addr - 0xC000] = byte;
        machine.memory().load_mos(mos.data(), mos.size());
        machine.reset();
        machine.step_instruction();
        debugger = std::make_unique<DebuggerControlServiceImpl>(target);
    }

    void plant(uint16_t addr, const std::vector<uint8_t>& code) {
        for (size_t i = 0; i < code.size(); ++i) machine.write(static_cast<uint16_t>(addr + i), code[i]);
    }

    grpc::Status breakpoint(uint32_t start, uint32_t end, const std::string& condition,
                            std::string* error = nullptr) {
        grpc::ServerContext context;
        AddBreakpointRequest request;
        request.set_start_address(start);
        request.set_end_address(end);
        request.set_condition(condition);
        AddBreakpointResponse response;
        auto status = debugger->AddBreakpoint(&context, &request, &response);
        if (error) *error = status.error_message();
        return status;
    }

    grpc::Status watchpoint(uint16_t start, uint16_t end, const std::string& condition,
                            std::string* error = nullptr) {
        grpc::ServerContext context;
        AddWatchpointRequest request;
        request.set_start_address(start);
        request.set_end_address(end);
        request.set_type(WATCHPOINT_BOTH);
        request.set_condition(condition);
        AddWatchpointResponse response;
        auto status = debugger->AddWatchpoint(&context, &request, &response);
        if (error) *error = status.error_message();
        return status;
    }

    // Run from `pc` until a breakpoint or watchpoint stops the machine (or the
    // budget runs out); the PC it stopped at.
    uint16_t run_from(uint16_t pc, uint64_t cycles = 2000) {
        machine.set_pc(pc);
        machine.resume();
        machine.run(cycles);
        REQUIRE(machine.is_paused());
        return machine.pc();
    }

    std::map<std::string, uint64_t> variables() const {
        std::map<std::string, uint64_t> values;
        for (const auto& variable : machine.memory().debug_variables()) {
            values[std::string(variable.name)] = variable.read();
        }
        return values;
    }
};

}  // namespace

#define ALL_MODELS ModelB, ModelBRomRamBoard, ModelBAtplSidewise, ModelBWatfordRomRam, \
                   ModelBIntegraB, ModelBPlus, ModelBPlus128K

TEMPLATE_TEST_CASE("bank reads the ROMSEL latch, not the MOS's copy of it",
                   "[debugger][conditions]", ALL_MODELS) {
    Rig<TestType> rig;
    rig.plant(0x0400, {
        0xA9, 0x05,        // LDA #5
        0x85, 0xF4,        // STA &F4     the MOS's copy only
        NOP,               // &0404
        0x8D, 0x30, 0xFE,  // STA &FE30   the latch
        NOP,               // &0408
        0x4C, 0x09, 0x04,  // JMP &0409
    });
    REQUIRE(rig.breakpoint(0x0404, 0x040C, "bank == 5 && (romsel & 0x0F) == 5").ok());
    CHECK(rig.run_from(0x0400) == 0x0408);
}

TEMPLATE_TEST_CASE("Conditions refuse a name the machine does not have, naming it",
                   "[debugger][conditions]", ALL_MODELS) {
    Rig<TestType> rig;
    const std::string machine(rig.machine.memory().machine_type());
    std::string error;
    CHECK(rig.breakpoint(0x0400, 0, "nosuch == 1", &error).error_code() ==
          grpc::StatusCode::INVALID_ARGUMENT);
    CHECK(error.rfind("no variable 'nosuch' on " + machine + "; available: bank, romsel", 0) == 0);
    CHECK(rig.breakpoint(0x0400, 0, "value == 0x42", &error).error_code() ==
          grpc::StatusCode::INVALID_ARGUMENT);
    CHECK(error == "no variable 'value' in a breakpoint condition on " + machine +
                   ": it is for watchpoint conditions only");
    CHECK(rig.watchpoint(0x2000, 0x2001, "acccon_typo", &error).error_code() ==
          grpc::StatusCode::INVALID_ARGUMENT);
    CHECK(error.rfind("no variable 'acccon_typo' on " + machine, 0) == 0);
}

TEMPLATE_TEST_CASE("GetCpuDescriptor lists exactly the names conditions accept",
                   "[debugger][conditions]", ALL_MODELS) {
    Rig<TestType> rig;
    grpc::ServerContext context;
    Empty request;
    CpuDescriptor descriptor;
    REQUIRE(rig.debugger->GetCpuDescriptor(&context, &request, &descriptor).ok());
    REQUIRE(descriptor.condition_variables_size() > 0);

    bool has_shadow_access = false;
    for (const auto& variable : descriptor.condition_variables()) {
        CAPTURE(variable.name());
        CHECK_FALSE(variable.description().empty());
        const std::string use = "(" + variable.name() + ") == 0 || 1";
        CHECK(rig.watchpoint(0x2000, 0x2001, use).ok());
        if (variable.scope() == CONDITION_VARIABLE_WATCHPOINT) {
            CHECK(rig.breakpoint(0x0400, 0, use).error_code() == grpc::StatusCode::INVALID_ARGUMENT);
        } else {
            CHECK(rig.breakpoint(0x0400, 0, use).ok());
        }
        has_shadow_access |= variable.name() == "shadow_access";
    }
    const bool has_shadow_ram = [&] {
        for (const auto& region : rig.machine.memory().get_memory_regions())
            if (region.name == "shadow_ram") return true;
        return false;
    }();
    CHECK(has_shadow_access == has_shadow_ram);
    CHECK(rig.watchpoint(0x2000, 0x2001, "made_up_name").error_code() ==
          grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_CASE("A watchpoint condition sees the access: value and write", "[debugger][conditions]") {
    Rig<ModelB> rig;
    rig.plant(0x0400, {
        0xA9, 0x01, 0x8D, 0x00, 0x20,  // LDA #1    STA &2000
        0xA9, 0x42, 0x8D, 0x00, 0x20,  // LDA #&42  STA &2000   (&0405)
        NOP,                           // &040A
        0x4C, 0x0B, 0x04,              // JMP &040B
    });
    REQUIRE(rig.watchpoint(0x2000, 0x2001, "write && value == 0x42 && addr == 0x2000").ok());
    CHECK(rig.run_from(0x0400) == 0x040A);  // after the second store, not the first

    Rig<ModelB> reads;
    reads.plant(0x0400, {
        0xA9, 0x07, 0x8D, 0x00, 0x20,  // LDA #7  STA &2000
        0xAD, 0x00, 0x20,              // LDA &2000          (&0405)
        NOP,                           // &0408
        0x4C, 0x09, 0x04,
    });
    REQUIRE(reads.watchpoint(0x2000, 0x2001, "!write && value == 7").ok());
    CHECK(reads.run_from(0x0400) == 0x0408);  // the read, not the write before it
}

TEMPLATE_TEST_CASE("On a B+, vdusel and andy follow ACCCON and ROMSEL bit 7",
                   "[debugger][conditions][bplus]", ModelBPlus, ModelBPlus128K) {
    Rig<TestType> rig;
    rig.plant(0x0400, {
        0xA9, 0x80, 0x8D, 0x34, 0xFE,  // LDA #&80  STA &FE34
        NOP,                           // &0405
        0x8D, 0x30, 0xFE,              // STA &FE30
        NOP,                           // &0409
        0x4C, 0x0A, 0x04,
    });
    REQUIRE(rig.breakpoint(0x0400, 0x0410, "vdusel && !andy").ok());
    CHECK(rig.run_from(0x0400) == 0x0405);
    auto values = rig.variables();
    CHECK(values["acccon"] == 0x80);
    CHECK(values["vdusel"] == 1);
    CHECK(values["andy"] == 0);
    rig.machine.step_instruction();
    rig.machine.step_instruction();  // the STA &FE30
    values = rig.variables();
    CHECK(values["andy"] == 1);
    CHECK(values["romsel"] == 0x80);
    CHECK(values["bank"] == 0);
}

TEMPLATE_TEST_CASE("On a B+, vdu is the flag the instruction at a breakpoint was fetched under",
                   "[debugger][conditions][bplus]", ModelBPlus, ModelBPlus128K) {
    Rig<TestType> rig;  // all NOPs from &C000
    rig.machine.write(0xFE34, 0x80);
    REQUIRE(rig.breakpoint(0xDFF0, 0xE000, "vdu && vdusel").ok());
    CHECK(rig.run_from(0xDFF0) == 0xDFF0);  // VDU driver code

    Rig<TestType> past;
    past.machine.write(0xFE34, 0x80);
    REQUIRE(past.breakpoint(0xE000, 0xE010, "vdu").ok());
    REQUIRE(past.breakpoint(0xE010, 0, "").ok());
    CHECK(past.run_from(0xDFF8) == 0xE010);  // never fired past &E000
}

TEMPLATE_TEST_CASE("On a B+, shadow_access is set by VDU code writing the shadow screen",
                   "[debugger][conditions][bplus]", ModelBPlus, ModelBPlus128K) {
    // STA &3000 in the MOS's VDU drivers at &D000, and the same from RAM.
    Rig<TestType> rig({{0xD000, 0x8D}, {0xD001, 0x00}, {0xD002, 0x30}});
    rig.machine.write(0xFE34, 0x80);
    rig.plant(0x0400, {0x8D, 0x00, 0x30, NOP, 0x4C, 0x04, 0x04});
    REQUIRE(rig.watchpoint(0x3000, 0x3001, "write && shadow_access").ok());
    REQUIRE(rig.breakpoint(0x0404, 0, "").ok());
    CHECK(rig.run_from(0x0400) == 0x0404);  // from RAM: main RAM, no hit
    CHECK(rig.run_from(0xD000) == 0xD003);  // from VDU code: shadow RAM
}

TEST_CASE("On an Integra-B, shadow_access follows SHEN and MEMSEL, and each bit has its name",
          "[debugger][conditions][integra_b]") {
    Rig<ModelBIntegraB> rig;
    rig.plant(0x0400, {0x8D, 0x00, 0x30, NOP, 0x4C, 0x04, 0x04});  // STA &3000
    REQUIRE(rig.watchpoint(0x3000, 0x3001, "write && shadow_access && shen && !memsel").ok());
    REQUIRE(rig.breakpoint(0x0404, 0, "").ok());

    rig.machine.write(0xFE34, 0x80);  // SHEN
    rig.machine.write(0xFE30, 0x80);  // MEMSEL: the CPU sees main memory
    CHECK(rig.run_from(0x0400) == 0x0404);
    rig.machine.write(0xFE30, 0x00);
    CHECK(rig.run_from(0x0400) == 0x0403);

    rig.machine.write(0xFE34, 0x70);  // PRVS1, PRVS4, PRVS8
    rig.machine.write(0xFE30, 0x46);  // PRVEN, bank 6
    auto values = rig.variables();
    CHECK(values["ramsel"] == 0x70);
    CHECK(values["shen"] == 0);
    CHECK(values["prvs1"] == 1);
    CHECK(values["prvs4"] == 1);
    CHECK(values["prvs8"] == 1);
    CHECK(values["prven"] == 1);
    CHECK(values["memsel"] == 0);
    CHECK(values["bank"] == 6);
    rig.machine.write(0xFE34, 0x80);
    rig.machine.write(0xFE30, 0x80);
    values = rig.variables();
    CHECK(values["shen"] == 1);
    CHECK(values["prvs1"] + values["prvs4"] + values["prvs8"] + values["prven"] == 0);
    CHECK(values["memsel"] == 1);
}

// ---------------------------------------------------------------------------
// Beam variables (#196): the 6845's position and the field, on every model.
// ---------------------------------------------------------------------------

namespace {

// Program the 6845 and the Video ULA as MOS 1.20's MODE 7 does: 64 1 MHz
// characters a scanline (128 CPU cycles), 25 displayed rows of 31, vsync at
// row 27, interlaced sync and video.
template <typename Machine>
void program_mode_7(Machine& machine) {
    constexpr uint8_t MODE_7[] = {63, 40, 51, 0x24, 30, 2, 25, 27, 0x93, 18, 0x72, 0x13, 0x28, 0x00};
    for (uint8_t r = 0; r < sizeof(MODE_7); ++r) {
        machine.write(0xFE00, r);
        machine.write(0xFE01, MODE_7[r]);
    }
    machine.write(0xFE20, 0x4B);  // Video ULA: teletext
}

constexpr uint64_t SCANLINE_CYCLES = 128;

template <typename Machine>
uint64_t variable(Machine& machine, std::string_view name) {
    for (const auto& v : machine.debug_variables()) {
        if (v.name == name) return v.read();
    }
    FAIL("no variable " << name);
    return 0;
}

}  // namespace

TEST_CASE("field counts vsyncs, about 50 an emulated second in MODE 7", "[debugger][conditions][beam]") {
    Rig<ModelB> rig;
    program_mode_7(rig.machine);
    rig.plant(0x0400, {0x4C, 0x00, 0x04});  // JMP &0400
    rig.machine.set_pc(0x0400);
    rig.machine.run(200'000);  // settle into the programmed timing
    const uint64_t before = variable(rig.machine, "field");
    rig.machine.run(2'000'000);  // one emulated second
    const uint64_t fields = variable(rig.machine, "field") - before;
    CHECK(fields >= 49);
    CHECK(fields <= 51);
}

TEST_CASE("A breakpoint on the next field stops as its vsync begins", "[debugger][conditions][beam]") {
    Rig<ModelB> rig;
    program_mode_7(rig.machine);
    rig.plant(0x0400, {0x4C, 0x00, 0x04});
    rig.machine.set_pc(0x0400);
    rig.machine.run(200'000);
    const uint64_t field = variable(rig.machine, "field");

    // A whole-address-space breakpoint, evaluated at every instruction boundary.
    REQUIRE(rig.breakpoint(0x0000, 0x10000, "field == " + std::to_string(field + 1)).ok());
    rig.machine.resume();
    rig.machine.run(100'000);
    REQUIRE(rig.machine.is_paused());
    CHECK(variable(rig.machine, "field") == field + 1);
    CHECK(variable(rig.machine, "in_vsync") == 1);
    CHECK(variable(rig.machine, "cycles_since_vsync") <= 4);  // the first boundary after it
}

TEST_CASE("crtc_row and display_enabled are the beam's at the access a watchpoint judges",
          "[debugger][conditions][beam]") {
    // STA &2000 in a loop: a write every 7 cycles.
    auto writing = [](const std::string& condition) {
        auto rig = std::make_unique<Rig<ModelB>>();
        program_mode_7(rig->machine);
        rig->plant(0x0400, {0x8D, 0x00, 0x20, 0x4C, 0x00, 0x04});
        rig->machine.set_pc(0x0400);
        rig->machine.run(200'000);
        REQUIRE(rig->watchpoint(0x2000, 0x2001, condition).ok());
        rig->machine.resume();
        rig->machine.run(100'000);
        REQUIRE(rig->machine.is_paused());
        return rig;
    };

    // Row 10 begins after the four rows from vsync (27-30), the two adjust
    // scanlines and rows 0-9: 46 to 50 rows-worth of 9 or 10 scanlines each.
    {
        auto rig = writing("crtc_row == 10");
        const uint64_t since_vsync = variable(rig->machine, "cycles_since_vsync");
        CHECK(since_vsync >= (4 * 9 + 2 + 10 * 9) * SCANLINE_CYCLES);
        CHECK(since_vsync <= (4 * 10 + 2 + 10 * 10 + 1) * SCANLINE_CYCLES);
        CHECK(variable(rig->machine, "crtc_row") == 10);
        CHECK(variable(rig->machine, "display_enabled") == 1);
    }
    // The vertical blank: rows 25-30 are not displayed.
    {
        auto rig = writing("!display_enabled && crtc_column < 40");
        const uint64_t row = variable(rig->machine, "crtc_row");
        CHECK(row >= 25);
        CHECK(row <= 30);
    }
}
