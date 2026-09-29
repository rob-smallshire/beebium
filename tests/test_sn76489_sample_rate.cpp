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

// The SN76489 produces exactly the declared sample rate per emulated second
// (#126): 48,000 samples for every 2,000,000 CPU cycles, however the CPU uses
// the bus. A client plays the stream at the declared rate, so any shortfall
// drains its buffer.

#include <catch2/catch_test_macros.hpp>

#include "beebium/AudioBuffer.hpp"
#include "beebium/Machines.hpp"
#include "beebium/devices/Sn76489.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace beebium;

namespace {

constexpr uint64_t kCyclesPerSecond = 2'000'000;
constexpr uint64_t kSamplesPerSecond = 48'000;

// Samples the machine's sound chip produces while the machine steps exactly
// `cycles` CPU cycles, whether or not the buffer had room for them.
uint64_t samples_over(ModelB& machine, uint64_t cycles) {
    const auto& audio = *machine.memory().audio_buffer;
    const uint64_t before = audio.sequence() + audio.dropped();
    const uint64_t end = machine.cycle_count() + cycles;
    while (machine.cycle_count() < end) {
        machine.step();
    }
    return audio.sequence() + audio.dropped() - before;
}

// A program that hammers the 1MHz bus: STA &FE40 (System VIA ORB) in a loop,
// so a large share of cycles are bus-stretch cycles.
//   &0400 STA &FE4F ; LDA &FE4F ; JMP &0400
void run_one_mhz_bus_loop(ModelB& machine) {
    std::array<uint8_t, 16384> mos{};
    mos.fill(0xEA);
    mos[0x3FFC] = 0x00;  // reset vector -> &0400
    mos[0x3FFD] = 0x04;
    machine.memory().load_mos(mos.data(), mos.size());
    const uint8_t code[] = {0x8D, 0x4F, 0xFE, 0xAD, 0x4F, 0xFE, 0x4C, 0x00, 0x04};
    for (size_t i = 0; i < sizeof(code); ++i) {
        machine.write(static_cast<uint16_t>(0x0400 + i), code[i]);
    }
    M6502_Reset(&machine.cpu());
}

}  // namespace

TEST_CASE("Sn76489 alone: one emulated second of 2 MHz ticks is 48,000 samples",
          "[sn76489][audio][rate]") {
    Sn76489 chip{4'000'000, 48'000};
    AudioBuffer buffer(kSamplesPerSecond * 11);
    for (uint64_t i = 0; i < kCyclesPerSecond; ++i) chip.tick(buffer);
    CHECK(buffer.sequence() == kSamplesPerSecond);
    for (uint64_t i = 0; i < 9 * kCyclesPerSecond; ++i) chip.tick(buffer);
    CHECK(buffer.sequence() == 10 * kSamplesPerSecond);
}

TEST_CASE("Machine: the sound chip runs through 1MHz bus stretch cycles",
          "[sn76489][audio][rate][machine]") {
    ModelB machine;
    machine.memory().enable_audio_output(kSamplesPerSecond * 11);
    run_one_mhz_bus_loop(machine);
    machine.step_instruction();  // finish the reset sequence

    const uint64_t one = samples_over(machine, kCyclesPerSecond);
    CHECK(one >= kSamplesPerSecond - 1);
    CHECK(one <= kSamplesPerSecond + 1);

    // Ten seconds: no drift accumulates.
    const uint64_t ten = one + samples_over(machine, 9 * kCyclesPerSecond);
    CHECK(ten >= 10 * kSamplesPerSecond - 1);
    CHECK(ten <= 10 * kSamplesPerSecond + 1);
}

#ifdef BEEBIUM_ROM_DIR
TEST_CASE("Machine: booting the MOS produces exactly 48,000 samples a second",
          "[sn76489][audio][rate][machine]") {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto load = [](const std::filesystem::path& filepath) {
        std::ifstream in(filepath, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    };
    auto mos = load(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load(rom_dirpath / "bbc-basic_2.rom");
    REQUIRE(mos.size() == 16384);

    ModelB machine;
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.memory().enable_audio_output(kSamplesPerSecond * 4);
    machine.reset();

    for (int second = 0; second < 3; ++second) {
        INFO("emulated second " << second);
        const uint64_t samples = samples_over(machine, kCyclesPerSecond);
        CHECK(samples >= kSamplesPerSecond - 1);
        CHECK(samples <= kSamplesPerSecond + 1);
    }
}
#endif
