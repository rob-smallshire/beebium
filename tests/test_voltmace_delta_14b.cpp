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

// The Voltmace Delta 14B/1 adapter: a built-in extension in the analogue port
// and the user port together, and a controller
// (docs/discussion/voltmace-delta-14b-specification.md, tests 1 to 11; test
// 12 is the [joystick] and [analogue_port] suites, unchanged).
//
// Tests 7, 9 and 10 each pin one behaviour the sources leave unverified; their
// names say "unverified" so they are easy to find when a real box is checked.

#include "VoltmaceDelta14bExtension.hpp"
#include "test_temp_paths.hpp"

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/disc/DiscLoader.hpp>
#include <beebium/extension/AnaloguePort.hpp>
#include <beebium/extension/AttachmentPlan.hpp>
#include <beebium/extension/ControllerRegistry.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/UserPort.hpp>
#include <beebium/server/BuiltinExtensions.hpp>
#include <beebium/server/ServerMain.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace beebium;

namespace {

constexpr uint16_t ADC_BASE = 0xFEC0;
constexpr uint16_t SYSTEM_ORB = 0xFE40;
constexpr uint16_t SYSTEM_DDRB = 0xFE42;
constexpr uint16_t USER_ORB = 0xFE60;
constexpr uint16_t USER_DDRB = 0xFE62;
constexpr uint16_t USER_IFR = 0xFE6D;

const std::filesystem::path ROM_DIRPATH = BEEBIUM_ROM_DIR;
const std::filesystem::path ASSETS_DIRPATH = BEEBIUM_TEST_ASSETS_DIR;

// The adapter plugged into `hardware`'s analogue and user ports.
struct Plugged {
    VoltmaceDelta14bExtension adapter;
    ControllerRegistry registry;

    template<typename Hardware>
    explicit Plugged(Hardware& hardware) {
        ExtensionContext ctx(nullptr, &hardware.user_port(), nullptr, nullptr, nullptr, nullptr,
                             nullptr, &hardware.analogue_port(), &registry);
        adapter.init(ctx);
    }
    ~Plugged() { adapter.shutdown(); }
};

template<typename Hardware>
std::pair<uint8_t, uint8_t> convert_12_bit(Hardware& hardware, int channel) {
    hardware.write(ADC_BASE, static_cast<uint8_t>(0x08 | channel));
    for (int i = 0; i < 20000; ++i) (void)hardware.poll_nmi();
    return {hardware.read(ADC_BASE + 1), hardware.read(ADC_BASE + 2)};
}

// Write the user VIA's DDRB and ORB, then read IRB.
template<typename Hardware>
uint8_t strobe(Hardware& hardware, uint8_t ddr, uint8_t output) {
    hardware.write(USER_DDRB, ddr);
    hardware.write(USER_ORB, output);
    return hardware.read(USER_ORB);
}

// System VIA port B bits 4 and 5 (the analogue connector's fire lines).
template<typename Hardware>
uint8_t fire_lines(Hardware& hardware) {
    hardware.write(SYSTEM_DDRB, 0x0F);
    hardware.system_via.tick_rising();
    hardware.system_via.tick_falling();
    return hardware.read(SYSTEM_ORB) & 0x30;
}

// The rows a key reads on, as PB3..PB0: row r clear on PB(3 - r).
uint8_t row_bits_for(int key) {
    return static_cast<uint8_t>(0x0F & ~(1u << (3 - key / 3)));
}

// The column strobe for a key's column, PB7 as given.
uint8_t strobe_for(int key, bool side) {
    const uint8_t columns = static_cast<uint8_t>(0x70 & ~(0x10u << (key % 3)));
    return static_cast<uint8_t>(columns | (side ? 0x80 : 0x00));
}

std::string key_id(int handset, int key) {
    return "handset" + std::to_string(handset) + ".key" + std::to_string(key);
}

std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open ROM: " + filepath.string());
    return {std::istreambuf_iterator<char>(file), {}};
}

bool roms_available() {
    return std::filesystem::exists(ROM_DIRPATH / "acorn-mos_1_20.rom") &&
           std::filesystem::exists(ROM_DIRPATH / "bbc-basic_2.rom") &&
           std::filesystem::exists(ROM_DIRPATH / "acorn-dfs_2_26.rom");
}

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

}  // namespace

