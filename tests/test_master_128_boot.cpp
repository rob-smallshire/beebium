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

// test_master_128_boot.cpp
// MOS 3.20 on the Master 128 policy, read through the teletext screen: the
// boot to BASIC in Mode 7 with the Welcome Guide's CMOS, *STATUS showing the
// seed, *CONFIGURE kept across Ctrl-Break by the clock's CMOS, and R held at
// power-on clearing CMOS as the MOS does it.

#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/Machines.hpp>
#include <beebium/TeletextGrid.hpp>
#include <beebium/TeletextText.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace beebium;

namespace {

const std::filesystem::path ROM_DIRPATH = BEEBIUM_ROM_DIR;

std::vector<uint8_t> read_file(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool roms_present() {
    if (!std::filesystem::exists(ROM_DIRPATH / Master128Hardware::DEFAULT_MOS_ROM)) return false;
    for (const auto& rom : Master128Hardware::DEFAULT_SIDEWAYS_ROMS) {
        if (!std::filesystem::exists(ROM_DIRPATH / rom.image_filename)) return false;
    }
    return true;
}

// Keyboard matrix positions (row, column) of the keys these tests press.
constexpr uint8_t CTRL_ROW = 0, CTRL_COLUMN = 1;
constexpr uint8_t R_ROW = 3, R_COLUMN = 3;

struct Master {
    std::unique_ptr<Master128> machine = std::make_unique<Master128>();
    HeapFrameAllocator allocator;
    FrameBuffer frame_buffer{&allocator, 640, 512};
    FrameRenderer renderer{&frame_buffer};
    TeletextGrid grid;

    Master() {
        auto& hw = machine->memory();
        const auto mos = read_file(ROM_DIRPATH / Master128Hardware::DEFAULT_MOS_ROM);
        hw.load_mos(mos.data(), mos.size());
        const auto basic = read_file(ROM_DIRPATH / Master128Hardware::DEFAULT_LANGUAGE_ROM);
        hw.load_sideways_rom(Master128Hardware::DEFAULT_LANGUAGE_SLOT, basic.data(), basic.size());
        for (const auto& rom : Master128Hardware::DEFAULT_SIDEWAYS_ROMS) {
            const auto image = read_file(ROM_DIRPATH / rom.image_filename);
            hw.load_sideways_rom(rom.slot, image.data(), image.size());
        }
        hw.saa5050.set_teletext_grid(&grid);
        hw.enable_video_output();
        machine->reset();
    }

    void seconds(double s) {
        const auto steps = static_cast<uint64_t>(s * 2'000'000.0);
        for (uint64_t i = 0; i < steps; ++i) {
            machine->step();
            renderer.process(machine->memory().video_output.value());
        }
    }

    void type(const std::string& text, double then = 1.0) {
        REQUIRE(machine->memory().system_via_peripheral.type_ahead().enqueue(text));
        seconds(0.5 + 0.1 * static_cast<double>(text.size()) + then);
    }

    void ctrl_break() {
        machine->memory().system_via_peripheral.key_down(CTRL_ROW, CTRL_COLUMN);
        machine->break_down();
        seconds(0.05);
        machine->break_up();
        seconds(0.3);
        machine->memory().system_via_peripheral.key_up(CTRL_ROW, CTRL_COLUMN);
        seconds(2.0);
    }

    std::string screen() { return teletext_text(grid); }
};

// The screen's lines with trailing spaces removed.
std::vector<std::string> screen_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    for (char c : text + "\n") {
        if (c != '\n') { line += c; continue; }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        lines.push_back(line);
        line.clear();
    }
    return lines;
}

}  // namespace

TEST_CASE("Master 128 MOS 3.20 boots to BASIC in Mode 7", "[master128][boot][rom]") {
    if (!roms_present()) SKIP("Master 128 MOS 3.20 ROMs not present");
    Master m;
    m.seconds(3.0);
    const auto text = m.screen();
    INFO(text);
    CHECK(text.find("Acorn MOS") != std::string::npos);
    // FILE 13: the MOS starts ADFS.
    CHECK(text.find("Acorn ADFS") != std::string::npos);
    CHECK(text.find("BASIC") != std::string::npos);
    CHECK(text.find(">") != std::string::npos);
    CHECK(m.machine->memory().video_ula.teletext_mode());
}

