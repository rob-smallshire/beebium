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

// The analogue port: the AnaloguePort handle, the AnaloguePortDevice seam,
// and its wiring into the uPD7002, System VIA PB4/PB5 and CB2, and the
// CRTC's light pen input, against
// docs/discussion/analogue-port-seam-specification.md (tests 1 to 7, 10
// to 11, and 8; 9 is with the inspection tests, 12 with the Python
// client's). Tests 10 and 11 use
// MOS 1.20 only as a test vector.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/VideoBinding.hpp>
#include <beebium/devices/Upd7002.hpp>
#include <beebium/extension/AnaloguePort.hpp>
#include <beebium/extension/AnaloguePortDevice.hpp>
#include <beebium/extension/ExtensionContext.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace beebium;

namespace {

constexpr uint16_t ADC_BASE = 0xFEC0;
constexpr uint8_t STATUS_NOT_EOC = 0x80;
constexpr uint16_t VIA_ORB = 0xFE40;
constexpr uint16_t VIA_DDRB = 0xFE42;
constexpr uint16_t VIA_PCR = 0xFE4C;
constexpr uint16_t VIA_IFR = 0xFE4D;
constexpr uint8_t IFR_CB2 = 0x08;
constexpr uint64_t CYCLES_PER_MS = 2000;

// A device on the port: settable levels, button bits and strobe, and a
// record of which channels the converter sampled.
class StubAnalogueDevice : public AnaloguePortDevice {
public:
    uint16_t channel_level(int channel) override {
        sampled_channels.push_back(channel);
        return levels[static_cast<size_t>(channel & 3)];
    }
    uint8_t button_lines() override { return buttons; }
    uint8_t light_pen_strobe() override { return strobe; }

    std::array<uint16_t, 4> levels = {UNPLUGGED_LEVEL, UNPLUGGED_LEVEL,
                                      UNPLUGGED_LEVEL, UNPLUGGED_LEVEL};
    uint8_t buttons = 0x30;
    uint8_t strobe = 0;
    std::vector<int> sampled_channels;
};

template<typename Hardware>
void run_adc_clocks(Hardware& hardware, uint32_t clocks) {
    for (uint32_t i = 0; i < clocks; ++i) {
        (void)hardware.poll_nmi();
    }
}

template<typename Hardware>
void tick_system_via(Hardware& hardware) {
    hardware.system_via.tick_rising();
    hardware.system_via.tick_falling();
}

template<typename Hardware>
void check_has_port() {
    Hardware hardware;
    AnaloguePort& port = hardware.analogue_port();
    CHECK_FALSE(port.is_occupied());
}

std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open ROM: " + filepath.string());
    return {std::istreambuf_iterator<char>(file), {}};
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

void run_ms(ModelB& machine, uint64_t ms) {
    for (uint64_t cycle = 0; cycle < ms * CYCLES_PER_MS; ++cycle) {
        machine.step();
    }
}

}  // namespace

TEST_CASE("Every Model B family policy has an empty analogue port",
          "[analogue_port]") {
    check_has_port<ModelBHardware>();
    check_has_port<ModelBPlusHardware>();
    check_has_port<ModelBPlus128KHardware>();
    check_has_port<ModelBRomRamBoardHardware>();
    check_has_port<ModelBAtplSidewiseHardware>();
    check_has_port<ModelBWatfordRomRamHardware>();
    check_has_port<ModelBIntegraBHardware>();
    static_assert(HasAnaloguePort<ModelBHardware>);
    static_assert(HasAnaloguePort<ModelBIntegraBHardware>);
}

TEST_CASE("The analogue port takes one device", "[analogue_port]") {
    ModelBHardware hardware;
    StubAnalogueDevice first;
    StubAnalogueDevice second;
    hardware.analogue_port().attach(first);
    CHECK(hardware.analogue_port().is_occupied());
    CHECK_THROWS_AS(hardware.analogue_port().attach(second), std::runtime_error);
    CHECK(hardware.analogue_port().is_occupied());
}

