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

// The joystick: a built-in extension that is the analogue port's device and a
// controller (docs/discussion/controller-service-specification.md, tests 1 to 7
// and 13; test 8 is the [analogue_port] suite, unchanged).

#include "JoystickExtension.hpp"

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/extension/AnaloguePort.hpp>
#include <beebium/extension/ControllerRegistry.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/server/BuiltinExtensions.hpp>
#include <beebium/server/ServerMain.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace beebium;

namespace {

constexpr uint16_t ADC_BASE = 0xFEC0;
constexpr uint16_t VIA_ORB = 0xFE40;
constexpr uint16_t VIA_DDRB = 0xFE42;
constexpr uint64_t CYCLES_PER_MS = 2000;

struct ArgvHelper {
    std::vector<std::string> args;
    std::vector<char*> argv;

    explicit ArgvHelper(std::initializer_list<const char*> init) {
        for (const char* arg : init) args.emplace_back(arg);
        for (auto& s : args) argv.push_back(s.data());
        argv.push_back(nullptr);
    }

    int argc() const { return static_cast<int>(args.size()); }
    char** data() { return argv.data(); }
};

// A joystick plugged into `hardware`'s analogue port, listed in `registry`.
struct Plugged {
    JoystickExtension joystick;
    ControllerRegistry registry;

    template<typename Hardware>
    explicit Plugged(Hardware& hardware) {
        ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                             &hardware.analogue_port(), &registry);
        joystick.init(ctx);
    }
    ~Plugged() { joystick.shutdown(); }
};

template<typename Hardware>
void run_adc_clocks(Hardware& hardware, uint32_t clocks) {
    for (uint32_t i = 0; i < clocks; ++i) {
        (void)hardware.poll_nmi();
    }
}

// A 12-bit conversion on `channel`: the high and low result bytes.
template<typename Hardware>
std::pair<uint8_t, uint8_t> convert_12_bit(Hardware& hardware, int channel) {
    hardware.write(ADC_BASE, static_cast<uint8_t>(0x08 | channel));
    run_adc_clocks(hardware, 20000);
    return {hardware.read(ADC_BASE + 1), hardware.read(ADC_BASE + 2)};
}

template<typename Hardware>
uint8_t read_port_b(Hardware& hardware) {
    hardware.write(VIA_DDRB, 0x0F);  // PB0-PB3 outputs (the latch), PB4-PB7 inputs
    hardware.system_via.tick_rising();
    hardware.system_via.tick_falling();
    return hardware.read(VIA_ORB);
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

}  // namespace

TEST_CASE("The joystick is a built-in extension on the analogue port", "[joystick]") {
    const auto* entry = builtin_extensions::find("joystick");
    REQUIRE(entry != nullptr);
    CHECK(entry->manifest.display_name == "Joystick");
    CHECK(entry->manifest.cli_name == "joystick");
    CHECK(entry->manifest.extension_kind == "peripheral");
    CHECK(entry->manifest.description ==
          "Two analogue joysticks with a fire button each, plugged into the analogue port.");
    CHECK(entry->manifest.attaches_to == std::vector<std::string>{"analogue-port"});
    CHECK(entry->manifest.parameters.empty());
}

TEST_CASE("A Model B started with --joystick has a joystick attached", "[joystick][cli]") {
    if (!roms_available()) SKIP("ROMs not available");
    server::RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);
    using MachineType = ModelB;
    ArgvHelper args{"beebium", "start", "--joystick"};
    server::ServerConfig<MachineType> config;
    auto parsed = server::parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);
    REQUIRE_FALSE(parsed.has_value());

    auto outcome = server::assemble_machine<MachineType>(config, /*with_audio=*/false);
    REQUIRE(outcome.assembled);
    auto& am = *outcome.assembled;
    CHECK(am.machine.state().memory.analogue_port().is_occupied());
    auto controllers = am.controller_registry.controllers();
    REQUIRE(controllers.size() == 1);
    CHECK(controllers[0]->controller_id() == "joystick");
    CHECK(controllers[0]->display_name() == "Joystick");
    am.extension_registry.shutdown();
    CHECK(am.controller_registry.controllers().empty());
}

