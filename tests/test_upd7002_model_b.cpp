// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// The uPD7002 on the Model B family's memory map and System VIA, against
// docs/discussion/upd7002-adc-specification.md: test 2 (the mirror), the
// peek path, the reset rules, and tests 15 to 17. Tests 15 to 17 use MOS 1.20
// only as a test vector: they assert what the machine does when that MOS
// drives the chip, never what any MOS must do.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace beebium;

namespace {

constexpr uint16_t ADC_BASE = 0xFEC0;
constexpr uint16_t ADC_END = 0xFEDF;
constexpr uint8_t STATUS_NOT_EOC = 0x80;
constexpr uint64_t CYCLES_PER_MS = 2000;  // 2 MHz machine cycles

std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open ROM: " + filepath.string());
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool roms_available() {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dir / "acorn-mos_1_20.rom") &&
           std::filesystem::exists(rom_dir / "bbc-basic_2.rom");
}

void load_roms(ModelB& machine) {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dir / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dir / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
}

// Run the chip's own 1 MHz clock without a CPU, through the policy's 1 MHz
// hook, as the machine does on each even cycle.
template<typename Hardware>
void run_adc_clocks(Hardware& hardware, uint32_t clocks) {
    for (uint32_t i = 0; i < clocks; ++i) {
        (void)hardware.poll_nmi();
    }
}

template<typename Hardware>
void check_mirror(Hardware& hardware) {
    hardware.analogue_converter().set_input_level(1, 0x4560);
    // Start a 12-bit conversion on channel 1 through a mirror of offset 0.
    hardware.write(0xFED4, 0x09);
    REQUIRE(hardware.analogue_converter().channel() == 1);
    run_adc_clocks(hardware, 20000);
    REQUIRE((hardware.peek(ADC_BASE) & STATUS_NOT_EOC) == 0);
    REQUIRE((hardware.peek(ADC_BASE) & 0x0F) == 0x09);

    for (uint16_t base = ADC_BASE; base <= ADC_END; base += 4) {
        INFO("mirror at " << std::hex << base);
        for (uint16_t offset = 0; offset < 4; ++offset) {
            CHECK(hardware.peek(static_cast<uint16_t>(base + offset)) ==
                  hardware.peek(static_cast<uint16_t>(ADC_BASE + offset)));
        }
    }
    CHECK(hardware.peek(ADC_BASE + 1) == 0x45);
    CHECK(hardware.peek(ADC_BASE + 2) == 0x60);

    // A read of a mirrored high byte resets EOC like the real one.
    CHECK(hardware.read(0xFEDD) == 0x45);
    CHECK((hardware.peek(ADC_BASE) & STATUS_NOT_EOC) != 0);
}

}  // namespace

TEST_CASE("uPD7002 registers repeat every four bytes from FEC0 to FEDF",
          "[upd7002][memory_map]") {
    SECTION("Model B") { ModelBHardware hardware; check_mirror(hardware); }
    SECTION("Model B+") { ModelBPlusHardware hardware; check_mirror(hardware); }
    SECTION("Model B+ 128K") { ModelBPlus128KHardware hardware; check_mirror(hardware); }
    SECTION("ROM/RAM board") { ModelBRomRamBoardHardware hardware; check_mirror(hardware); }
    SECTION("ATPL Sidewise") { ModelBAtplSidewiseHardware hardware; check_mirror(hardware); }
    SECTION("Watford ROM/RAM") { ModelBWatfordRomRamHardware hardware; check_mirror(hardware); }
    SECTION("Integra-B") { ModelBIntegraBHardware hardware; check_mirror(hardware); }
}

TEST_CASE("uPD7002 peek through the machine leaves EOC and CB1 as they were",
          "[upd7002][memory_map]") {
    ModelBHardware hardware;
    hardware.write(ADC_BASE, 0x08);
    run_adc_clocks(hardware, 20000);
    REQUIRE(hardware.analogue_converter().eoc_level() == 0);
    CHECK(hardware.peek(ADC_BASE + 1) == 0x80);
    CHECK(hardware.analogue_converter().eoc_level() == 0);
    CHECK((hardware.peek(ADC_BASE) & STATUS_NOT_EOC) == 0);
}

TEST_CASE("uPD7002 EOC reaches the System VIA as a CB1 level the VIA edge-detects",
          "[upd7002]") {
    ModelBHardware hardware;
    constexpr uint16_t VIA_PCR = 0xFE4C;
    constexpr uint16_t VIA_IFR = 0xFE4D;
    constexpr uint8_t IFR_CB1 = 0x10;
    auto tick_via = [&] {
        hardware.system_via.tick_rising();
        hardware.system_via.tick_falling();
    };
    auto cb1_flag = [&] { return (hardware.peek(VIA_IFR) & IFR_CB1) != 0; };
    auto clear_cb1_flag = [&] { hardware.write(VIA_IFR, IFR_CB1); };
    auto complete_conversion = [&] {
        hardware.write(ADC_BASE, 0x00);
        tick_via();
        run_adc_clocks(hardware, 20000);
        tick_via();
    };

    SECTION("negative edge: the completion sets the flag, the high-byte read does not") {
        hardware.write(VIA_PCR, 0x00);
        tick_via();
        clear_cb1_flag();
        hardware.write(ADC_BASE, 0x00);
        tick_via();
        CHECK_FALSE(cb1_flag());
        run_adc_clocks(hardware, 20000);
        tick_via();
        CHECK(cb1_flag());
        clear_cb1_flag();
        (void)hardware.read(ADC_BASE + 1);
        tick_via();
        CHECK_FALSE(cb1_flag());
    }
    SECTION("positive edge: the high-byte read sets the flag, the completion does not") {
        hardware.write(VIA_PCR, 0x10);
        tick_via();
        clear_cb1_flag();
        complete_conversion();
        CHECK_FALSE(cb1_flag());
        (void)hardware.read(ADC_BASE + 1);
        tick_via();
        CHECK(cb1_flag());
    }
    SECTION("positive edge: a start write also releases EOC") {
        hardware.write(VIA_PCR, 0x10);
        tick_via();
        complete_conversion();
        clear_cb1_flag();
        hardware.write(ADC_BASE, 0x00);
        tick_via();
        CHECK(cb1_flag());
    }
}

