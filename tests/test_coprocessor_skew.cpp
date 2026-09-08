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

// Tests the host/coprocessor skew contract (docs/tube-coprocessor-contract.md,
// Step 2): every host Tube register access is exact (coprocessor_time() == host
// time), and at every run_coprocessor_until the interval since the last call is
// at most TubeSocket::MAX_COPROCESSOR_SKEW. Asserted across a full 65C02 boot
// and the CE2023 load. The assertions test the BOUND, not the value the current
// zero-skew strategy happens to achieve, so they keep passing when Step 3 raises
// the call interval.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/disc/DiscLoader.hpp>
#include <beebium/tube/Coprocessor.hpp>
#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeSocket.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>

#include "test_econet_helpers.hpp"

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

using namespace beebium;
using namespace beebium::test;

namespace {

constexpr const char* TUBE_ROM_FILENAME = "acorn-tube-6502_1_10.rom";
constexpr const char* DNFS_ROM_FILENAME = "acorn-dnfs_3_02.rom";
constexpr const char* DFS_ROM_FILENAME = "acorn-dfs_2_26.rom";
constexpr const char* CE2023_DISC_FILENAME = "chuckieEgg2023.ssd";
constexpr size_t TUBE_ROM_SIZE = 2048;

std::array<uint8_t, TUBE_ROM_SIZE> load_tube_rom() {
    auto filepath = std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / TUBE_ROM_FILENAME;
    std::ifstream file(filepath, std::ios::binary);
    REQUIRE(file.good());
    std::array<uint8_t, TUBE_ROM_SIZE> rom{};
    file.read(reinterpret_cast<char*>(rom.data()), TUBE_ROM_SIZE);
    REQUIRE(file.gcount() == static_cast<std::streamsize>(TUBE_ROM_SIZE));
    return rom;
}

bool tube_rom_available() {
    return std::filesystem::exists(
        std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / TUBE_ROM_FILENAME);
}

// A Coprocessor decorator that observes the skew contract. It wraps the real
// coprocessor to see every run_coprocessor_until (contract 2, the interval
// bound), and installs the socket's register-access hook to see every host
// register access (contract 1, exactness). It accumulates counts rather than
// asserting inline so the tests can assert either zero violations (the boot and
// CE2023 runs) or a positive count (the deliberate-violation run).
class SkewObserver : public Coprocessor {
public:
    SkewObserver(Coprocessor& inner, TubeSocket& socket)
        : inner_(inner), socket_(socket) {
        socket_.set_register_access_observer(
            [this](uint64_t host_time, uint8_t offset, bool is_write) {
                on_access(host_time, offset, is_write);
            });
    }

    // Coprocessor -- delegate everything, observing run_until intervals.
    void run_until(uint64_t host_cycle) override {
        if (have_prev_) {
            const uint64_t interval = host_cycle - prev_c_;
            if (interval > max_interval_) max_interval_ = interval;
            if (interval > TubeSocket::MAX_COPROCESSOR_SKEW) ++interval_violations_;
        }
        prev_c_ = host_cycle;
        have_prev_ = true;
        ++run_count_;
        inner_.run_until(host_cycle);
    }
    void pause() override { inner_.pause(); }
    void resume() override { inner_.resume(); }
    bool is_paused() const override { return inner_.is_paused(); }
    void reset() override { have_prev_ = false; inner_.reset(); }
    ClockRatio clock_ratio() const override { return inner_.clock_ratio(); }

    uint64_t access_count() const { return access_count_; }
    uint64_t access_exactness_violations() const { return access_exactness_violations_; }
    uint64_t run_count() const { return run_count_; }
    uint64_t interval_violations() const { return interval_violations_; }
    uint64_t max_interval() const { return max_interval_; }

private:
    void on_access(uint64_t host_time, uint8_t, bool) {
        ++access_count_;
        // Contract 1: at a register access the coprocessor has been run to
        // exactly this host time.
        if (socket_.coprocessor_time() != host_time) ++access_exactness_violations_;
    }

