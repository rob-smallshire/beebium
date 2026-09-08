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

// test_econet_tx_with_tube.cpp
//
// Tests whether the Econet TX path completes when a Tube second processor
// is fitted. The server-side Econet bug manifests as nmi_tx_complete never
// running (CR1=&82 never written) after TX_LAST_DATA.
//
// Two test cases compare the same TX operation (triggered by *. or *NET)
// with and without a Tube to isolate whether the Tube is the cause.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/econet/TestBackend.hpp>
#include <beebium/econet/FourWayHandshake.hpp>
#include <beebium/econet/Mc6854.hpp>
#include <beebium/tube/CoprocessorRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "test_econet_helpers.hpp"
#include "test_keyboard_helpers.hpp"

using namespace beebium;
using namespace beebium::test;

namespace {

constexpr const char* ANFS_ROM_FILENAME = "acorn-anfs_4_18.rom";
constexpr const char* TUBE_ROM_FILENAME = "acorn-tube-6502_1_10.rom";
constexpr size_t TUBE_ROM_SIZE = 2048;

bool anfs_rom_available() {
    return std::filesystem::exists(
        std::filesystem::path(BEEBIUM_TEST_ROM_DIR) / ANFS_ROM_FILENAME);
}

bool tube_rom_available() {
    return std::filesystem::exists(
        std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / TUBE_ROM_FILENAME);
}

std::array<uint8_t, TUBE_ROM_SIZE> load_tube_rom() {
    auto filepath = std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / TUBE_ROM_FILENAME;
    std::ifstream file(filepath, std::ios::binary);
    std::array<uint8_t, TUBE_ROM_SIZE> rom{};
    file.read(reinterpret_cast<char*>(rom.data()), TUBE_ROM_SIZE);
    return rom;
}

// Step the machine while processing video output
void run_with_video(ModelB& machine, FrameRenderer& renderer, uint64_t cycles) {
    for (uint64_t i = 0; i < cycles; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }
}

struct TxTestResults {
    size_t frames_sent;
    uint32_t cr1_0x82_count;
    uint8_t final_cr1;
    bool nmi_enable_ff;
    uint64_t tick_count;
    std::string handshake_stage;
    std::string screen;
};

// Run the Econet TX test: boot, type *NET, type *., check results
TxTestResults run_econet_tx_test(ModelB& machine, TestBackend* backend,
                                 uint64_t boot_cycles, uint64_t tx_cycles) {
    machine.memory().enable_video_output();
    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot
    machine.reset();
    run_with_video(machine, renderer, boot_cycles);

    bool booted = screen_contains(machine, "BBC Computer") ||
                  screen_contains(machine, "Acorn TUBE");
    REQUIRE(booted);

    // Type *NET to select NFS as the filing system
    type_string_with_shift(machine, renderer, "*NET\r", 100000);
    run_with_video(machine, renderer, 2'000'000);

    // Record state before TX
    size_t frames_before = backend->sent_frame_count();
    uint32_t cr1_82_before = machine.memory().econet_socket.cr1_0x82_write_count();

    // Type *. to trigger network TX (catalogue request to file server)
    type_string_with_shift(machine, renderer, "*.\r", 100000);

    // Run for enough cycles for the TX sequence to complete.
    // The NFS ROM polls INACTIVE, configures ADLC, runs NMI-driven TX,
    // waits for scout ack (5000 ticks), sends data, waits for final ack.
    // 5M cycles gives plenty of time for multiple retry attempts.
    run_with_video(machine, renderer, tx_cycles);

    TxTestResults r{};
    r.frames_sent = backend->sent_frame_count() - frames_before;
    r.cr1_0x82_count = machine.memory().econet_socket.cr1_0x82_write_count() - cr1_82_before;
    const auto* adlc = machine.memory().econet_socket.adlc();
    r.final_cr1 = adlc ? adlc->cr1() : 0;
    r.nmi_enable_ff = machine.memory().econet_socket.nmi_enable_ff();
    r.tick_count = machine.memory().econet_socket.tick_count();
    const auto* hs = machine.memory().econet_socket.handshake();
    if (hs) {
        switch (hs->stage()) {
            case FourWayHandshake::Stage::Idle: r.handshake_stage = "idle"; break;
            case FourWayHandshake::Stage::ScoutSent: r.handshake_stage = "scout_sent"; break;
            case FourWayHandshake::Stage::ScoutAckReceived: r.handshake_stage = "scout_ack_received"; break;
            case FourWayHandshake::Stage::DataSent: r.handshake_stage = "data_sent"; break;
            case FourWayHandshake::Stage::WaitForIdle: r.handshake_stage = "wait_for_idle"; break;
            default: r.handshake_stage = "other"; break;
        }
    }
    r.screen = dump_screen(machine, 12);
    return r;
}

} // namespace