TEST_CASE("uPD7002 survives Break but not a power cycle", "[upd7002]") {
    ModelBHardware hardware;
    hardware.analogue_converter().set_input_level(3, 0x1230);
    hardware.write(ADC_BASE, 0x0B);
    run_adc_clocks(hardware, 100);
    REQUIRE(hardware.analogue_converter().busy());

    SECTION("Break leaves a conversion in flight to complete") {
        hardware.soft_reset();
        CHECK(hardware.analogue_converter().busy());
        CHECK(hardware.analogue_converter().channel() == 3);
        run_adc_clocks(hardware, 20000);
        CHECK(hardware.analogue_converter().eoc_level() == 0);
        CHECK((hardware.peek(ADC_BASE) & 0x0F) == 0x0B);
        CHECK(hardware.peek(ADC_BASE + 1) == 0x12);
    }
    SECTION("a power cycle returns the chip to its power-on state") {
        hardware.reset();
        CHECK_FALSE(hardware.analogue_converter().busy());
        CHECK(hardware.peek(ADC_BASE) == 0xC0);
        CHECK(hardware.peek(ADC_BASE + 1) == 0x00);
        CHECK(hardware.analogue_converter().eoc_level() == 1);
        // Input levels belong to what drives the inputs and are kept.
        CHECK(hardware.analogue_converter().input_level(3) == 0x1230);
    }
}

// The specification asked for 50 ms; on a power-on reset MOS 1.20 clears RAM
// before its VIA set-up writes &0E to the chip, which takes 256 ms here.
TEST_CASE("Model B MOS 1.20 starts its first conversion within 300 ms of power-on",
          "[upd7002][mos]") {
    if (!roms_available()) SKIP("ROMs not available");
    ModelB machine;
    load_roms(machine);
    machine.reset();

    const auto& adc = machine.memory().analogue_converter();
    uint64_t cycles = 0;
    while (!adc.busy() && cycles < 300 * CYCLES_PER_MS) {
        machine.step();
        ++cycles;
    }
    REQUIRE(adc.busy());
    // MOS 1.20 writes &0E: channel 2, 12-bit, flag set.
    CHECK(adc.channel() == 2);
    CHECK(adc.twelve_bit());
    CHECK(adc.flag());
}

TEST_CASE("Model B MOS 1.20 fills its ADC workspace with each channel's high byte",
          "[upd7002][mos]") {
    if (!roms_available()) SKIP("ROMs not available");
    ModelB machine;
    load_roms(machine);
    auto& adc = machine.memory().analogue_converter();
    const uint16_t levels[4] = {0x1100, 0x5500, 0x9900, 0xDD00};
    for (int channel = 0; channel < 4; ++channel) {
        adc.set_input_level(channel, levels[channel]);
    }
    machine.reset();
    for (uint64_t cycle = 0; cycle < 1000 * CYCLES_PER_MS; ++cycle) {
        machine.step();
    }
    // Advanced User Guide section 11: &2BA-&2BD hold the high bytes of the
    // most recent conversion on channels 1 to 4 (hardware channels 0 to 3).
    for (int channel = 0; channel < 4; ++channel) {
        INFO("channel " << channel);
        CHECK(machine.memory().peek(static_cast<uint16_t>(0x2BA + channel)) ==
              (levels[channel] >> 8));
    }
}

TEST_CASE("Model B System VIA sees a CB1 edge every 9 to 12 ms under MOS 1.20",
          "[upd7002][mos]") {
    if (!roms_available()) SKIP("ROMs not available");
    ModelB machine;
    load_roms(machine);
    machine.reset();
    // Let the MOS finish its reset and settle into cycling the channels: its
    // first conversion starts at about 256 ms, the cycling at about 320 ms.
    for (uint64_t cycle = 0; cycle < 500 * CYCLES_PER_MS; ++cycle) {
        machine.step();
    }

    const auto& via = machine.memory().system_via;
    std::vector<uint64_t> edges;
    bool flag = via.state().ifr.bits.cb1 != 0;
    for (uint64_t cycle = 0; cycle < 500 * CYCLES_PER_MS; ++cycle) {
        machine.step();
        const bool now = via.state().ifr.bits.cb1 != 0;
        if (now && !flag) {
            edges.push_back(cycle);
        }
        flag = now;
    }
    REQUIRE(edges.size() >= 40);
    for (size_t i = 1; i < edges.size(); ++i) {
        const uint64_t interval = edges[i] - edges[i - 1];
        INFO("interval " << i << ": " << interval << " cycles");
        CHECK(interval >= 9 * CYCLES_PER_MS);
        CHECK(interval <= 12 * CYCLES_PER_MS);
    }
}