    Coprocessor& inner_;
    TubeSocket& socket_;
    bool have_prev_ = false;
    uint64_t prev_c_ = 0;
    uint64_t max_interval_ = 0;
    uint64_t run_count_ = 0;
    uint64_t interval_violations_ = 0;
    uint64_t access_count_ = 0;
    uint64_t access_exactness_violations_ = 0;
};

void setup_tube_machine(ModelB& machine) {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    auto dnfs = load_rom(rom_dirpath / DNFS_ROM_FILENAME);
    machine.memory().load_sideways_rom(13, dnfs.data(), dnfs.size());
}

// A minimal coprocessor for the socket-level tests that need no CPU.
class StubCoprocessor : public Coprocessor {
public:
    void run_until(uint64_t) override {}
    void pause() override {}
    void resume() override {}
    bool is_paused() const override { return false; }
    void reset() override {}
    ClockRatio clock_ratio() const override { return ClockRatio{3, 2}; }
};

// A stub coprocessor whose only behaviour is a callback on each run_until,
// letting a socket-level test poke or observe the ULA at coprocessor-run time.
class ActionStub : public Coprocessor {
public:
    std::function<void(uint64_t host_cycle)> on_run;
    void run_until(uint64_t host_cycle) override { if (on_run) on_run(host_cycle); }
    void pause() override {}
    void resume() override {}
    bool is_paused() const override { return false; }
    void reset() override {}
    ClockRatio clock_ratio() const override { return ClockRatio{1, 1}; }
};

// A coprocessor that counts the cycles it is actually driven for, at a fixed
// clock ratio, using the same CoprocessorClock the real runners use. It needs
// no CPU or ROM, so it isolates the socket's time-base behaviour.
class CycleCountingStub : public Coprocessor {
public:
    explicit CycleCountingStub(ClockRatio ratio) : clock_(ratio) {}
    void run_until(uint64_t host_cycle) override { cycles_ += clock_.cycles_due(host_cycle); }
    void pause() override {}
    void resume() override {}
    bool is_paused() const override { return false; }
    void reset() override { clock_.rebase(); cycles_ = 0; }
    ClockRatio clock_ratio() const override { return clock_.ratio(); }
    uint64_t cycles() const { return cycles_; }
private:
    CoprocessorClock clock_;
    uint64_t cycles_ = 0;
};

}  // namespace

TEST_CASE("Skew: exact accesses and bounded interval across a 65C02 boot",
          "[tube][skew]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!tube_rom_available()) SKIP("Tube 6502 ROM not available");
    if (!std::filesystem::exists(std::filesystem::path(BEEBIUM_ROM_DIR) / DNFS_ROM_FILENAME))
        SKIP("DNFS ROM not available");

    ModelB machine;
    setup_tube_machine(machine);
    machine.state().memory.tube_socket.enable();
    machine.reset();

    TubeUla* tube = machine.state().memory.tube_socket.tube_ula();
    REQUIRE(tube != nullptr);
    auto tube_rom = load_tube_rom();
    ParasiteRunner runner(*tube, tube_rom);
    runner.reset();

    SkewObserver obs(runner, machine.state().memory.tube_socket);
    machine.state().memory.tube_socket.install_coprocessor(&obs);

    machine.run(30'000'000);

    // It really booted (so the Tube traffic the contract governs actually
    // happened), and the contract held throughout.
    CHECK(screen_contains(machine, "Acorn TUBE 6502 64K"));
    CHECK(obs.access_count() > 0);
    CHECK(obs.access_exactness_violations() == 0);   // contract 1
    CHECK(obs.interval_violations() == 0);            // contract 2
    // The strategy actually batches: idle stretches during boot let the
    // coprocessor fall a full Delta behind before it is run, so this equals
    // the bound. Asserting equality means the optimisation cannot be silently
    // lost (e.g. by a stray per-cycle sync).
    CHECK(obs.max_interval() == TubeSocket::MAX_COPROCESSOR_SKEW);
}

TEST_CASE("Skew: exact accesses and bounded interval across the CE2023 load",
          "[tube][skew][ce2023]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!tube_rom_available()) SKIP("Tube 6502 ROM not available");
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    const auto disc_filepath =
        std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) / "discs" / CE2023_DISC_FILENAME;
    if (!std::filesystem::exists(rom_dirpath / DFS_ROM_FILENAME)
        || !std::filesystem::exists(disc_filepath)) {
        SKIP("DFS ROM or CE2023 disc image not available");
    }

    ModelB machine;
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    auto dfs = load_rom(rom_dirpath / DFS_ROM_FILENAME);
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.memory().load_sideways_rom(14, dfs.data(), dfs.size());
    machine.memory().install_acorn_1770();

    auto disc_result = load_disc_from_url_or_filepath(disc_filepath.string());
    REQUIRE(disc_result.success());
    machine.memory().disc_drive_0.insert(std::move(disc_result.disc));

    machine.state().memory.tube_socket.enable();
    machine.memory().set_auto_boot(true);
    machine.reset();

    TubeUla* tube = machine.state().memory.tube_socket.tube_ula();
    REQUIRE(tube != nullptr);
    auto tube_rom = load_tube_rom();
    ParasiteRunner runner(*tube, tube_rom);
    runner.reset();

    SkewObserver obs(runner, machine.state().memory.tube_socket);
    machine.state().memory.tube_socket.install_coprocessor(&obs);

    machine.run(60'000'000);

    // CE2023 drives heavy R1/R3/R4 traffic through the Tube; the contract holds
    // across it.
    CHECK(obs.access_count() > 0);
    CHECK(obs.access_exactness_violations() == 0);
    CHECK(obs.interval_violations() == 0);
    CHECK(obs.max_interval() <= TubeSocket::MAX_COPROCESSOR_SKEW);
}

