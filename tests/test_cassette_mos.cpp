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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
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

// =============================================================================
// 7 and 8 (round 6). SAVE to a blank tape and CHAIN it back
// =============================================================================

namespace {

struct SavedChunk {
    uint16_t id;
    std::vector<uint8_t> body;
};

std::vector<SavedChunk> chunks_of(const std::vector<uint8_t>& uef) {
    std::vector<SavedChunk> chunks;
    for (size_t pos = 12; pos + 6 <= uef.size();) {
        const auto id = static_cast<uint16_t>(uef[pos] | (uef[pos + 1] << 8));
        const uint32_t length = uef[pos + 2] | (uef[pos + 3] << 8) | (uef[pos + 4] << 16) |
                                (static_cast<uint32_t>(uef[pos + 5]) << 24);
        pos += 6;
        chunks.push_back({id, {uef.begin() + static_cast<std::ptrdiff_t>(pos),
                               uef.begin() + static_cast<std::ptrdiff_t>(pos + length)}});
        pos += length;
    }
    return chunks;
}

// Run until the motor relay has closed and then stayed open for half a second:
// the end of a SAVE.
bool run_until_relay_rests(TapeBeeb& beeb, double seconds) {
    bool closed = false;
    double open_for = 0.0;
    constexpr uint64_t CHUNK = 100'000;  // 50 ms
    for (double t = 0.0; t < seconds; t += 0.05) {
        beeb.step(CHUNK);
        if (beeb.deck().published().relay_closed) {
            closed = true;
            open_for = 0.0;
        } else if (closed && (open_for += 0.05) >= 0.5) {
            return true;
        }
    }
    return false;
}

// Load tape_ok, SAVE it as "OK" to a copy of the C15 blank with the deck
// recording, and return the written file's bytes; CHAIN it back as well.
std::vector<uint8_t> save_and_chain_back(const std::vector<uint8_t>& program_uef, bool baud_300,
                                         const std::filesystem::path& blank_filepath) {
    TapeBeeb beeb;
    beeb.insert(program_uef, "tape_ok.uef");
    beeb.type(baud_300 ? "*TAPE3\rCHAIN\"\"\r" : "*TAPE\rCHAIN\"\"\r");
    REQUIRE(beeb.run_until_screen("TAPE OK 42", 40.0));
    beeb.deck().eject();

    beeb.deck().insert(open_tape_file(blank_filepath), blank_filepath.filename().string(), "", blank_filepath);
    // Record pressed first, with the relay still open, as a user at a deck
    // does; then SAVE, and RETURN at the prompt.
    beeb.deck().record();
    CHECK(beeb.deck().transport() == CassetteDeck::Transport::Recording);
    beeb.type("CLS\rSAVE \"PROGRAM\"\r");
    REQUIRE(beeb.run_until_screen("RECORD then RETURN", 5.0));
    // A person takes a moment to press RETURN; the motor is on meanwhile.
    beeb.step(4'600'000);
    beeb.type("\r");
    REQUIRE(run_until_relay_rests(beeb, 60.0));
    INFO(beeb.screen());
    beeb.deck().stop();
    CHECK(beeb.deck().transport() == CassetteDeck::Transport::Stopped);

    // Back to the start, and load it.
    beeb.deck().seek(0);
    beeb.deck().play();
    beeb.type("NEW\r?&70=0\rCLS\rCHAIN \"PROGRAM\"\r");
    const bool loaded = beeb.run_until_screen("TAPE OK 42", 60.0);
    INFO(beeb.screen());
    CHECK(loaded);
    CHECK(beeb.machine.peek(0x70) == 42);
    return read_file(blank_filepath);
}

void check_saved_shape(const std::vector<uint8_t>& written, bool baud_300) {
    const auto chunks = chunks_of(written);
    REQUIRE_FALSE(chunks.empty());
    CHECK(chunks[0].id == 0x0000);
    CHECK(std::string(chunks[0].body.begin(), chunks[0].body.end() - 1) == "Beebium");

    // Every chunk after the origin is silence, carrier, &0117 or bytes: MOS
    // 1.20's SAVE frames as 8N1 throughout. Its leader carries one dummy &AA
    // byte, as UEF's &0111 describes.
    double leader_cycles = 0.0;
    double tail_cycles = 0.0;
    std::optional<size_t> data_at;
    bool baud_300_before_data = false;
    for (size_t i = 1; i < chunks.size(); ++i) {
        const auto& c = chunks[i];
        INFO("chunk " << i << " is &" << std::hex << c.id);
        CHECK((c.id == 0x0110 || c.id == 0x0100 || c.id == 0x0112 || c.id == 0x0116 || c.id == 0x0117));
        const double cycles = c.id == 0x0110 ? (c.body[0] | (c.body[1] << 8)) : 0.0;
        if (c.id == 0x0117 && !data_at) baud_300_before_data = (c.body[0] | (c.body[1] << 8)) == 300;
        if (c.id == 0x0100 && !data_at && c.body == std::vector<uint8_t>{0xAA}) {
            // The leader proper follows the last dummy byte; the carrier before
            // it is the wait at "RECORD then RETURN".
            leader_cycles = 0.0;
            continue;
        }
        if (c.id == 0x0100 && !data_at) {
            data_at = i;
            REQUIRE(c.body.size() > 9);
            CHECK(c.body[0] == 0x2A);
            CHECK(std::string(c.body.begin() + 1, c.body.begin() + 9) == std::string("PROGRAM\0", 8));
        }
        if (!data_at) leader_cycles += cycles;
        else tail_cycles += cycles;
    }
    REQUIRE(data_at);
    CHECK(baud_300_before_data == baud_300);
    // About 5.1 s of 2400 Hz carrier before the block and after it.
    CHECK(leader_cycles / 2400.0 > 4.5);
    CHECK(leader_cycles / 2400.0 < 5.6);
    CHECK(tail_cycles / 2400.0 > 4.5);
    CHECK(tail_cycles / 2400.0 < 5.6);
}

}  // namespace