TEST_CASE("The Voltmace Delta 14B/1 adapter is a built-in extension on two ports",
          "[voltmace_delta_14b]") {
    const auto* entry = builtin_extensions::find("voltmace-delta-14b");
    REQUIRE(entry != nullptr);
    CHECK(entry->manifest.display_name == "Voltmace Delta 14B/1 adapter");
    CHECK(entry->manifest.cli_name == "voltmace-delta-14b");
    CHECK(entry->manifest.extension_kind == "peripheral");
    CHECK(entry->manifest.description ==
          "Voltmace's Delta 14B/1 adapter box with two Delta 14B handsets: a joystick and a "
          "12-key keypad each, in the analogue port and the user port together.");
    CHECK(entry->manifest.attaches_to ==
          std::vector<std::string>{"analogue-port", "user-port"});
    CHECK(entry->manifest.parameters.empty());
}

TEST_CASE("A Model B started with --voltmace-delta-14b has both ports occupied",
          "[voltmace_delta_14b][cli]") {
    if (!roms_available()) SKIP("ROMs not available");
    server::RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);
    using MachineType = ModelB;
    std::vector<std::string> args = {"beebium", "start", "--voltmace-delta-14b"};
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    server::ServerConfig<MachineType> config;
    auto parsed = server::parse_start_arguments<MachineType>(static_cast<int>(args.size()),
                                                             argv.data(), 2, config);
    REQUIRE_FALSE(parsed.has_value());

    auto outcome = server::assemble_machine<MachineType>(config, /*with_audio=*/false);
    REQUIRE(outcome.assembled);
    auto& am = *outcome.assembled;
    CHECK(am.machine.state().memory.analogue_port().is_occupied());
    CHECK(am.machine.state().memory.user_port().is_occupied());
    auto controllers = am.controller_registry.controllers();
    REQUIRE(controllers.size() == 1);
    CHECK(controllers[0]->controller_id() == "voltmace-delta-14b");
    am.extension_registry.shutdown();
}

TEST_CASE("The Voltmace Delta 14B/1 adapter leaves no room on either port",
          "[voltmace_delta_14b]") {
    const auto* adapter = builtin_extensions::find("voltmace-delta-14b");
    const auto* joystick = builtin_extensions::find("joystick");
    REQUIRE(adapter != nullptr);
    REQUIRE(joystick != nullptr);
    ExtensionManifest user_port_device;
    user_port_device.name = "a-user-port-device";
    user_port_device.extension_kind = "peripheral";
    user_port_device.attaches_to = {"user-port"};
    const std::vector<const ExtensionManifest*> known = {&adapter->manifest,
                                                         &joystick->manifest, &user_port_device};
    const auto machine_points = server::machine_attachment_points<ModelBHardware>();

    SECTION("the plan refuses the joystick as well") {
        const auto problems = check_attachments(
            {{"voltmace-delta-14b", {}}, {"joystick", {}}}, known, machine_points);
        REQUIRE(problems.size() == 1);
        CHECK(problems[0].instance == 1);
        CHECK(problems[0].message == "Analogue Port holds at most 1 extension");
    }
    SECTION("the plan refuses a user port device as well") {
        const auto problems = check_attachments(
            {{"voltmace-delta-14b", {}}, {"a-user-port-device", {}}}, known, machine_points);
        REQUIRE(problems.size() == 1);
        CHECK(problems[0].instance == 1);
        CHECK(problems[0].message == "User Port holds at most 1 extension");
    }
    SECTION("the port handles refuse a second device") {
        ModelBHardware hardware;
        Plugged plugged(hardware);
        VoltmaceDelta14bExtension other;
        CHECK_THROWS_AS(hardware.analogue_port().attach(other), std::runtime_error);
        CHECK_THROWS_AS(hardware.user_port().attach(other), std::runtime_error);
    }
}

