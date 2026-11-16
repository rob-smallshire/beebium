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

// Loading from tape through the whole stack: a tape the test builds, the deck,
// the Serial ULA's cassette side, the 6850 and MOS 1.20's cassette filing
// system, read back from the screen (cassette round 4, #208). MOS 1.20 is a test
// vector here: nothing in the model depends on it.

#include "support/tape_builder.hpp"
#include "test_temp_paths.hpp"

#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/Machines.hpp>
#include <beebium/TeletextGrid.hpp>
#include <beebium/TeletextText.hpp>
#include <beebium/cassette/TapeImage.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace beebium;
using beebium::test::FileOptions;
using beebium::test::TapeBuilder;

namespace {

const std::filesystem::path ROM_DIRPATH = BEEBIUM_ROM_DIR;
const std::filesystem::path ASSETS_DIRPATH = BEEBIUM_TEST_ASSETS_DIR;

std::vector<uint8_t> read_file(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool roms_present() {
    return std::filesystem::exists(ROM_DIRPATH / "acorn-mos_1_20.rom") &&
           std::filesystem::exists(ROM_DIRPATH / "bbc-basic_2.rom");
}

// tests/assets/cassette/<name>.bas tokenised with oaknut-basic, or nullopt if
// the tool is not available.
std::optional<std::vector<uint8_t>> tokenised(const std::string& name) {
    const auto dirpath = beebium::test::unique_temp_path("beebium_cassette_basic_", "");
    std::filesystem::create_directories(dirpath);
    const auto tok_filepath = dirpath / (name + ".tok");
    const std::string command = "uvx oaknut-basic tokenise " +
        (ASSETS_DIRPATH / "cassette" / (name + ".bas")).string() + " " + tok_filepath.string() +
        " >/dev/null 2>&1";
    if (std::system(command.c_str()) != 0 || !std::filesystem::exists(tok_filepath)) {
        return std::nullopt;
    }
    return read_file(tok_filepath);
}

// A BASIC program as MOS 1.20 saves it: loaded at PAGE (&0E00 with no disc
// filing system), run by BASIC.
constexpr uint32_t BASIC_LOAD = 0xFFFF0E00;
constexpr uint32_t BASIC_EXEC = 0xFFFF8023;

// A Model B with MOS 1.20 and BASIC 2 and no disc filing system, a tape in its
// deck, watched through the Mode 7 screen.
struct TapeBeeb {
    ModelB machine;
    HeapFrameAllocator allocator;
    FrameBuffer frame_buffer{&allocator, 640, 512};
    FrameRenderer renderer{&frame_buffer};
    TeletextGrid grid;

    TapeBeeb() {
        const auto mos = read_file(ROM_DIRPATH / "acorn-mos_1_20.rom");
        const auto basic = read_file(ROM_DIRPATH / "bbc-basic_2.rom");
        machine.memory().load_mos(mos.data(), mos.size());
        machine.memory().load_basic(basic.data(), basic.size());
        machine.memory().saa5050.set_teletext_grid(&grid);
        machine.memory().enable_video_output();
        machine.reset();
    }

    CassetteDeck& deck() { return machine.memory().cassette_deck; }
    SerialUla& ula() { return machine.memory().serial_socket.ula(); }

    void insert(const std::vector<uint8_t>& image, const std::string& name, bool play = true) {
        deck().insert(open_tape_image(image, name), name);
        if (play) deck().play();
    }

    void type(const std::string& text) {
        REQUIRE(machine.memory().system_via_peripheral.type_ahead().enqueue(text));
    }

    std::string screen() { return teletext_text(grid); }

    void step(uint64_t steps) {
        for (uint64_t i = 0; i < steps; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    // Run until the screen shows `text`, up to `seconds` of emulated time.
    bool run_until_screen(const std::string& text, double seconds) {
        const auto steps = static_cast<uint64_t>(seconds * 2'000'000.0);
        constexpr uint64_t CHUNK = 200'000;
        for (uint64_t done = 0; done < steps; done += CHUNK) {
            step(CHUNK);
            if (screen().find(text) != std::string::npos) return true;
        }
        return screen().find(text) != std::string::npos;
    }
};

// The BASIC program on a standard MOS tape, as UEF or CSW.
struct ProgramTape {
    std::vector<uint8_t> uef;
    std::vector<uint8_t> csw;
};

std::optional<ProgramTape> program_tape(const FileOptions& options = {}) {
    const auto program = tokenised("tape_ok");
    if (!program) return std::nullopt;
    TapeBuilder builder;
    builder.origin("beebium tests").file("TAPEOK", BASIC_LOAD, BASIC_EXEC, *program, options);
    return ProgramTape{builder.uef(), builder.csw()};
}

#define REQUIRE_ROMS_AND_TOOLS(tape)                                                            \
    if (!roms_present()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not in " + ROM_DIRPATH.string() + "."); \
    if (!(tape)) SKIP("oaknut-basic is not available through uvx, so the test program cannot be tokenised.")

}  // namespace

// =============================================================================
// 11. *TAPE and CHAIN"" at 1200 and 300 baud
// =============================================================================

TEST_CASE("MOS 1.20 loads and runs a BASIC program from a UEF tape", "[cassette][mos]") {
    const auto tape = program_tape();
    REQUIRE_ROMS_AND_TOOLS(tape);
    TapeBeeb beeb;
    beeb.insert(tape->uef, "tape_ok.uef");
    beeb.type("*TAPE\rCHAIN\"\"\r");
    const bool loaded = beeb.run_until_screen("TAPE OK 42", 30.0);
    INFO(beeb.screen());
    CHECK(loaded);
}

TEST_CASE("MOS 1.20 loads a 300 baud tape after *TAPE3", "[cassette][mos]") {
    const auto tape = program_tape({.baud = 300});
    REQUIRE_ROMS_AND_TOOLS(tape);
    TapeBeeb beeb;
    beeb.insert(tape->uef, "tape_ok_300.uef");
    beeb.type("*TAPE3\rCHAIN\"\"\r");
    const bool loaded = beeb.run_until_screen("TAPE OK 42", 40.0);
    INFO(beeb.screen());
    CHECK(loaded);
}

// =============================================================================
// 12. The CSW of the same tape
// =============================================================================

TEST_CASE("MOS 1.20 loads the same program from its CSW", "[cassette][mos][csw]") {
    const auto tape = program_tape();
    REQUIRE_ROMS_AND_TOOLS(tape);
    TapeBeeb beeb;
    beeb.insert(tape->csw, "tape_ok.csw");
    beeb.type("*TAPE\rCHAIN\"\"\r");
    const bool loaded = beeb.run_until_screen("TAPE OK 42", 30.0);
    INFO(beeb.screen());
    CHECK(loaded);
}

// =============================================================================
// 13. Data? and Block?
// =============================================================================

TEST_CASE("MOS 1.20 reports Data? and Rewind tape for a bad data CRC", "[cassette][mos]") {
    if (!roms_present()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    const std::vector<uint8_t> data(100, 0x55);
    TapeBuilder builder;
    builder.file("BIN", 0xFFFF3000, 0xFFFF3000, data, {.corrupt_data_crc_block = 0});
    TapeBeeb beeb;
    beeb.insert(builder.uef(), "bad_crc.uef");
    beeb.type("*TAPE\r*LOAD BIN\r");
    const bool reported = beeb.run_until_screen("Rewind tape", 20.0);
    INFO(beeb.screen());
    CHECK(reported);
    CHECK(beeb.screen().find("Data?") != std::string::npos);
}

TEST_CASE("MOS 1.20 reports Block? for a block out of sequence", "[cassette][mos]") {
    if (!roms_present()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    const std::vector<uint8_t> data(300, 0xA5);
    TapeBuilder builder;
    builder.file("BIN", 0xFFFF3000, 0xFFFF3000, data, {.renumber = {{1, 2}}});
    TapeBeeb beeb;
    beeb.insert(builder.uef(), "out_of_sequence.uef");
    beeb.type("*TAPE\r*LOAD BIN\r");
    const bool reported = beeb.run_until_screen("Block?", 25.0);
    INFO(beeb.screen());
    CHECK(reported);
}

// =============================================================================
// 14. A short leader
// =============================================================================

TEST_CASE("MOS 1.20 loads a tape with the Welcome tape's short leaders", "[cassette][mos]") {
    const auto tape = program_tape({.short_leader = true});
    REQUIRE_ROMS_AND_TOOLS(tape);
    TapeBeeb beeb;
    beeb.insert(tape->uef, "short.uef");
    beeb.type("*TAPE\rCHAIN\"\"\r");
    const bool loaded = beeb.run_until_screen("TAPE OK 42", 20.0);
    INFO(beeb.screen());
    CHECK(loaded);
}

// =============================================================================
// 15. *CAT
// =============================================================================

TEST_CASE("MOS 1.20 *CAT lists the file and runs the tape only while the relay is on",
          "[cassette][mos]") {
    if (!roms_present()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    const std::vector<uint8_t> data(300, 0x11);
    TapeBuilder builder;
    builder.file("LISTED", 0xFFFF3000, 0xFFFF3000, data, {});
    TapeBeeb beeb;
    beeb.insert(builder.uef(), "listed.uef");

    // Playing, but the relay is open until the filing system asks for tape.
    beeb.step(4'000'000);
    CHECK_FALSE(beeb.ula().motor_on());
    CHECK(beeb.deck().position() == 0);

    beeb.type("*TAPE\r*CAT\r");
    const bool listed = beeb.run_until_screen("LISTED", 20.0);
    INFO(beeb.screen());
    CHECK(listed);
    CHECK(beeb.ula().motor_on());
    CHECK(beeb.deck().position() > 0);
}

// =============================================================================
// 16. The deck stopped
// =============================================================================

TEST_CASE("MOS 1.20 searches with the relay on until the deck plays", "[cassette][mos]") {
    const auto tape = program_tape();
    REQUIRE_ROMS_AND_TOOLS(tape);
    TapeBeeb beeb;
    beeb.insert(tape->uef, "tape_ok.uef", /*play=*/false);
    beeb.type("*TAPE\rCHAIN\"\"\r");
    const bool searching = beeb.run_until_screen("Searching", 10.0);
    INFO(beeb.screen());
    CHECK(searching);
    beeb.step(4'000'000);
    CHECK(beeb.ula().motor_on());
    CHECK(beeb.deck().position() == 0);

    beeb.deck().play();
    const bool loaded = beeb.run_until_screen("TAPE OK 42", 30.0);
    INFO(beeb.screen());
    CHECK(loaded);
}