TEST_CASE("Without --joystick a Model B lists no controller", "[joystick][cli]") {
    if (!roms_available()) SKIP("ROMs not available");
    server::RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);
    using MachineType = ModelB;
    server::ServerConfig<MachineType> config;
    auto outcome = server::assemble_machine<MachineType>(config, /*with_audio=*/false);
    REQUIRE(outcome.assembled);
    auto& am = *outcome.assembled;
    CHECK_FALSE(am.machine.state().memory.analogue_port().is_occupied());
    CHECK(am.controller_registry.controllers().empty());
    am.extension_registry.shutdown();
}

TEST_CASE("The joystick publishes two sticks and two fire buttons", "[joystick]") {
    JoystickExtension joystick;
    const auto controls = joystick.controls();
    struct Expected {
        const char* id;
        const char* display_name;
        const char* group;
        ControlKind kind;
    };
    const Expected expected[] = {
        {"stick1.x", "Stick 1 X", "Stick 1", ControlKind::Axis},
        {"stick1.y", "Stick 1 Y", "Stick 1", ControlKind::Axis},
        {"fire1", "Fire 1", "Stick 1", ControlKind::Button},
        {"stick2.x", "Stick 2 X", "Stick 2", ControlKind::Axis},
        {"stick2.y", "Stick 2 Y", "Stick 2", ControlKind::Axis},
        {"fire2", "Fire 2", "Stick 2", ControlKind::Button},
    };
    REQUIRE(controls.size() == std::size(expected));
    for (size_t i = 0; i < controls.size(); ++i) {
        INFO("control " << i);
        CHECK(controls[i].id == expected[i].id);
        CHECK(controls[i].display_name == expected[i].display_name);
        CHECK(controls[i].group == expected[i].group);
        CHECK(controls[i].kind == expected[i].kind);
    }
    CHECK(joystick.display_name() == "Joystick");
}

TEST_CASE("The joystick starts centred with its buttons released", "[joystick]") {
    JoystickExtension joystick;
    for (const char* id : {"stick1.x", "stick1.y", "stick2.x", "stick2.y"}) {
        INFO(id);
        CHECK(joystick.axis(id) == 32768);
    }
    CHECK_FALSE(joystick.button("fire1"));
    CHECK_FALSE(joystick.button("fire2"));
    for (int channel = 0; channel < 4; ++channel) {
        CHECK(joystick.channel_level(channel) == 32768);
    }
    CHECK(joystick.button_lines() == 0x30);
    CHECK(joystick.light_pen_strobe() == 0);
}

TEST_CASE("Each stick axis reaches its channel of the converter", "[joystick]") {
    ModelBHardware hardware;
    Plugged plugged(hardware);
    auto& joystick = plugged.joystick;

    joystick.set_axis("stick1.x", 0x1234);
    auto [high, low] = convert_12_bit(hardware, 0);
    CHECK(high == 0x12);
    CHECK(low == 0x30);

    const char* axes[] = {"stick1.x", "stick1.y", "stick2.x", "stick2.y"};
    const uint16_t levels[] = {0x1100, 0x5500, 0x9900, 0xDD00};
    for (int channel = 0; channel < 4; ++channel) {
        joystick.set_axis(axes[channel], levels[channel]);
    }
    for (int channel = 0; channel < 4; ++channel) {
        INFO("channel " << channel);
        CHECK(convert_12_bit(hardware, channel).first == (levels[channel] >> 8));
    }
}

TEST_CASE("The fire buttons ground System VIA PB4 and PB5", "[joystick]") {
    ModelBHardware hardware;
    Plugged plugged(hardware);
    auto& joystick = plugged.joystick;

    CHECK((read_port_b(hardware) & 0x30) == 0x30);

    joystick.set_button("fire1", true);
    CHECK((read_port_b(hardware) & 0x30) == 0x20);

    joystick.set_button("fire1", false);
    joystick.set_button("fire2", true);
    CHECK((read_port_b(hardware) & 0x30) == 0x10);

    joystick.set_button("fire1", true);
    CHECK((read_port_b(hardware) & 0x30) == 0x00);

    joystick.set_button("fire1", false);
    joystick.set_button("fire2", false);
    CHECK((read_port_b(hardware) & 0x30) == 0x30);
}

