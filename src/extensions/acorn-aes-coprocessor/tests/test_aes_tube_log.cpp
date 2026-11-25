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
// test_aes_tube_log.cpp, tests the counting wrapper
// version 2.0.0, 20 November 2026.

// The counting wrapper: a tool in test builds only (USER, 1 Nov 2026; 2 Nov 2026). These say
// only what it records; what the programs do is read from its logs.

#include "AesBoardTest.hpp"
#include "../beebium/TubeLog.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct LogFixture {
    std::string path = (std::filesystem::temp_directory_path() / "aes-tube-log-test.txt").string();
    AesOptions opt;
    AesTiming timing{opt};
    TubeLog log{path, timing};
    FakeTube tube;
    AesMemory mem{opt, timing, log};
    LogFixture() {
        log.connect(tube, mem);
        mem.write_word(0x8000, 0, false);  // Clears the latch (DOC, HW p.35); RAM, so not logged.
    }
    std::vector<std::string> lines() {
        log.close();
        std::ifstream in(path);
        std::vector<std::string> rows;
        for (std::string row; std::getline(in, row);) rows.push_back(row);
        return rows;
    }
    // A row's fields after its tick: direction, address, register, value, count.
    static std::string fields(const std::string& row) { return row.substr(row.find(' ') + 1); }
};

const uint32_t kBase = 0x01000000;  // SRC, Kernel:79.

}  // namespace

TEST(log_read_and_write, "the log gives direction, the address issued, the register reached, the value") {
    LogFixture f;
    f.log.write_byte(kBase + 12, 0x41, false);
    f.log.read_byte(kBase + 28, false);
    const auto rows = f.lines();
    CHECK_EQ(rows.size(), size_t(3));
    CHECK_EQ(LogFixture::fields(rows[0]) == "W 100000C 3 41 1", true);
}

TEST(log_counts_repeats, "identical accesses in a row are one line with a count, so a poll fills no disc") {
    LogFixture f;
    for (int i = 0; i < 1000; i++) f.log.read_byte(kBase + 8, false);
    f.log.write_byte(kBase + 4, 7, false);
    const auto rows = f.lines();
    CHECK_EQ(LogFixture::fields(rows[0]).substr(LogFixture::fields(rows[0]).size() - 5) == " 1000", true);
    CHECK_EQ(rows.back() == "end 1001", true);
}

TEST(log_end_line_counts_all, "the last line gives the accesses seen, so a missing row can be shown") {
    LogFixture f;
    f.log.read_byte(kBase, false);
    f.log.read_byte(kBase + 4, false);
    f.log.read_byte(kBase + 4, false);
    CHECK_EQ(f.lines().back() == "end 3", true);
}

TEST(log_access_reaching_no_register, "an access to region 01 that reaches no register is logged as register -") {
    LogFixture f;
    f.log.read_byte(0x01800000, false);  // tube.addr_decode: A23 high selects nothing (INF).
    const auto rows = f.lines();
    CHECK_EQ(LogFixture::fields(rows[0]).substr(0, 12) == "R 1800000 - ", true);
}

TEST(log_leaves_other_addresses_alone, "RAM and ROM accesses pass through and are not logged") {
    LogFixture f;
    f.log.write_word(0x2000, 0x12345678, false);
    CHECK_EQ(f.log.read_word(0x2000, false), 0x12345678);
    CHECK_EQ(f.lines().back() == "end 0", true);
}

TEST(log_changes_no_time, "the wrapper adds no time: ticks are as without it") {
    AesOptions opt;
    AesTiming plain_timing(opt);
    FakeTube plain_tube;
    AesMemory plain(opt, plain_timing, plain_tube);
    plain.write_word(0x8000, 0, false);
    plain.read_byte(kBase + 8, false);
    plain.write_byte(kBase + 12, 1, false);
    LogFixture f;
    f.log.read_byte(kBase + 8, false);
    f.log.write_byte(kBase + 12, 1, false);
    CHECK_EQ(f.timing.ticks(), plain_timing.ticks());
}

TEST(log_passes_user_mode_on, "abort strict works in the logged build: the log passes user mode on to the memory (review, 21 Nov 2026)") {
    AesOptions opt;
    opt.abort = AesOptions::MachineAbort::Strict;
    AesTiming timing{opt};
    TubeLog log{"", timing};
    FakeTube tube;
    AesMemory mem{opt, timing, log};
    log.connect(tube, mem);
    log.write_word(0x8000, 0, false);  // Clears the latch (DOC, HW p.35).
    log.user_mode = true;              // The core sets it on the bus it holds: the log.
    log.read_word(4 * 1024 * 1024 + 0x100, false);  // Above 4 MB of RAM: strict aborts it (USER, 31 Oct 2026).
    CHECK_EQ(log.abort_line, true);
}
