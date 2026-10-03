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

// Application-to-application serial BREAK tests through the *real emulated CPU*.
//
// Unlike the codec / endpoint tests (which drive the C++ SerialPortDevice seam
// directly), these boot a Model B from an auto-booting DFS disc and run a small
// beebasm-assembled 6502 program that touches the MC6850 ACIA and Serial ULA
// hardware registers. So the break travels the whole stack: 6502 instructions
// -> ACIA/ULA -> the SerialPortDevice seam -> the host peer (a recording device,
// or a real pySerial client over RFC 2217), and back.
//
// The disc images are assembled at test time with beebasm (tests/assets/serial/
// serial_break.asm); the test SKIPs cleanly when beebasm, the ROMs, or (for the
// pySerial case) pySerial are unavailable.

#include "Rfc2217ServerEndpoint.hpp"
#include "SerialTestDevice.hpp"
#include "test_temp_paths.hpp"

#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/Machines.hpp>
#include <beebium/disc/DiscLoader.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace beebium;
using namespace std::chrono_literals;

namespace {

#ifdef BEEBIUM_ROM_DIR
const std::filesystem::path kRomDir = BEEBIUM_ROM_DIR;
#else
const std::filesystem::path kRomDir;
#endif

#ifdef BEEBIUM_TEST_ASSETS_DIR
const std::filesystem::path kAssetsDir = BEEBIUM_TEST_ASSETS_DIR;
#else
const std::filesystem::path kAssetsDir;
#endif

// Zero-page locations the beebasm program writes (see serial_break.asm).
constexpr std::uint16_t kSentinel = 0x0070;  // 0xFF when the program has finished
constexpr std::uint16_t kReady = 0x0071;     // 0xAA once RS423 is live (RX mode)

std::vector<std::uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool roms_available() {
    return !kRomDir.empty()
        && std::filesystem::exists(kRomDir / "acorn-mos_1_20.rom")
        && std::filesystem::exists(kRomDir / "bbc-basic_2.rom")
        && std::filesystem::exists(kRomDir / "acorn-dfs_2_26.rom");
}

int exit_status(int system_rc) {
#ifndef _WIN32
    return WIFEXITED(system_rc) ? WEXITSTATUS(system_rc) : -1;
#else
    return system_rc;
#endif
}

// Locate a beebasm executable (PATH, or $BEEBIUM_BEEBASM). Empty if absent.
std::string find_beebasm() {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("BEEBIUM_BEEBASM")) {
        if (env[0]) candidates.emplace_back(env);
    }
    candidates.emplace_back("beebasm");
    for (const auto& c : candidates) {
        const std::string check = c + " --help >/dev/null 2>&1";
        if (std::system(check.c_str()) == 0) return c;
    }
    return "";
}

// Assemble serial_break.asm for the given MODE into an auto-booting DFS SSD.
bool assemble_ssd(const std::string& beebasm, int mode,
                  const std::filesystem::path& out_ssd) {
    const auto asm_filepath = kAssetsDir / "serial" / "serial_break.asm";
    std::error_code ec;
    std::filesystem::remove(out_ssd, ec);
    const std::string cmd = beebasm + " -i " + asm_filepath.string() + " -D MODE="
                            + std::to_string(mode) + " -do " + out_ssd.string()
                            + " -boot PROG -title SERBRK >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0 && std::filesystem::exists(out_ssd);
}

// A booted Model B (MOS 1.20 + BASIC + Acorn 1770 DFS) auto-booting the given
// SSD, with the supplied serial device wired to the RS423 port.
struct BootedBeeb {
    ModelB machine;
    HeapFrameAllocator allocator;
    FrameBuffer fb{&allocator, 640, 512};
    FrameRenderer renderer{&fb};

    BootedBeeb(const std::filesystem::path& ssd, SerialPortDevice& device) {
        auto mos = load_rom(kRomDir / "acorn-mos_1_20.rom");
        auto basic = load_rom(kRomDir / "bbc-basic_2.rom");
        auto dfs = load_rom(kRomDir / "acorn-dfs_2_26.rom");
        machine.memory().load_mos(mos.data(), mos.size());
        machine.memory().load_basic(basic.data(), basic.size());
        machine.memory().load_sideways_rom(14, dfs.data(), dfs.size());
        machine.memory().install_acorn_1770();

        auto disc = load_disc_from_url_or_filepath(ssd.string());
        REQUIRE(disc.success());
        machine.memory().disc_drive_0.insert(std::move(disc.disc));

        machine.memory().serial_socket.set_device(&device);
        machine.memory().enable_video_output();
        machine.memory().set_auto_boot(true);  // SHIFT-BREAK equivalent: run !BOOT
        machine.reset();
    }