TEST_CASE("The joystick refuses a control it does not have", "[joystick]") {
    JoystickExtension joystick;
    auto message_of = [](auto&& action) -> std::string {
        try {
            action();
        } catch (const std::invalid_argument& error) {
            return error.what();
        }
        return "";
    };
    CHECK(message_of([&] { joystick.set_axis("stick3.x", 0); }) ==
          "The joystick has no axis named stick3.x.");
    CHECK(message_of([&] { joystick.set_axis("fire1", 0); }) ==
          "The joystick has no axis named fire1.");
    CHECK(message_of([&] { joystick.set_button("fire3", true); }) ==
          "The joystick has no button named fire3.");
    CHECK(message_of([&] { joystick.set_button("stick1.x", true); }) ==
          "The joystick has no button named stick1.x.");
    CHECK(message_of([&] { (void)joystick.axis("stick3.x"); }) ==
          "The joystick has no axis named stick3.x.");
    CHECK(message_of([&] { (void)joystick.button("fire3"); }) ==
          "The joystick has no button named fire3.");
}

TEST_CASE("The joystick keeps its values across Break and power-on", "[joystick]") {
    ModelBHardware hardware;
    Plugged plugged(hardware);
    auto& joystick = plugged.joystick;
    joystick.set_axis("stick1.x", 0xF000);
    joystick.set_axis("stick2.y", 0x0400);
    joystick.set_button("fire2", true);

    auto check_held = [&] {
        CHECK(joystick.axis("stick1.x") == 0xF000);
        CHECK(joystick.axis("stick2.y") == 0x0400);
        CHECK(joystick.axis("stick1.y") == 32768);
        CHECK_FALSE(joystick.button("fire1"));
        CHECK(joystick.button("fire2"));
        CHECK(convert_12_bit(hardware, 0).first == 0xF0);
        CHECK(convert_12_bit(hardware, 3).first == 0x04);
        CHECK((read_port_b(hardware) & 0x30) == 0x10);
    };

    SECTION("soft reset") {
        hardware.soft_reset();
        check_held();
    }
    SECTION("power-on reset") {
        hardware.reset();
        check_held();
    }
}

TEST_CASE("Model B MOS 1.20 reads the joystick", "[joystick][mos]") {
    if (!roms_available()) SKIP("ROMs not available");
    ModelB machine;
    load_roms(machine);
    Plugged plugged(machine.memory());
    auto& joystick = plugged.joystick;
    joystick.set_axis("stick1.x", 0xF000);
    machine.reset();

    // Run to the first conversion the MOS starts, then 300 ms on.
    uint64_t cycles = 0;
    while (!machine.memory().analogue_converter().busy() && cycles < 1000 * CYCLES_PER_MS) {
        machine.step();
        ++cycles;
    }
    REQUIRE(machine.memory().analogue_converter().busy());
    for (uint64_t cycle = 0; cycle < 300 * CYCLES_PER_MS; ++cycle) {
        machine.step();
    }
    // Advanced User Guide section 11: &2BA is the high byte of the most
    // recent conversion on channel 1 (hardware channel 0).
    CHECK(machine.memory().peek(0x2BA) == 0xF0);

    // OSBYTE &80 X=0: bit 0 is the left-side fire button (fire1, PB4).
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }
    // At &0900: LDA #&80 : LDX #0 : JSR OSBYTE : STX &70 : JMP * (spin).
    const uint8_t routine[] = {0xA9, 0x80, 0xA2, 0x00, 0x20, 0xF4, 0xFF,
                               0x86, 0x70, 0x4C, 0x09, 0x09};
    auto fire_buttons = [&] {
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
    CHECK(fire_buttons() == 0);
    joystick.set_button("fire1", true);
    CHECK(fire_buttons() == 1);
}