TEST_CASE("A conversion samples the device's level for the channel written, once",
          "[analogue_port]") {
    ModelBHardware hardware;
    StubAnalogueDevice device;
    device.levels = {0x1234, 0x5678, 0x9ABC, 0xDEF0};
    hardware.analogue_port().attach(device);

    const uint8_t expected_high[4] = {0x12, 0x56, 0x9A, 0xDE};
    const uint8_t expected_low[4] = {0x30, 0x70, 0xB0, 0xF0};
    for (int channel = 0; channel < 4; ++channel) {
        INFO("channel " << channel);
        device.sampled_channels.clear();
        hardware.write(ADC_BASE, static_cast<uint8_t>(0x08 | channel));
        run_adc_clocks(hardware, 20000);
        REQUIRE((hardware.peek(ADC_BASE) & STATUS_NOT_EOC) == 0);
        CHECK(hardware.read(ADC_BASE + 2) == expected_low[channel]);
        CHECK(hardware.read(ADC_BASE + 1) == expected_high[channel]);
        CHECK(device.sampled_channels == std::vector<int>{channel});
    }
}

TEST_CASE("With no device a conversion reads the unplugged level", "[analogue_port]") {
    // The chip's and the device seam's unplugged level are the same value.
    CHECK(AnaloguePortDevice::UNPLUGGED_LEVEL == Upd7002::UNPLUGGED_LEVEL);
    ModelBHardware hardware;
    hardware.write(ADC_BASE, 0x09);
    run_adc_clocks(hardware, 20000);
    CHECK(hardware.read(ADC_BASE + 2) == 0xF0);
    CHECK(hardware.read(ADC_BASE + 1) == 0xCA);
}

TEST_CASE("The device's button lines are System VIA PB4 and PB5", "[analogue_port]") {
    ModelBHardware hardware;
    hardware.write(VIA_DDRB, 0x0F);  // PB0-PB3 outputs (the latch), PB4-PB7 inputs
    tick_system_via(hardware);
    const uint8_t no_device = hardware.read(VIA_ORB);
    CHECK((no_device & 0x30) == 0x30);

    StubAnalogueDevice device;
    hardware.analogue_port().attach(device);

    device.buttons = 0x20;  // PB4 pressed
    tick_system_via(hardware);
    const uint8_t pb4_pressed = hardware.read(VIA_ORB);
    CHECK((pb4_pressed & 0x10) == 0x00);
    CHECK((pb4_pressed & 0x20) == 0x20);
    CHECK((pb4_pressed & 0xCF) == (no_device & 0xCF));

    device.buttons = 0x30;
    tick_system_via(hardware);
    CHECK(hardware.read(VIA_ORB) == no_device);

    device.buttons = 0xFF & ~0x20;  // PB5 pressed; other bits ignored
    tick_system_via(hardware);
    const uint8_t pb5_pressed = hardware.read(VIA_ORB);
    CHECK((pb5_pressed & 0x30) == 0x10);
    CHECK((pb5_pressed & 0xCF) == (no_device & 0xCF));
}

TEST_CASE("The device's light pen strobe reaches System VIA CB2", "[analogue_port]") {
    SECTION("a strobe sets the CB2 flag under negative-edge CB2 input") {
        ModelBHardware hardware;
        StubAnalogueDevice device;
        hardware.analogue_port().attach(device);
        hardware.write(VIA_PCR, 0x00);  // CB2 input, negative edge
        tick_system_via(hardware);
        hardware.write(VIA_IFR, IFR_CB2);
        device.strobe = 1;
        tick_system_via(hardware);
        CHECK((hardware.peek(VIA_IFR) & IFR_CB2) == 0);
        device.strobe = 0;
        tick_system_via(hardware);
        CHECK((hardware.peek(VIA_IFR) & IFR_CB2) != 0);
    }
    SECTION("with no device CB2 never flags") {
        ModelBHardware hardware;
        hardware.write(VIA_PCR, 0x00);
        tick_system_via(hardware);
        hardware.write(VIA_IFR, IFR_CB2);
        for (int tick = 0; tick < 1'000'000; ++tick) {
            tick_system_via(hardware);
        }
        CHECK((hardware.peek(VIA_IFR) & IFR_CB2) == 0);
    }
}