    // Step until peek(addr) == value, or the cycle budget is exhausted.
    bool run_until_byte(std::uint16_t addr, std::uint8_t value, std::uint64_t budget) {
        for (std::uint64_t i = 0; i < budget; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
            if ((i & 0xFFFF) == 0 && machine.peek(addr) == value) return true;
        }
        return machine.peek(addr) == value;
    }
};

std::string find_pyserial_runner() {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("BEEBIUM_PYSERIAL")) {
        if (env[0]) candidates.emplace_back(env);
    }
    candidates.emplace_back("uv run --with pyserial python3");
    candidates.emplace_back("python3");
    for (const auto& c : candidates) {
        const std::string check = c + " -c \"import serial\" >/dev/null 2>&1";
        if (std::system(check.c_str()) == 0) return c;
    }
    return "";
}

}  // namespace

// The BBC transmits a BREAK (its 6502 program writes the 6850 Transmitter-Control
// field), and the attached serial device sees the asserted-then-cleared edges.
// Always runs when beebasm + the ROMs are present (no external peer needed).
TEST_CASE("BBC 6502 program transmits a serial BREAK to the device",
          "[serial][break][e2e]") {
    if (!roms_available()) SKIP("ROMs not available");
    const std::string beebasm = find_beebasm();
    if (beebasm.empty()) SKIP("beebasm not found (set BEEBIUM_BEEBASM or install beebasm)");

    const auto ssd = beebium::test::unique_temp_path("beebium_serial_break_tx_", ".ssd");
    REQUIRE(assemble_ssd(beebasm, /*MODE=*/0, ssd));

    SerialTestDevice device;
    BootedBeeb beeb(ssd, device);

    // The program asserts a break, holds it, clears it, then writes the sentinel.
    REQUIRE(beeb.run_until_byte(kSentinel, 0xFF, 30'000'000));

    // The device must have seen exactly one break pulse: a rising edge to true
    // followed by a falling edge back to false (after the baseline level(s)).
    const auto& edges = device.break_events();
    REQUIRE(edges.size() >= 2);
    CHECK(edges.back() == false);                                  // break was cleared
    bool saw_pulse = false;
    for (std::size_t i = 0; i + 1 < edges.size(); ++i) {
        if (edges[i] == true && edges[i + 1] == false) saw_pulse = true;
    }
    CHECK(saw_pulse);

    std::error_code ec;
    std::filesystem::remove(ssd, ec);
}

// A real pySerial client transmits a BREAK over RFC 2217; the BBC's 6502 program
// detects it at the hardware level (Framing Error + data 0x00) and sets a
// sentinel. The full application-to-application loop through the emulated CPU.
// Opt-in: SKIPs without beebasm or pySerial.
TEST_CASE("pySerial transmits a BREAK that the BBC 6502 program detects",
          "[serial][break][e2e][pyserial]") {
    if (!roms_available()) SKIP("ROMs not available");
    const std::string beebasm = find_beebasm();
    if (beebasm.empty()) SKIP("beebasm not found (set BEEBIUM_BEEBASM or install beebasm)");
    const std::string runner = find_pyserial_runner();
    if (runner.empty()) SKIP("no pySerial-capable Python found");

    const auto ssd = beebium::test::unique_temp_path("beebium_serial_break_rx_", ".ssd");
    REQUIRE(assemble_ssd(beebasm, /*MODE=*/1, ssd));

    rfc2217::Rfc2217ServerEndpoint::Options opts;
    opts.bind = "127.0.0.1";
    opts.port = 0;  // ephemeral
    rfc2217::Rfc2217ServerEndpoint server(std::move(opts));
    REQUIRE(server.is_listening());
    const std::uint16_t port = server.local_port();

    BootedBeeb beeb(ssd, server);

    // Run until the program has selected RS423 (carrier up) and is polling, so the
    // break is not consumed before the receiver has a carrier to detect it on.
    REQUIRE(beeb.run_until_byte(kReady, 0xAA, 30'000'000));

    // A pySerial client that connects and sends a real break.
    const auto script = beebium::test::unique_temp_path("beebium_serial_break_sender_", ".py");
    {
        std::ofstream f(script);
        f << "import sys, serial\n"
             "s = serial.serial_for_url(sys.argv[1], baudrate=9600, timeout=5)\n"
             "s.send_break(0.1)\n"
             "s.close()\n";
    }
    std::atomic<int> py_rc{-1};
    std::thread sender([&] {
        const std::string cmd = runner + " " + script.string()
                                + " rfc2217://127.0.0.1:" + std::to_string(port);
        py_rc.store(exit_status(std::system(cmd.c_str())));
    });

    // The program sets the sentinel once it sees the Framing-Error + data-ready
    // condition (the received break).
    const bool detected = beeb.run_until_byte(kSentinel, 0xFF, 60'000'000);

    sender.join();
    std::error_code ec;
    std::filesystem::remove(script, ec);
    std::filesystem::remove(ssd, ec);

    CHECK(py_rc.load() == 0);
    CHECK(detected);
}