TEST_CASE("MOS 1.20 saves to a blank tape and chains it back, here and in a fresh machine",
          "[cassette][mos][recording]") {
    const auto tape = program_tape();
    REQUIRE_ROMS_AND_TOOLS(tape);
    beebium::test::ScopedTempDir dir("beebium_cassette_save_");
    const auto blank_filepath = dir.path() / "saved.uef";
    std::filesystem::copy_file(std::filesystem::path(BEEBIUM_BLANK_TAPES_DIR) / "Blank C15 (15 minutes).uef",
                               blank_filepath);

    const auto written = save_and_chain_back(tape->uef, false, blank_filepath);
    check_saved_shape(written, false);

    TapeBeeb fresh;
    fresh.deck().insert(open_tape_file(blank_filepath), "saved.uef");
    fresh.deck().play();
    fresh.type("*TAPE\rCHAIN\"\"\r");
    const bool loaded = fresh.run_until_screen("TAPE OK 42", 40.0);
    INFO(fresh.screen());
    CHECK(loaded);
}

TEST_CASE("MOS 1.20 saves at 300 baud after *TAPE3 and the file says so", "[cassette][mos][recording]") {
    const auto tape = program_tape({.baud = 300});
    REQUIRE_ROMS_AND_TOOLS(tape);
    beebium::test::ScopedTempDir dir("beebium_cassette_save_");
    const auto blank_filepath = dir.path() / "saved300.uef";
    std::filesystem::copy_file(std::filesystem::path(BEEBIUM_BLANK_TAPES_DIR) / "Blank C15 (15 minutes).uef",
                               blank_filepath);

    const auto written = save_and_chain_back(tape->uef, true, blank_filepath);
    check_saved_shape(written, true);

    TapeBeeb fresh;
    fresh.deck().insert(open_tape_file(blank_filepath), "saved300.uef");
    fresh.deck().play();
    fresh.type("*TAPE3\rCHAIN\"\"\r");
    const bool loaded = fresh.run_until_screen("TAPE OK 42", 60.0);
    INFO(fresh.screen());
    CHECK(loaded);
}

// =============================================================================
// 7 (round 7). tape_ok as a WAV, through the front end
// =============================================================================

namespace {

std::optional<std::vector<uint8_t>> program_wav(const beebium::test::WavOptions& wav) {
    const auto program = tokenised("tape_ok");
    if (!program) return std::nullopt;
    TapeBuilder builder;
    builder.origin("beebium tests").file("TAPEOK", BASIC_LOAD, BASIC_EXEC, *program, {});
    return builder.wav(wav);
}

// Tokenise and skip in the test case's body (through REQUIRE_ROMS_AND_TOOLS),
// never inside an assertion's expression: a SKIP thrown from within CHECK is
// reported as an unexpected exception.
bool loads_from_wav(const std::vector<uint8_t>& tape, const char* name) {
    TapeBeeb beeb;
    beeb.insert(tape, name);
    beeb.type("*TAPE\rCHAIN\"\"\r");
    const bool loaded = beeb.run_until_screen("TAPE OK 42", 40.0);
    if (!loaded) INFO(name << " did not load; the screen reads:\n" << beeb.screen());
    return loaded;
}

}  // namespace