TEST_CASE("The device's light pen strobe latches the CRTC's address", "[analogue_port]") {
    auto set_crtc = [](ModelBHardware& hardware, uint8_t reg, uint8_t value) {
        hardware.crtc.write(0, reg);
        hardware.crtc.write(1, value);
    };
    auto read_light_pen = [](ModelBHardware& hardware) {
        hardware.crtc.write(0, 16);
        const uint16_t high = hardware.crtc.read(1);
        hardware.crtc.write(0, 17);
        const uint16_t low = hardware.crtc.read(1);
        return static_cast<uint16_t>((high << 8) | low);
    };
    auto setup = [&](ModelBHardware& hardware) {
        // A plain 40-column frame starting at character address &1000.
        set_crtc(hardware, 0, 63);
        set_crtc(hardware, 1, 40);
        set_crtc(hardware, 2, 51);
        set_crtc(hardware, 4, 38);
        set_crtc(hardware, 6, 32);
        set_crtc(hardware, 7, 34);
        set_crtc(hardware, 9, 7);
        set_crtc(hardware, 12, 0x10);
        set_crtc(hardware, 13, 0x00);
    };

    SECTION("a rising strobe latches the address of that tick") {
        ModelBHardware hardware;
        setup(hardware);
        StubAnalogueDevice device;
        hardware.analogue_port().attach(device);
        VideoBinding<ModelBHardware> video(hardware);
        for (int tick = 0; tick < 5000; ++tick) {
            video.tick_falling();
        }
        const uint16_t before = read_light_pen(hardware);
        const uint16_t address = hardware.crtc.address() & 0x3FFF;
        device.strobe = 1;
        video.tick_falling();
        const uint16_t latched = read_light_pen(hardware);
        CHECK(latched == address);
        CHECK(latched != before);
        // Held high, the strobe does not latch again.
        for (int tick = 0; tick < 100; ++tick) {
            video.tick_falling();
        }
        CHECK(read_light_pen(hardware) == latched);
    }
    SECTION("with no device the light pen registers never change") {
        ModelBHardware hardware;
        setup(hardware);
        VideoBinding<ModelBHardware> video(hardware);
        const uint16_t before = read_light_pen(hardware);
        for (int tick = 0; tick < 100000; ++tick) {
            video.tick_falling();
        }
        CHECK(read_light_pen(hardware) == before);
    }
}

TEST_CASE("Model B MOS 1.20 fills its ADC workspace from the device",
          "[analogue_port][mos]") {
    if (!roms_available()) SKIP("ROMs not available");
    ModelB machine;
    load_roms(machine);
    StubAnalogueDevice device;
    device.levels = {0x1100, 0x5500, 0x9900, 0xDD00};
    machine.memory().analogue_port().attach(device);
    machine.reset();
    run_ms(machine, 1000);
    // Advanced User Guide section 11: &2BA-&2BD hold the high bytes of the
    // most recent conversion on channels 1 to 4 (hardware channels 0 to 3).
    for (int channel = 0; channel < 4; ++channel) {
        INFO("channel " << channel);
        CHECK(machine.memory().peek(static_cast<uint16_t>(0x2BA + channel)) ==
              (device.levels[static_cast<size_t>(channel)] >> 8));
    }
}