TEST_CASE("Econet TX completes without Tube (baseline)",
          "[econet][tx][baseline]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!anfs_rom_available()) SKIP("ANFS 4.18 ROM not available");

    ModelB machine;
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_TEST_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    auto anfs = load_rom(rom_dirpath / ANFS_ROM_FILENAME);
    machine.memory().load_sideways_rom(10, anfs.data(), anfs.size());

    auto backend_ptr = std::make_unique<TestBackend>();
    backend_ptr->set_connected(true);
    auto* backend = backend_ptr.get();
    machine.state().memory.econet_socket.enable(101, std::move(backend_ptr), true);

    auto r = run_econet_tx_test(machine, backend, 8'000'000, 5'000'000);

    INFO("Screen:\n" << r.screen);
    INFO("Frames sent: " << r.frames_sent);
    INFO("CR1=&82 writes (nmi_tx_complete): " << r.cr1_0x82_count);
    INFO("Final CR1: 0x" << std::hex << (int)r.final_cr1);
    INFO("Handshake stage: " << r.handshake_stage);
    INFO("Tick count: " << std::dec << r.tick_count);

    // NFS ROM must have sent at least one frame
    CHECK(r.frames_sent > 0);

    // nmi_tx_complete must have run at least once
    CHECK(r.cr1_0x82_count > 0);
}


TEST_CASE("Econet TX completes with Tube (server scenario)",
          "[econet][tx][tube]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!anfs_rom_available()) SKIP("ANFS 4.18 ROM not available");
    if (!tube_rom_available()) SKIP("Tube 6502 ROM not available");

    ModelB machine;
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_TEST_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    // ANFS 4.18 contains both NFS and Tube Host Code
    auto anfs = load_rom(rom_dirpath / ANFS_ROM_FILENAME);
    machine.memory().load_sideways_rom(10, anfs.data(), anfs.size());

    // Enable Tube BEFORE reset
    machine.state().memory.tube_socket.enable();

    // Set up coprocessor
    auto tube_rom = load_tube_rom();
    TubeUla* tube = machine.state().memory.tube_socket.tube_ula();
    REQUIRE(tube != nullptr);
    CoprocessorRunner coprocessor(*tube, tube_rom);
    coprocessor.reset();
    // The 3:2 clock ratio lives with the runner; the socket drives it in host time.
    machine.state().memory.tube_socket.install_coprocessor(&coprocessor);

    // Enable Econet
    auto backend_ptr = std::make_unique<TestBackend>();
    backend_ptr->set_connected(true);
    auto* backend = backend_ptr.get();
    machine.state().memory.econet_socket.enable(101, std::move(backend_ptr), true);

    // Boot with Tube takes longer (30M cycles for Tube init)
    // TX test also needs more cycles because Tube stretch slows things down
    auto r = run_econet_tx_test(machine, backend, 30'000'000, 10'000'000);

    INFO("Screen:\n" << r.screen);
    INFO("Frames sent: " << r.frames_sent);
    INFO("CR1=&82 writes (nmi_tx_complete): " << r.cr1_0x82_count);
    INFO("Final CR1: 0x" << std::hex << (int)r.final_cr1);
    INFO("Handshake stage: " << r.handshake_stage);
    INFO("Tick count: " << std::dec << r.tick_count);

    // NFS ROM must have sent at least one frame
    CHECK(r.frames_sent > 0);

    // THE KEY CHECK: does nmi_tx_complete run with Tube present?
    // If this fails, the Tube is interfering with NMI delivery.
    CHECK(r.cr1_0x82_count > 0);
}