// The sines are at 90 degrees, a cosine a half-cycle: the phase a playback
// head's differentiation gives a recording.
TEST_CASE("MOS 1.20 loads tape_ok from a WAV of sines", "[cassette][mos][wav]") {
    beebium::test::WavOptions wav;
    wav.amplitude = 0.5;
    wav.shape = beebium::test::WavOptions::Shape::Shifted90;
    const auto tape = program_wav(wav);
    REQUIRE_ROMS_AND_TOOLS(tape);
    CHECK(loads_from_wav(*tape, "tape_ok.wav"));
}

TEST_CASE("MOS 1.20 loads tape_ok from a low-level, noisy WAV", "[cassette][mos][wav]") {
    beebium::test::WavOptions wav;
    wav.amplitude = 0.18;
    wav.noise = 0.02;
    wav.shape = beebium::test::WavOptions::Shape::Shifted90;
    const auto tape = program_wav(wav);
    REQUIRE_ROMS_AND_TOOLS(tape);
    CHECK(loads_from_wav(*tape, "tape_ok_low.wav"));
}

TEST_CASE("MOS 1.20 loads tape_ok from a WAV of square waves", "[cassette][mos][wav]") {
    beebium::test::WavOptions wav;
    wav.amplitude = 0.9;
    wav.shape = beebium::test::WavOptions::Shape::Square;
    const auto tape = program_wav(wav);
    REQUIRE_ROMS_AND_TOOLS(tape);
    CHECK(loads_from_wav(*tape, "tape_ok_square.wav"));
}

// The model's prediction, unverified on hardware: sines at 0 degrees do not
// load. The front end moves the low tone by about +65 degrees and the high by
// about -5, so at each change of tone the first low half-cycle falls under the
// separator's 350-clock timeout and the first high one over it.
TEST_CASE("MOS 1.20 does not load tape_ok from a WAV of sines at 0 degrees", "[cassette][mos][wav]") {
    beebium::test::WavOptions wav;
    wav.amplitude = 0.5;
    wav.shape = beebium::test::WavOptions::Shape::Sine;
    const auto tape = program_wav(wav);
    REQUIRE_ROMS_AND_TOOLS(tape);
    CHECK_FALSE(loads_from_wav(*tape, "tape_ok_0.wav"));
}


// =============================================================================
// SAVE on the timetable that found two recording faults
// =============================================================================