TEST_CASE("Model B MOS 1.20 reports the device's fire buttons through OSBYTE &80",
          "[analogue_port][mos]") {
    if (!roms_available()) SKIP("ROMs not available");
    ModelB machine;
    load_roms(machine);
    StubAnalogueDevice device;
    machine.memory().analogue_port().attach(device);
    machine.reset();
    run_ms(machine, 1500);
    // Finish on an instruction boundary before moving the PC.
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // At &0900: LDA #&80 : LDX #0 : JSR OSBYTE : STX &70 : JMP * (spin).
    const uint8_t routine[] = {0xA9, 0x80, 0xA2, 0x00, 0x20, 0xF4, 0xFF,
                               0x86, 0x70, 0x4C, 0x09, 0x09};
    auto fire_buttons = [&](uint8_t lines) {
        device.buttons = lines;
        for (size_t i = 0; i < sizeof(routine); ++i) {
            machine.write(static_cast<uint16_t>(0x0900 + i), routine[i]);
        }
        machine.write(0x70, 0xEE);
        machine.set_pc(0x0900);
        for (int i = 0; i < 2000 && machine.pc() != 0x0909; ++i) {
            machine.step_instruction();
        }
        REQUIRE(machine.pc() == 0x0909);
        return machine.peek(0x70) & 0x03;
    };

    // ADVAL(0) AND 3 (User Guide): 1 for the left-side button (PB4), 2 for
    // the right-side button (PB5), 3 for both.
    CHECK(fire_buttons(0x30) == 0);
    CHECK(fire_buttons(0x20) == 1);
    CHECK(fire_buttons(0x10) == 2);
    CHECK(fire_buttons(0x00) == 3);
}

// A period idiom for detecting a joystick: read channel 1 through OSBYTE &80
// and take a high byte of exactly &80, a centred stick, as a joystick
// present (the 1987 Dunjunz does this twice, and then reads its keys from
// the stick). With the port empty the reading must not be centred, so such
// a program takes the keyboard.
TEST_CASE("Model B MOS 1.20 with the port empty does not read a centred joystick",
          "[analogue_port][mos]") {
    if (!roms_available()) SKIP("ROMs not available");
    ModelB machine;
    load_roms(machine);
    machine.reset();
    run_ms(machine, 1500);
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // At &0900: LDA #&80 : LDX #1 : JSR OSBYTE : STY &70, twice (&70, &71),
    // then JMP * (spin).
    const uint8_t routine[] = {0xA9, 0x80, 0xA2, 0x01, 0x20, 0xF4, 0xFF, 0x84, 0x70,
                               0xA9, 0x80, 0xA2, 0x01, 0x20, 0xF4, 0xFF, 0x84, 0x71,
                               0x4C, 0x12, 0x09};
    for (size_t i = 0; i < sizeof(routine); ++i) {
        machine.write(static_cast<uint16_t>(0x0900 + i), routine[i]);
    }
    machine.write(0x70, 0x80);
    machine.write(0x71, 0x80);
    machine.set_pc(0x0900);
    for (int i = 0; i < 4000 && machine.pc() != 0x0912; ++i) {
        machine.step_instruction();
    }
    REQUIRE(machine.pc() == 0x0912);
    CHECK(machine.peek(0x70) != 0x80);
    CHECK(machine.peek(0x71) != 0x80);
    CHECK(machine.peek(0x70) == (Upd7002::UNPLUGGED_LEVEL >> 8));
    CHECK(machine.peek(0x71) == (Upd7002::UNPLUGGED_LEVEL >> 8));
}

TEST_CASE("ExtensionContext hands out the analogue port", "[analogue_port][extension-context]") {
    ModelBHardware hardware;
    ExtensionContext context(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                             &hardware.analogue_port());
    CHECK(context.has<AnaloguePort>());
    CHECK(&context.get<AnaloguePort>() == &hardware.analogue_port());

    ExtensionContext without;
    CHECK_FALSE(without.has<AnaloguePort>());
    try {
        (void)without.get<AnaloguePort>();
        FAIL("get<AnaloguePort>() should throw without a port");
    } catch (const std::runtime_error& error) {
        CHECK(std::string(error.what()) ==
              "ExtensionContext: analogue-port not available on this machine");
    }
}
