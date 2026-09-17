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

// Exercises the server-facing coprocessor wiring without the 65C02, through
// the abstract interfaces: a stub CoprocessorExtension installs its coprocessor
// and backend into the TubeSocket; a debug target of an unserved CPU family is
// rejected by the same dynamic_cast the server uses (so the server would carry
// on without a debugger); and the server's cross-processor stop wiring pauses
// each processor when the other hits a stop_counterpart breakpoint.

#include <catch2/catch_test_macros.hpp>

#include <beebium/extension/CoprocessorExtension.hpp>
#include <beebium/extension/CpuDebugTarget.hpp>
#include <beebium/service/DebuggerService.hpp>
#include <beebium/tube/Coprocessor.hpp>
#include <beebium/tube/CoprocessorRunner.hpp>
#include <beebium/tube/TubeHostBackend.hpp>
#include <beebium/tube/TubeSocket.hpp>
#include <beebium/tube/TubeUla.hpp>

#include "debugger.pb.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

using namespace beebium;

namespace {

std::array<uint8_t, 4096> make_nop_rom(uint16_t entry = 0xF800) {
    std::array<uint8_t, 4096> rom{};
    rom.fill(0xEA);  // NOP
    rom[0xFFC] = static_cast<uint8_t>(entry & 0xFF);
    rom[0xFFD] = static_cast<uint8_t>(entry >> 8);
    rom[0xFFE] = 0x00; rom[0xFFF] = 0xF9; rom[0x900] = 0x40;
    rom[0xFFA] = 0x80; rom[0xFFB] = 0xF9; rom[0x980] = 0x40;
    return rom;
}

// Minimal coprocessor and backend that just record that the socket reaches them.
class StubCoprocessor : public Coprocessor {
public:
    std::vector<uint64_t> run_until_args;
    void run_until(uint64_t host_cycle) override { run_until_args.push_back(host_cycle); }
    void pause() override {}
    void resume() override {}
    bool is_paused() const override { return false; }
    void reset() override {}
    BoardTiming board_timing() const override { return BoardTiming{ClockRatio{1, 1}, 1, 1, 0, 1}; }
};

class StubBackend : public TubeHostBackend {
public:
    uint8_t host_read(uint8_t) override { return 0xAB; }
    uint8_t host_peek(uint8_t) const override { return 0xAB; }
    void host_write(uint8_t, uint8_t) override {}
    bool hirq() const override { return false; }
    void reset() override {}
};

class StubCoprocessorExtension : public CoprocessorExtension {
public:
    StubCoprocessor cop;
    StubBackend backend;

    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"tube"};
        return deps;
    }
    std::span<const std::string_view> provides() const override { return {}; }
    void init(ExtensionContext&) override {}
    void shutdown() override {}

    Coprocessor* coprocessor() override { return &cop; }
    TubeHostBackend* tube_backend() override { return &backend; }
    // debug_target() defaults to nullptr: this stub offers no debugger.
};

}  // namespace

TEST_CASE("CoprocessorExtension: coprocessor and backend land in the socket", "[coprocessor][extension]") {
    TubeSocket socket;
    StubCoprocessorExtension ext;

    // The same install calls the extension/server make.
    socket.install_backend(ext.tube_backend());
    socket.install_coprocessor(ext.coprocessor());
    CHECK(socket.enabled());

    // Installing pins the coprocessor's time origin at the current host time
    // (0 here) with a run to that cycle, so its first recorded arg is 0. The
    // socket then drives it in host time.
    socket.run_coprocessor_until(1);
    socket.run_coprocessor_until(2);
    CHECK(ext.cop.run_until_args == std::vector<uint64_t>{0, 1, 2});

    // Host register access is delegated to the installed backend.
    CHECK(socket.read(0) == 0xAB);

    // This coprocessor offers no debugger.
    CHECK(ext.debug_target() == nullptr);
}

TEST_CASE("CoprocessorExtension: a coprocessor describes its own CPU through the interface",
          "[coprocessor][extension]") {
    // The server no longer casts to a family: it reads the description off the
    // abstract CpuDebugTarget. A 6502 coprocessor names its family and lists its
    // registers in display order.
    TubeUla ula;
    auto rom = make_nop_rom();
    CoprocessorRunner runner(ula, rom);
    CpuDebugTarget* target = &runner;
    CHECK(target->cpu_descriptor().family == "6502");
    REQUIRE(target->cpu_descriptor().registers.size() == 6);
    CHECK(target->cpu_descriptor().registers[0].name == "A");
    CHECK(target->cpu_descriptor().signals.size() == 2);
}

TEST_CASE("CoprocessorExtension: cross-processor stop is wired both ways server-side",
          "[coprocessor][extension]") {
    // Two coprocessors standing in for host and coprocessor; the wiring is
    // CPU-agnostic. The server wires each debugger impl's counterpart callback
    // to pause the other processor.
    TubeUla host_ula, cop_ula;
    auto rom = make_nop_rom();
    CoprocessorRunner host(host_ula, rom);
    CoprocessorRunner cop(cop_ula, rom);
    host.reset();
    cop.reset();

    service::DebuggerControlServiceImpl host_impl(host);
    service::DebuggerControlServiceImpl cop_impl(cop);

    host_impl.set_counterpart_stop_callback([&] { cop.pause(); });
    cop_impl.set_counterpart_stop_callback([&] { host.pause(); });

    auto add_stop_counterpart_bp = [](auto& impl, uint16_t addr) {
        AddBreakpointRequest req;
        req.set_start_address(addr);
        req.set_stop_counterpart(true);
        AddBreakpointResponse resp;
        REQUIRE(impl.AddBreakpoint(nullptr, &req, &resp).ok());
    };

    // Drive the runner's own step() rather than the service's StepCycle: the
    // breakpoint-hit callback the impl registered locks the service mutex, and
    // a service method must not hold that lock while stepping or the hit
    // deadlocks. The impl still owns the counterpart wiring, the breakpoint
    // entries and the hit callback; we just supply the cycles.
    auto step_until = [](CoprocessorRunner& runner, auto&& predicate) {
        for (int i = 0; i < 40 && !predicate(); ++i) {
            runner.step();
        }
    };

    SECTION("host breakpoint with stop_counterpart pauses the coprocessor") {
        add_stop_counterpart_bp(host_impl, 0xF800);
        REQUIRE_FALSE(cop.is_paused());
        step_until(host, [&] { return cop.is_paused(); });
        CHECK(cop.is_paused());
    }

    SECTION("coprocessor breakpoint with stop_counterpart pauses the host") {
        add_stop_counterpart_bp(cop_impl, 0xF800);
        REQUIRE_FALSE(host.is_paused());
        step_until(cop, [&] { return host.is_paused(); });
        CHECK(host.is_paused());
    }
}