TEST_CASE("Skew: pausing the host syncs the coprocessor to it", "[tube][skew]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!tube_rom_available()) SKIP("Tube 6502 ROM not available");
    if (!std::filesystem::exists(std::filesystem::path(BEEBIUM_ROM_DIR) / DNFS_ROM_FILENAME))
        SKIP("DNFS ROM not available");

    ModelB machine;
    setup_tube_machine(machine);
    machine.state().memory.tube_socket.enable();
    machine.reset();
    TubeUla* tube = machine.state().memory.tube_socket.tube_ula();
    REQUIRE(tube != nullptr);
    auto tube_rom = load_tube_rom();
    ParasiteRunner runner(*tube, tube_rom);
    runner.reset();
    machine.state().memory.tube_socket.install_coprocessor(&runner);

    machine.run(5'000'000);

    // Direct step()s (not run()) let the coprocessor batch behind by up to the
    // bound, then pause() must bring it back to the host cycle.
    for (int i = 0; i < 5; ++i) machine.step();
    machine.pause();
    CHECK(machine.state().memory.tube_socket.coprocessor_time() == machine.cycle_count());
}

TEST_CASE("Skew: single-stepping the host keeps the coprocessor within a cycle",
          "[tube][skew]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!tube_rom_available()) SKIP("Tube 6502 ROM not available");
    if (!std::filesystem::exists(std::filesystem::path(BEEBIUM_ROM_DIR) / DNFS_ROM_FILENAME))
        SKIP("DNFS ROM not available");

    ModelB machine;
    setup_tube_machine(machine);
    machine.state().memory.tube_socket.enable();
    machine.reset();
    TubeUla* tube = machine.state().memory.tube_socket.tube_ula();
    REQUIRE(tube != nullptr);
    auto tube_rom = load_tube_rom();
    ParasiteRunner runner(*tube, tube_rom);
    runner.reset();
    machine.state().memory.tube_socket.install_coprocessor(&runner);

    machine.run(5'000'000);

    // The debugger single-steps by step_instruction() then finish_step() (as
    // DebuggerControlServiceImpl does), which syncs the coprocessor, so it
    // never lags the single-stepping host by more than a cycle.
    for (int i = 0; i < 50; ++i) {
        machine.step_instruction();
        machine.finish_step();
        const uint64_t lag = machine.cycle_count()
                           - machine.state().memory.tube_socket.coprocessor_time();
        CHECK(lag <= 1);
    }
}

TEST_CASE("Skew: the observer catches a deliberate interval violation",
          "[tube][skew]") {
    // A stub strategy that runs the coprocessor only every 9 host cycles
    // exceeds the 8-cycle bound; the observer must notice.
    TubeSocket socket;
    StubCoprocessor stub;
    SkewObserver obs(stub, socket);
    socket.install_coprocessor(&obs);

    socket.run_coprocessor_until(0);
    socket.run_coprocessor_until(9);
    socket.run_coprocessor_until(18);

    CHECK(obs.interval_violations() > 0);
    CHECK(obs.max_interval() == 9);
}

TEST_CASE("Skew: coprocessor_time() is re-established after reset()",
          "[tube][skew]") {
    TubeSocket socket;
    StubCoprocessor stub;
    socket.install_coprocessor(&stub);

    socket.run_coprocessor_until(1000);
    CHECK(socket.coprocessor_time() == 1000);

    // A hard reset zeroes the host cycle count; the next run_coprocessor_until
    // re-establishes C at the new (smaller) host time.
    socket.reset();
    socket.run_coprocessor_until(3);
    CHECK(socket.coprocessor_time() == 3);
}

