// Copyright 2026 James Sargent <acheton1984@letterboxes.org>
//
// This file is part of AESulator, a plugin for Beebium.
//
// AESulator is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. AESulator is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with AESulator.
// If not, see <https://www.gnu.org/licenses/>.

// AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
// machine_test.cpp, the plugin in a Beebium machine, in-process
// version 2.0.0, 20 November 2026.

// The AES inside a real Beebium Model B, in one process, for what only that can show: the
// skew contract across a whole boot, and what Beebium does on a held Break. Built by
// acorn-aes-coprocessor/CMakeLists.txt in the Beebium tree, not by run-tests.sh. Tied to the working ROM.

#include "../beebium/AesTube.hpp"
#include "RomCheck.hpp"

#include <beebium/Machines.hpp>
#include <beebium/tube/TubeSocket.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>

using namespace beebium;

namespace {

std::vector<uint8_t> file_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// Skips as Beebium's tests do (SRC), or fails under AES_REQUIRE_ALL. The machine must not be
// built first: it hands the ROM to the board as it starts.
void need_rom() {
    const std::string why = rom_problem(rom_from_env());
    if (why.empty()) return;
    if (getenv("AES_REQUIRE_ALL")) FAIL(why);
    SKIP(why);
}

// DOC, Beebium guide section 8: wraps the coprocessor to see the two skew rules at work.
class SkewWatch : public Coprocessor {
public:
    SkewWatch(AesTube& inner, TubeSocket& socket) : inner_(inner), socket_(socket) {
        socket_.set_register_access_observer([this](uint64_t host_time, uint8_t, bool) {
            accesses++;
            if (socket_.coprocessor_time() != host_time) inexact_accesses++;
        });
    }
    void run_until(uint64_t host_cycle) override {
        if (have_previous_ && host_cycle - previous_ > TubeSocket::MAX_COPROCESSOR_SKEW) long_batches++;
        previous_ = host_cycle;
        have_previous_ = true;
        last_host_time = host_cycle;
        inner_.run_until(host_cycle);
    }
    void pause() override { inner_.pause(); }
    void resume() override { inner_.resume(); }
    bool is_paused() const override { return inner_.is_paused(); }
    void reset() override {
        resets++;
        have_previous_ = false;
        inner_.reset();
    }
    BoardTiming board_timing() const override { return inner_.board_timing(); }

    long accesses = 0, inexact_accesses = 0, long_batches = 0, resets = 0;
    uint64_t last_host_time = 0;

private:
    AesTube& inner_;
    TubeSocket& socket_;
    bool have_previous_ = false;
    uint64_t previous_ = 0;
};

bool screen_has(ModelB& machine, const std::string& text) {
    for (uint16_t at = 0x7C00; at + text.size() <= 0x8000; at++) {
        size_t matched = 0;
        while (matched < text.size() && machine.state().memory.read(uint16_t(at + matched)) == uint8_t(text[matched])) matched++;
        if (matched == text.size()) return true;
    }
    return false;
}

struct AesMachine {
    ModelB machine;
    std::vector<uint8_t> rom = rom_from_env();
    AesOptions options;
    std::unique_ptr<AesTube> tube;
    std::unique_ptr<SkewWatch> watch;

    AesMachine() {
        const std::filesystem::path roms = BEEBIUM_ROM_DIR;
        const auto mos = file_bytes(roms / "acorn-mos_1_20.rom");
        const auto basic = file_bytes(roms / "bbc-basic_2.rom");
        const auto dnfs = file_bytes(roms / "acorn-dnfs_3_02.rom");
        machine.memory().load_mos(mos.data(), mos.size());
        machine.memory().load_basic(basic.data(), basic.size());
        machine.memory().load_sideways_rom(13, dnfs.data(), dnfs.size());
        TubeSocket& socket = machine.state().memory.tube_socket;
        socket.enable();
        machine.reset();
        tube = std::make_unique<AesTube>(options, *socket.tube_ula());
        tube->set_rom(rom);
        watch = std::make_unique<SkewWatch>(*tube, socket);
        tube->reset();
        socket.install_coprocessor(watch.get());
    }
};

}  // namespace

TEST_CASE("AES: banner on the Model B's screen, skew rules kept, ticks exact", "[aes]") {
    need_rom();
    AesMachine m;
    const uint64_t start = m.machine.state().cycle_count;
    m.machine.run(30'000'000);
    CHECK(screen_has(m.machine, expected_banner_words(m.rom, 4)));
    CHECK(m.watch->accesses > 0);
    CHECK(m.watch->inexact_accesses == 0);
    CHECK(m.watch->long_batches == 0);
    // DOC, Beebium guide section 8: exactly the ratio over the run. The host's cycles start
    // at the installation, within one batch of `start`.
    const uint64_t host = m.watch->last_host_time - start;
    CHECK(m.tube->ticks_due() >= 10 * (host - TubeSocket::MAX_COPROCESSOR_SKEW));
    CHECK(m.tube->ticks_due() <= 10 * host);
    // And the ARM really spent them. Behind by at most a held instruction's price: an LDM of 16
    // at the Tube from ROM, 243. Past by at most one instruction: an LDM of 16 from ROM into
    // R15, 19 ROM cycles and 4 refreshes, 606. Arithmetic on the INF defaults, profile 6 and 8a.
    const auto spent = int64_t(m.tube->ticks_spent());
    const auto due = int64_t(m.tube->ticks_due());
    CHECK(spent >= due - 243);
    CHECK(spent <= due + 606);
}

// An open question about Beebium: this records what Beebium does; it says nothing
// about an AES. Prints the counts, so that the answer is in the run's log.
TEST_CASE("AES: what a held Break does (Beebium's behaviour, recorded)", "[aes]") {
    need_rom();
    AesMachine m;
    m.machine.run(30'000'000);
    const long resets_before = m.watch->resets;
    const uint64_t spent_before = m.tube->ticks_spent();

    m.machine.break_down();
    const long resets_on_press = m.watch->resets;
    m.machine.run(4'000'000);
    const uint64_t spent_while_held = m.tube->ticks_spent() - spent_before;
    const long resets_while_held = m.watch->resets;
    m.machine.break_up();
    const long resets_on_release = m.watch->resets;

    printf("held Break: resets before %ld, after press %ld, while held %ld, after release %ld; ARM ticks while held %llu\n",
           resets_before, resets_on_press, resets_while_held, resets_on_release, (unsigned long long)spent_while_held);
    CHECK(resets_on_release > resets_before);

    m.machine.run(30'000'000);
    CHECK(screen_has(m.machine, expected_banner_words(m.rom, 4)));
}

// The version on the plugin's start-up line comes from the build (CMakeLists.txt), from
// manifest.json.
TEST_CASE("AES: built with its own version", "[aes]") {
    const std::string aes = AES_VERSION;
    printf("plugin version line: AESulator %s\n", aes.c_str());
    CHECK(std::regex_match(aes, std::regex("[0-9]+\\.[0-9]+\\.[0-9]+")));
}