TEST_CASE("Master 128 keypad keys type at the BASIC prompt", "[master128][boot][rom][keypad]") {
    if (!roms_present()) SKIP("Master 128 MOS 3.20 ROMs not present");
    Master m;
    m.seconds(3.0);
    auto& via = m.machine->memory().system_via_peripheral;
    // PRINT 7*3 with the digits and operator on the keypad.
    m.type("PRINT ", 0.2);
    for (const auto& [row, column] : {std::pair<uint8_t, uint8_t>{1, 11},    // Keypad7
                                     std::pair<uint8_t, uint8_t>{5, 11},    // KeypadMultiply
                                     std::pair<uint8_t, uint8_t>{6, 12}}) { // Keypad3
        via.key_down(row, column);
        m.seconds(0.1);
        via.key_up(row, column);
        m.seconds(0.1);
    }
    via.key_down(3, 12);  // KeypadReturn
    m.seconds(0.1);
    via.key_up(3, 12);
    m.seconds(0.5);
    const auto lines = screen_lines(m.screen());
    INFO(m.screen());
    CHECK(std::find(lines.begin(), lines.end(), ">PRINT 7*3") != lines.end());
    CHECK(std::find_if(lines.begin(), lines.end(), [](const std::string& line) {
              return line.find("21") != std::string::npos && line.find("PRINT") == std::string::npos;
          }) != lines.end());
}

TEST_CASE("Master 128 *STATUS shows the Welcome Guide configuration",
          "[master128][boot][rom][seed]") {
    if (!roms_present()) SKIP("Master 128 MOS 3.20 ROMs not present");
    Master m;
    m.seconds(3.0);
    m.type("*STATUS\r", 2.0);
    const auto text = m.screen();
    INFO(text);
    // The lines as MOS 3.20 prints them.
    const auto lines = screen_lines(text);
    for (const char* setting : {"Baud     4", "No Boot", "Caps", "Data     4", "Delay    50",
                                "No Directory", "Internal Tube", "FDrive   3", "File     13",
                                "Floppy", "Ignore   10", "Lang     12", "Mode     7", "Tube",
                                "Loud", "Print    1", "Repeat   10", "Scroll", "TV       0,1"}) {
        INFO(setting);
        CHECK(std::find(lines.begin(), lines.end(), setting) != lines.end());
    }
}

TEST_CASE("Master 128 *TIME shows the host's date and time", "[master128][boot][rom][clock]") {
    if (!roms_present()) SKIP("Master 128 MOS 3.20 ROMs not present");
    Master m;
    m.seconds(3.0);
    const std::time_t now = std::time(nullptr);
    m.type("*TIME\r", 1.0);
    const auto text = m.screen();
    INFO(text);
    // "Wed,19 Nov 2026.14:23:05": the date, and the hour and minute (either
    // of two, should the minute turn while the test runs).
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char date[16];
    // MOS 3.20 prints the century as 19 whatever the year (it is not year
    // 2000 compliant): 2026 is "1926".
    std::strftime(date, sizeof date, "%d %b 19%y", &local);
    CHECK(text.find(date) != std::string::npos);
    char hm[8], hm_next[8];
    std::strftime(hm, sizeof hm, "%H:%M", &local);
    const std::time_t later = now + 60;
    std::tm next{};
#if defined(_WIN32)
    localtime_s(&next, &later);
#else
    localtime_r(&later, &next);
#endif
    std::strftime(hm_next, sizeof hm_next, "%H:%M", &next);
    CHECK((text.find(std::string(".") + hm) != std::string::npos ||
           text.find(std::string(".") + hm_next) != std::string::npos));
}

TEST_CASE("Master 128 *CONFIGURE MODE survives Ctrl-Break in the clock's CMOS",
          "[master128][boot][rom][cmos]") {
    if (!roms_present()) SKIP("Master 128 MOS 3.20 ROMs not present");
    Master m;
    m.seconds(3.0);
    m.type("*CONFIGURE MODE 0\r");
    m.ctrl_break();
    CHECK_FALSE(m.machine->memory().video_ula.teletext_mode());
    CHECK((m.machine->memory().rtc.peek_register(0x0E + 10) & 0x07) == 0);
}

TEST_CASE("Master 128 R held at power-on resets CMOS as MOS 3.20 does",
          "[master128][boot][rom][cmos]") {
    if (!roms_present()) SKIP("Master 128 MOS 3.20 ROMs not present");
    Master m;
    m.machine->memory().system_via_peripheral.key_down(R_ROW, R_COLUMN);
    m.machine->reset();
    m.seconds(3.0);
    const auto text = m.screen();
    INFO(text);
    CHECK(text.find("CMOS RAM reset") != std::string::npos);
    auto& rtc = m.machine->memory().rtc;
    CHECK(rtc.peek_register(0x0E + 5) == 0x00);   // LANG 0, FILE 0
    CHECK(rtc.peek_register(0x0E + 6) == 0xFF);   // every ROM inserted
    CHECK(rtc.peek_register(0x0E + 7) == 0xFF);
    CHECK(rtc.peek_register(0x0E + 10) == 0x00);
}