TEST_CASE("The Voltmace Delta 14B/1 adapter publishes two handsets of fourteen controls",
          "[voltmace_delta_14b]") {
    VoltmaceDelta14bExtension adapter;
    CHECK(adapter.display_name() == "Voltmace Delta 14B/1 adapter");
    const auto controls = adapter.controls();
    REQUIRE(controls.size() == 28);
    for (int handset = 1; handset <= 2; ++handset) {
        const size_t base = static_cast<size_t>(14 * (handset - 1));
        const std::string prefix = "handset" + std::to_string(handset) + ".";
        const std::string group = handset == 1 ? "Handset 1 (rear)" : "Handset 2 (side)";
        INFO("handset " << handset);
        CHECK(controls[base].id == prefix + "x");
        CHECK(controls[base].display_name == "X");
        CHECK(controls[base].kind == ControlKind::Axis);
        CHECK(controls[base + 1].id == prefix + "y");
        CHECK(controls[base + 1].display_name == "Y");
        CHECK(controls[base + 1].kind == ControlKind::Axis);
        for (int key = 0; key < 12; ++key) {
            const auto& control = controls[base + 2 + static_cast<size_t>(key)];
            INFO("key " << key);
            CHECK(control.id == key_id(handset, key));
            CHECK(control.display_name == "Key " + std::to_string(key));
            CHECK(control.kind == ControlKind::Button);
        }
        for (size_t i = base; i < base + 14; ++i) {
            CHECK(controls[i].group == group);
        }
    }
    for (const auto& control : controls) {
        if (control.kind == ControlKind::Axis) {
            CHECK(adapter.axis(control.id) == 32768);
        } else {
            CHECK_FALSE(adapter.button(control.id));
        }
    }
}

TEST_CASE("Each handset axis reaches its channel of the converter", "[voltmace_delta_14b]") {
    ModelBHardware hardware;
    Plugged plugged(hardware);
    const char* axes[] = {"handset1.x", "handset1.y", "handset2.x", "handset2.y"};
    const uint16_t levels[] = {0x1230, 0x5670, 0x9AB0, 0xDEF0};
    for (int channel = 0; channel < 4; ++channel) {
        plugged.adapter.set_axis(axes[channel], levels[channel]);
    }
    for (int channel = 0; channel < 4; ++channel) {
        INFO("channel " << channel);
        auto [high, low] = convert_12_bit(hardware, channel);
        CHECK(high == (levels[channel] >> 8));
        CHECK(low == (levels[channel] & 0xF0));
    }
}

TEST_CASE("Each key reads on its row under its own column and handset strobe",
          "[voltmace_delta_14b]") {
    ModelBHardware hardware;
    Plugged plugged(hardware);
    auto& adapter = plugged.adapter;
    for (int handset = 1; handset <= 2; ++handset) {
        const bool side = handset == 2;
        for (int key = 0; key < 12; ++key) {
            INFO("handset " << handset << " key " << key);
            adapter.set_button(key_id(handset, key), true);

            const uint8_t own = strobe_for(key, side);
            CHECK(strobe(hardware, 0xF0, own) == (own | row_bits_for(key)));
            for (int other_key : {key + 1, key + 2}) {
                const uint8_t other = strobe_for(other_key, side);
                CHECK(strobe(hardware, 0xF0, other) == (other | 0x0F));
            }
            const uint8_t other_handset = strobe_for(key, !side);
            CHECK(strobe(hardware, 0xF0, other_handset) == (other_handset | 0x0F));

            adapter.set_button(key_id(handset, key), false);
        }
    }
}