namespace {

// A program SAVEd as "PROG" to a copy of the C15 blank, with record pressed
// and RETURN typed at moments set by `k` (a timetable that, before the idle
// transmitter's restart, doubled the sync byte at k = 19). Returns the beeb,
// stopped, and the written file's chunks.
struct ProgSave {
    std::unique_ptr<TapeBeeb> beeb = std::make_unique<TapeBeeb>();
    std::vector<SavedChunk> chunks;
    std::vector<uint8_t> block;
};

ProgSave save_prog(int k, const std::filesystem::path& blank_filepath) {
    ProgSave save;
    TapeBeeb& beeb = *save.beeb;
    beeb.step(4'000'000);
    beeb.type("10 PRINT \"HELLO FROM TAPE\"\r*TAPE\r");
    beeb.step(2'000'000 + 137'000 * static_cast<uint64_t>(k));
    beeb.deck().insert(open_tape_file(blank_filepath), blank_filepath.filename().string(), "", blank_filepath);
    beeb.deck().record();
    beeb.type("SAVE \"PROG\"\r");
    beeb.step(7'000'000 + 73'000 * static_cast<uint64_t>(k));
    beeb.type("\r");
    beeb.step(36'000'000);
    beeb.deck().stop();
    save.chunks = chunks_of(read_file(blank_filepath));
    for (const auto& c : save.chunks) {
        if (c.id == 0x0100 && c.body.size() > 6) save.block = c.body;
    }
    return save;
}

std::filesystem::path blank_copy(const beebium::test::ScopedTempDir& dir, const std::string& name) {
    const auto filepath = dir.path() / name;
    std::filesystem::copy_file(std::filesystem::path(BEEBIUM_BLANK_TAPES_DIR) / "Blank C15 (15 minutes).uef", filepath);
    return filepath;
}

}  // namespace

TEST_CASE("A SAVE whose sync byte the transmitter took at once records its header whole",
          "[cassette][mos][recording]") {
    // Before an idle transmitter waited a full bit for a written byte, this
    // timetable had MOS 1.20 send the sync byte twice and lose the name's
    // first letter: the transmit interrupt came inside the gap the block
    // writer leaves between sending &2A and placing the next byte.
    if (!roms_present()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    beebium::test::ScopedTempDir dir("beebium_cassette_prog_");
    const auto save = save_prog(19, blank_copy(dir, "k19.uef"));
    REQUIRE(save.block.size() >= 6);
    CHECK(std::vector<uint8_t>(save.block.begin(), save.block.begin() + 6) ==
          std::vector<uint8_t>{0x2A, 'P', 'R', 'O', 'G', 0x00});
}

TEST_CASE("A SAVE that ends with a stray FF frame still loads", "[cassette][mos][recording]") {
    // On this timetable the last byte MOS writes before its master reset gets
    // its start bit onto the line, as it can on the machine: the recording ends
    // with an &FF frame after the data CRC, which loading never reads.
    if (!roms_present()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    beebium::test::ScopedTempDir dir("beebium_cassette_prog_");
    auto save = save_prog(3, blank_copy(dir, "k3.uef"));
    REQUIRE(save.block.size() == 54);
    CHECK(save.block.back() == 0xFF);
    TapeBeeb& beeb = *save.beeb;
    beeb.deck().seek(0);
    beeb.deck().play();
    beeb.type("NEW\rCLS\rCHAIN \"PROG\"\r");
    const bool loaded = beeb.run_until_screen("HELLO FROM TAPE", 60.0);
    INFO(beeb.screen());
    CHECK(loaded);
    CHECK(beeb.screen().find("Data?") == std::string::npos);
}

// =============================================================================
// The signal map of a tape recorded on (round 9)
// =============================================================================

namespace {

// Wait, in wall time, for the deck's signal map to satisfy `done`.
template <typename Done>
beebium::SignalMapper::Map wait_for_map(const beebium::CassetteDeck& deck, Done done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto map = deck.signal_map();
    while (!done(map) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        map = deck.signal_map();
    }
    return map;
}

}  // namespace

TEST_CASE("A recording's signal map shows where it landed", "[cassette][mos][recording][signal_map]") {
    if (!roms_present()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    beebium::test::ScopedTempDir dir("beebium_cassette_map_");
    const auto blank_filepath = blank_copy(dir, "mapped.uef");
    TapeBeeb beeb;
    beeb.step(4'000'000);
    beeb.type("10 PRINT \"HELLO FROM TAPE\"\r*TAPE\r");
    beeb.step(2'000'000);
    beeb.deck().insert(open_tape_file(blank_filepath), blank_filepath.filename().string(), "", blank_filepath);

    // A blank C15: fifteen minutes of silence.
    const auto blank = wait_for_map(beeb.deck(), [](const auto& m) { return m.ready; });
    REQUIRE(blank.ready);
    REQUIRE(blank.levels.size() == 900);
    CHECK(std::all_of(blank.levels.begin(), blank.levels.end(), [](uint8_t level) { return level == 0; }));

    beeb.deck().record();
    beeb.type("SAVE \"PROG\"\r");
    beeb.step(7'000'000);
    beeb.type("\r");
    beeb.step(36'000'000);
    beeb.deck().stop();
    const auto end_second = static_cast<size_t>(static_cast<double>(beeb.deck().position()) / beebium::ULA_CLOCK_HZ);

    const auto recorded = wait_for_map(beeb.deck(), [&](const auto& m) { return m.ready && m.generation > blank.generation; });
    REQUIRE(recorded.ready);
    CHECK(recorded.generation == blank.generation + 1);
    REQUIRE(recorded.levels.size() == 900);
    // The SAVE's leader and blocks: several seconds of signal before the head
    // stopped, and silence beyond.
    const auto loud = std::count_if(recorded.levels.begin(), recorded.levels.begin() + static_cast<std::ptrdiff_t>(end_second + 1),
                                    [](uint8_t level) { return level >= 128; });
    CAPTURE(end_second, loud);
    CHECK(loud >= 5);
    CHECK(std::all_of(recorded.levels.begin() + static_cast<std::ptrdiff_t>(end_second + 2), recorded.levels.end(),
                      [](uint8_t level) { return level == 0; }));
}