TEST_CASE("Skew: a coprocessor installed at an arbitrary host time runs exactly ratio x cycles",
          "[tube][skew]") {
    // Installing a coprocessor into an already-running machine must pin its
    // time origin at the install host time, not up to Delta cycles later when
    // batching would otherwise first run it. Over N host cycles from install it
    // then executes exactly ratio x N of its own cycles, with no startup phase
    // error -- the batching interval never changes the total.
    for (const ClockRatio ratio : {ClockRatio{3, 2}, ClockRatio{2, 1}}) {
        TubeSocket socket;
        CycleCountingStub stub(ratio);
        stub.reset();

        // The host runs to an arbitrary time T with no coprocessor installed.
        const uint64_t T = 12345;
        for (uint64_t h = 1; h <= T; ++h) socket.host_cycle(h);

        // Install at T. The origin is pinned here with zero cycles due.
        socket.install_coprocessor(&stub);
        CHECK(stub.cycles() == 0);

        // Run N host cycles the way Machine::step() drives the socket, then
        // flush to the final host time as a stop/step boundary would.
        const uint64_t N = 100000;
        for (uint64_t h = T + 1; h <= T + N; ++h) socket.host_cycle(h);
        socket.run_coprocessor_until(T + N);

        CHECK(stub.cycles() == ratio.numerator * N / ratio.denominator);
    }
}

TEST_CASE("Skew: a coprocessor HIRQ reaches the host's IRQ input within Delta",
          "[tube][skew]") {
    // In-process ULA; the host enables HIRQ (Q=1). The coprocessor then raises
    // HIRQ by writing R4 P2H, and the host (which polls hirq() every cycle)
    // sees it within Delta host cycles -- the batching latency.
    TubeSocket socket;
    socket.enable();
    TubeUla* ula = socket.tube_ula();
    REQUIRE(ula != nullptr);

    socket.host_cycle(0);
    socket.write(0, TubeUla::FLAG_S | TubeUla::FLAG_Q);  // Q=1
    REQUIRE_FALSE(socket.irq_pending());  // Q set, but no R4 P-to-H data yet

    constexpr uint64_t RAISE_AT = 1;
    ActionStub stub;
    bool raised = false;
    stub.on_run = [&](uint64_t h) {
        if (!raised && h >= RAISE_AT) { ula->parasite_write(7, 0x42); raised = true; }
    };
    socket.install_coprocessor(&stub);

    bool seen = false;
    uint64_t seen_at = 0;
    for (uint64_t h = 1; h <= RAISE_AT + 4 * TubeSocket::MAX_COPROCESSOR_SKEW; ++h) {
        socket.host_cycle(h);
        if (!seen && socket.irq_pending()) { seen = true; seen_at = h; }
    }
    REQUIRE(raised);
    REQUIRE(seen);
    CHECK(seen_at >= RAISE_AT);
    CHECK(seen_at - RAISE_AT <= TubeSocket::MAX_COPROCESSOR_SKEW);
}

TEST_CASE("Skew: a host PIRQ reaches the coprocessor within Delta",
          "[tube][skew]") {
    // In-process ULA; PIRQ from R1 enabled (I=1). A host write to R1 raises
    // PIRQ, and the coprocessor sees it on its next run_until, within Delta.
    TubeSocket socket;
    socket.enable();
    TubeUla* ula = socket.tube_ula();
    REQUIRE(ula != nullptr);

    socket.host_cycle(0);
    socket.write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);  // I=1

    ActionStub stub;
    bool pirq_seen = false;
    uint64_t pirq_seen_at = 0;
    stub.on_run = [&](uint64_t h) {
        if (!pirq_seen && ula->pirq()) { pirq_seen = true; pirq_seen_at = h; }
    };
    socket.install_coprocessor(&stub);

    constexpr uint64_t WRITE_AT = 3;
    for (uint64_t h = 1; h < WRITE_AT; ++h) socket.host_cycle(h);

    // The write syncs the coprocessor to WRITE_AT (before the write, so it does
    // not yet see PIRQ), then raises PIRQ.
    socket.host_cycle(WRITE_AT);
    socket.write(1, 0x55);  // R1 H-to-P data, with I=1 -> PIRQ
    REQUIRE(ula->pirq());
    REQUIRE_FALSE(pirq_seen);  // the pre-write sync ran the coprocessor before PIRQ

    for (uint64_t h = WRITE_AT + 1;
         h <= WRITE_AT + 4 * TubeSocket::MAX_COPROCESSOR_SKEW && !pirq_seen; ++h) {
        socket.host_cycle(h);
    }
    REQUIRE(pirq_seen);
    CHECK(pirq_seen_at >= WRITE_AT);
    CHECK(pirq_seen_at - WRITE_AT <= TubeSocket::MAX_COPROCESSOR_SKEW);
}