TEST_CASE("A strobe written to the user port is read back on the next instruction",
          "[voltmace_delta_14b][e2e]") {
    if (!roms_available()) SKIP("ROMs not available");
    const std::string beebasm = find_beebasm();
    if (beebasm.empty()) SKIP("beebasm not found (set BEEBIUM_BEEBASM or install beebasm)");

    const auto ssd_filepath = beebium::test::unique_temp_path("beebium_keypad_scan_", ".ssd");
    const std::string cmd = beebasm + " -i " +
                            (ASSETS_DIRPATH / "voltmace" / "keypad_scan.asm").string() +
                            " -do " + ssd_filepath.string() +
                            " -boot PROG -title KEYPAD >/dev/null 2>&1";
    REQUIRE(std::system(cmd.c_str()) == 0);

    ModelB machine;
    auto mos = load_rom(ROM_DIRPATH / "acorn-mos_1_20.rom");
    auto basic = load_rom(ROM_DIRPATH / "bbc-basic_2.rom");
    auto dfs = load_rom(ROM_DIRPATH / "acorn-dfs_2_26.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.memory().load_sideways_rom(14, dfs.data(), dfs.size());
    machine.memory().install_acorn_1770();
    auto disc = load_disc_from_url_or_filepath(ssd_filepath.string());
    REQUIRE(disc.success());
    machine.memory().disc_drive_0.insert(std::move(disc.disc));

    Plugged plugged(machine.memory());
    plugged.adapter.set_button("handset2.key7", true);

    machine.memory().set_auto_boot(true);
    machine.reset();
    bool finished = false;
    for (uint64_t i = 0; i < 30'000'000 && !finished; ++i) {
        machine.step();
        if ((i & 0xFFFF) == 0) finished = machine.peek(0x70) == 0xFF;
    }
    REQUIRE(machine.peek(0x70) == 0xFF);

    // Key 7 is row 2 (PB1), column 1 (the &50 and &D0 strobes).
    const uint8_t strobes[] = {0x60, 0x50, 0x30, 0xE0, 0xD0, 0xB0};
    for (int i = 0; i < 6; ++i) {
        INFO("strobe &" << std::hex << int(strobes[i]));
        const uint8_t expected = strobes[i] == 0xD0 ? (0xD0 | 0x0D) : (strobes[i] | 0x0F);
        CHECK(machine.peek(static_cast<uint16_t>(0x72 + i)) == expected);
    }
    std::error_code ec;
    std::filesystem::remove(ssd_filepath, ec);
}

TEST_CASE("The pre-probe of all columns low sees a rear key and an idle side",
          "[voltmace_delta_14b]") {
    ModelBHardware hardware;
    Plugged plugged(hardware);
    auto& adapter = plugged.adapter;

    // With no key held both probes read idle.
    CHECK(strobe(hardware, 0xF0, 0x00) == 0x0F);
    CHECK(strobe(hardware, 0xF0, 0x80) == 0x8F);

    for (int key = 0; key < 12; ++key) {
        INFO("key " << key);
        adapter.set_button(key_id(1, key), true);
        CHECK((strobe(hardware, 0xF0, 0x00) & 0x0F) != 0x0F);
        CHECK(strobe(hardware, 0xF0, 0x80) == 0x8F);
        adapter.set_button(key_id(1, key), false);
    }
    adapter.set_button("handset1.key0", true);
    adapter.set_button("handset1.key11", true);
    CHECK(strobe(hardware, 0xF0, 0x80) == 0x8F);
}

TEST_CASE("Unverified: PB7 as an input selects the side handset", "[voltmace_delta_14b]") {
    ModelBHardware hardware;
    Plugged plugged(hardware);
    auto& adapter = plugged.adapter;
    // DDRB = &70: PB7 and the rows are inputs, the columns outputs. ORB = &50
    // drives column 1 low.
    adapter.set_button("handset2.key4", true);
    CHECK((strobe(hardware, 0x70, 0x50) & 0x0F) == row_bits_for(4));
    adapter.set_button("handset2.key4", false);
    adapter.set_button("handset1.key4", true);
    CHECK((strobe(hardware, 0x70, 0x50) & 0x0F) == 0x0F);
}

TEST_CASE("Columns not driven read every row high and fire nothing",
          "[voltmace_delta_14b]") {
    ModelBHardware hardware;
    Plugged plugged(hardware);
    auto& adapter = plugged.adapter;
    for (int handset = 1; handset <= 2; ++handset) {
        for (int key = 0; key < 12; ++key) {
            adapter.set_button(key_id(handset, key), true);
        }
    }
    CHECK((strobe(hardware, 0x00, 0x00) & 0x0F) == 0x0F);
    CHECK((strobe(hardware, 0x00, 0x80) & 0x0F) == 0x0F);
    CHECK(fire_lines(hardware) == 0x30);
    // Columns outputs but high: not driven either.
    CHECK((strobe(hardware, 0xF0, 0x70) & 0x0F) == 0x0F);
    CHECK(fire_lines(hardware) == 0x30);
}

TEST_CASE("Unverified: row 3 keys reach the fire lines through driven columns",
          "[voltmace_delta_14b]") {
    ModelBHardware hardware;
    Plugged plugged(hardware);
    auto& adapter = plugged.adapter;

    SECTION("every column driven low") {
        (void)strobe(hardware, 0x70, 0x00);
        CHECK(fire_lines(hardware) == 0x30);
        for (int key : {9, 10, 11}) {
            INFO("handset 1 key " << key);
            adapter.set_button(key_id(1, key), true);
            CHECK(fire_lines(hardware) == 0x20);  // bit 4 low, bit 5 high
            adapter.set_button(key_id(1, key), false);
        }
        adapter.set_button("handset2.key10", true);
        CHECK(fire_lines(hardware) == 0x10);  // bit 5 low, bit 4 high
        adapter.set_button("handset2.key10", false);
        adapter.set_button("handset1.key6", true);
        CHECK(fire_lines(hardware) == 0x30);
    }
    SECTION("column 1 only") {
        (void)strobe(hardware, 0xF0, 0x50);
        adapter.set_button("handset1.key10", true);
        CHECK(fire_lines(hardware) == 0x20);
        adapter.set_button("handset1.key10", false);
        adapter.set_button("handset1.key9", true);
        CHECK(fire_lines(hardware) == 0x30);
    }
}

TEST_CASE("Unverified: the adapter leaves user port CB1 and CB2 alone",
          "[voltmace_delta_14b]") {
    ModelBHardware hardware;
    Plugged plugged(hardware);
    auto& adapter = plugged.adapter;
    for (int key = 0; key < 12; key += 2) {
        adapter.set_button(key_id(1, key), true);
        adapter.set_button(key_id(2, key + 1), true);
    }
    hardware.write(USER_DDRB, 0xF0);
    const auto initial = hardware.user_via.state();
    const uint8_t cb1 = initial.port_b.c1;
    const uint8_t cb2 = initial.port_b.c2;
    const uint8_t strobes[] = {0x60, 0x50, 0x30, 0xE0, 0xD0, 0xB0};
    // One second of emulated time at 1 MHz, a strobe written and read back
    // every 100 cycles.
    for (int cycle = 0; cycle < 1'000'000; ++cycle) {
        if (cycle % 100 == 0) {
            hardware.write(USER_ORB, strobes[(cycle / 100) % 6]);
            (void)hardware.read(USER_ORB);
        }
        hardware.user_via.tick_rising();
        hardware.user_via.tick_falling();
        const auto state = hardware.user_via.state();
        if (state.port_b.c1 != cb1 || state.port_b.c2 != cb2 ||
            (hardware.peek(USER_IFR) & 0x18) != 0) {
            FAIL("CB1, CB2 or their IFR bits changed at cycle " << cycle);
        }
    }
    SUCCEED();
}

TEST_CASE("The Voltmace Delta 14B/1 adapter keeps its values across Break and power-on",
          "[voltmace_delta_14b]") {
    ModelBHardware hardware;
    Plugged plugged(hardware);
    auto& adapter = plugged.adapter;
    adapter.set_axis("handset1.x", 0xF000);
    adapter.set_axis("handset2.y", 0x0400);
    adapter.set_button("handset1.key4", true);
    adapter.set_button("handset2.key10", true);

    auto check_held = [&] {
        CHECK(adapter.axis("handset1.x") == 0xF000);
        CHECK(adapter.axis("handset2.y") == 0x0400);
        CHECK(adapter.axis("handset1.y") == 32768);
        CHECK(adapter.button("handset1.key4"));
        CHECK(adapter.button("handset2.key10"));
        CHECK_FALSE(adapter.button("handset1.key10"));
        CHECK(convert_12_bit(hardware, 0).first == 0xF0);
        CHECK(convert_12_bit(hardware, 3).first == 0x04);
        CHECK(strobe(hardware, 0xF0, 0x50) == (0x50 | row_bits_for(4)));
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

TEST_CASE("The Voltmace Delta 14B/1 adapter refuses a control it does not have",
          "[voltmace_delta_14b]") {
    VoltmaceDelta14bExtension adapter;
    auto message_of = [](auto&& action) -> std::string {
        try {
            action();
        } catch (const std::invalid_argument& error) {
            return error.what();
        }
        return "";
    };
    CHECK(message_of([&] { adapter.set_axis("handset3.x", 0); }) ==
          "The Voltmace Delta 14B/1 adapter has no axis named handset3.x.");
    CHECK(message_of([&] { adapter.set_axis("handset1.key0", 0); }) ==
          "The Voltmace Delta 14B/1 adapter has no axis named handset1.key0.");
    CHECK(message_of([&] { adapter.set_button("handset1.key12", true); }) ==
          "The Voltmace Delta 14B/1 adapter has no button named handset1.key12.");
    CHECK(message_of([&] { adapter.set_button("handset2.x", true); }) ==
          "The Voltmace Delta 14B/1 adapter has no button named handset2.x.");
    CHECK(message_of([&] { (void)adapter.button("handset1.key01"); }) ==
          "The Voltmace Delta 14B/1 adapter has no button named handset1.key01.");
}
