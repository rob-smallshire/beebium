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

// Host-initiated 256-byte Tube transfers, types 6 and 7, through the Tube host
// code at &0406 (issue #204).
//
// A Model B with a 65C02 second processor boots to BASIC on the parasite.
// Host code then claims the Tube (&CE, claim id &0E), runs a type 6 transfer
// (256 bytes parasite to host) and a type 7 (256 bytes host to parasite), and
// releases the claim (&8E) after each, in the shape a filing system uses: the
// interrupts off, and an unpolled loop that reads or writes the R3 data
// register &FEE5 every 10 us (three NOPs of padding), which the protocol
// guarantees the other side keeps up with. The bytes must arrive intact, and
// the host cycles each transfer takes, claim and release included, are pinned
// as regression bounds: they depend on the Tube host code, the parasite's NMI
// handler and the coprocessor's timing, all of which a timing change would move.
//
// Cycles are read at instruction boundaries, from host breakpoints that pause
// the machine, while the coprocessor runs in host time.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/tube/CoprocessorRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "test_econet_helpers.hpp"

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

using namespace beebium;
using namespace beebium::test;

namespace {

constexpr const char* TUBE_ROM_FILENAME = "acorn-tube-6502_1_10.rom";
constexpr const char* DNFS_ROM_FILENAME = "acorn-dnfs_3_02.rom";

// Host addresses.
constexpr uint16_t HOST_CODE = 0x2000;
constexpr uint16_t HOST_PULLED = 0x2400;  // type 6 lands here
constexpr uint16_t HOST_PUSHED = 0x2500;  // type 7 is sent from here

// Parasite addresses.
constexpr uint16_t PARASITE_SOURCE = 0x3000;  // type 6 is sent from here
constexpr uint16_t PARASITE_TARGET = 0x3100;  // type 7 lands here

// A minimal 6502 assembler: bytes, and labels resolved on finish().
class Assembler {
public:
    explicit Assembler(uint16_t origin) : origin_(origin) {}

    uint16_t here() const { return static_cast<uint16_t>(origin_ + bytes_.size()); }
    void label(const std::string& name) { labels_[name] = here(); }
    void op(std::initializer_list<uint8_t> bytes) { bytes_.insert(bytes_.end(), bytes); }

    // An absolute operand (JSR/JMP/LDA abs...), little-endian, resolved later.
    void op_abs(uint8_t opcode, const std::string& target) {
        bytes_.push_back(opcode);
        fixups_.push_back({bytes_.size(), target, false});
        bytes_.push_back(0);
        bytes_.push_back(0);
    }
    void op_abs(uint8_t opcode, uint16_t address) {
        op({opcode, static_cast<uint8_t>(address), static_cast<uint8_t>(address >> 8)});
    }
    // A relative branch to a label.
    void branch(uint8_t opcode, const std::string& target) {
        bytes_.push_back(opcode);
        fixups_.push_back({bytes_.size(), target, true});
        bytes_.push_back(0);
    }
    // LDX #<label, LDY #>label.
    void ldxy(const std::string& target) {
        bytes_.push_back(0xA2);
        lo_fixups_.push_back({bytes_.size(), target});
        bytes_.push_back(0);
        bytes_.push_back(0xA0);
        hi_fixups_.push_back({bytes_.size(), target});
        bytes_.push_back(0);
    }

    std::vector<uint8_t> finish() const {
        auto bytes = bytes_;
        for (const auto& f : fixups_) {
            const uint16_t target = labels_.at(f.target);
            if (f.relative) {
                const int offset = target - (origin_ + static_cast<int>(f.at) + 1);
                REQUIRE(offset >= -128);
                REQUIRE(offset <= 127);
                bytes[f.at] = static_cast<uint8_t>(offset);
            } else {
                bytes[f.at] = static_cast<uint8_t>(target);
                bytes[f.at + 1] = static_cast<uint8_t>(target >> 8);
            }
        }
        for (const auto& f : lo_fixups_) bytes[f.at] = static_cast<uint8_t>(labels_.at(f.target));
        for (const auto& f : hi_fixups_) bytes[f.at] = static_cast<uint8_t>(labels_.at(f.target) >> 8);
        return bytes;
    }
    uint16_t address_of(const std::string& name) const { return labels_.at(name); }

private:
    struct Fixup { size_t at; std::string target; bool relative; };
    struct ByteFixup { size_t at; std::string target; };
    uint16_t origin_;
    std::vector<uint8_t> bytes_;
    std::map<std::string, uint16_t> labels_;
    std::vector<Fixup> fixups_;
    std::vector<ByteFixup> lo_fixups_, hi_fixups_;
};

constexpr uint8_t NOP = 0xEA, SEI = 0x78, INY = 0xC8, DEX = 0xCA;
constexpr uint8_t JSR = 0x20, JMP = 0x4C, BCC = 0x90, BNE = 0xD0;
constexpr uint8_t LDA_ABS = 0xAD, STA_ABS = 0x8D, LDA_ABS_Y = 0xB9, STA_ABS_Y = 0x99;
constexpr uint16_t TUBE_HOST_ENTRY = 0x0406;
constexpr uint16_t R3_DATA = 0xFEE5;
constexpr uint8_t CLAIM = 0xCE, RELEASE = 0x8E;

// Claim the Tube, start a 256-byte transfer of `type` with the parasite
// address at `block`, move the 256 bytes with `transfer`, release.
template <typename Transfer>
void emit_transfer(Assembler& a, const std::string& name, uint8_t type, Transfer transfer) {
    a.label(name + "_claim");
    a.op({0xA9, CLAIM});                        // LDA #&CE
    a.op_abs(JSR, TUBE_HOST_ENTRY);
    a.branch(BCC, name + "_claim");             // until claimed
    a.op({0xA9, type});                         // LDA #type
    a.ldxy(name + "_block");
    a.op_abs(JSR, TUBE_HOST_ENTRY);
    a.op({0xA2, 0x0A});                         // LDX #10: ~24 us before the first byte
    a.label(name + "_settle");
    a.op({DEX});
    a.branch(BNE, name + "_settle");
    a.op({0xA0, 0x00});                         // LDY #0
    a.label(name + "_loop");
    a.op({NOP, NOP, NOP});                      // padding to 10 us a byte
    transfer(a);
    a.op({INY});
    a.branch(BNE, name + "_loop");
    a.op({0xA9, RELEASE});                      // LDA #&8E
    a.op_abs(JSR, TUBE_HOST_ENTRY);
}

struct HostProgram {
    std::vector<uint8_t> bytes;
    uint16_t type6, type7, done;
};

HostProgram host_program() {
    Assembler a(HOST_CODE);
    a.op({SEI});
    a.label("type6");
    emit_transfer(a, "t6", 6, [](Assembler& b) {
        b.op_abs(LDA_ABS, R3_DATA);
        b.op_abs(STA_ABS_Y, HOST_PULLED);
    });
    a.label("type7");
    emit_transfer(a, "t7", 7, [](Assembler& b) {
        b.op_abs(LDA_ABS_Y, HOST_PUSHED);
        b.op_abs(STA_ABS, R3_DATA);
    });
    a.label("done");
    a.op_abs(JMP, "done");
    a.label("t6_block");
    a.op({static_cast<uint8_t>(PARASITE_SOURCE), PARASITE_SOURCE >> 8, 0x00, 0x00});
    a.label("t7_block");
    a.op({static_cast<uint8_t>(PARASITE_TARGET), PARASITE_TARGET >> 8, 0x00, 0x00});
    return {a.finish(), a.address_of("type6"), a.address_of("type7"), a.address_of("done")};
}

std::array<uint8_t, 4096> load_tube_rom() {
    std::ifstream file(std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / TUBE_ROM_FILENAME,
                       std::ios::binary);
    REQUIRE(file.good());
    std::array<uint8_t, 4096> rom{};
    file.read(reinterpret_cast<char*>(rom.data()), rom.size());
    return rom;
}

std::filesystem::path dnfs_filepath() {
    auto filepath = std::filesystem::path(BEEBIUM_ROM_DIR) / DNFS_ROM_FILENAME;
    if (!std::filesystem::exists(filepath)) {
        filepath = std::filesystem::path(BEEBIUM_TEST_ROM_DIR) / DNFS_ROM_FILENAME;
    }
    return filepath;
}

// Host cycles measured on this emulator when the test was written, claim and
// release included. Stratagem measured 6,108 (type 6) and 5,844 (type 7) for
// its own loops of this shape on the B+ 128K and Integra-B: 490 more each way,
// consistent with a loop a couple of cycles a byte dearer. The run is
// deterministic, so the tolerance (about 1%) is for a deliberate small timing
// change, and anything outside it is a change to explain.
constexpr uint64_t TYPE6_CYCLES = 5'618;
constexpr uint64_t TYPE7_CYCLES = 5'354;
constexpr uint64_t TOLERANCE = 56;

}  // namespace

TEST_CASE("Tube types 6 and 7 move 256 bytes each way under a host claim (#204)",
          "[tube][transfer][issue204]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!std::filesystem::exists(std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / TUBE_ROM_FILENAME))
        SKIP("Tube 6502 ROM not available");
    if (!std::filesystem::exists(dnfs_filepath())) SKIP("DNFS ROM not available");

    ModelB machine;
    {
        const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
        auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
        auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
        auto dnfs = load_rom(dnfs_filepath());
        machine.memory().load_mos(mos.data(), mos.size());
        machine.memory().load_basic(basic.data(), basic.size());
        machine.memory().load_sideways_rom(13, dnfs.data(), dnfs.size());
    }
    machine.state().memory.tube_socket.enable();
    machine.reset();
    auto tube_rom = load_tube_rom();
    TubeUla* tube = machine.state().memory.tube_socket.tube_ula();
    REQUIRE(tube != nullptr);
    CoprocessorRunner coprocessor(*tube, tube_rom);
    coprocessor.reset();
    machine.state().memory.tube_socket.install_coprocessor(&coprocessor);

    machine.run(8'000'000);  // to BASIC's prompt, on the parasite
    INFO("Screen:\n" << dump_screen(machine));
    REQUIRE(screen_contains(machine, "Acorn TUBE 6502 64K"));

    // The data each way: distinct patterns, so a dropped or doubled byte shows.
    for (int i = 0; i < 256; ++i) {
        coprocessor.memory().write(static_cast<uint16_t>(PARASITE_SOURCE + i),
                                   static_cast<uint8_t>(i ^ 0x5A));
        machine.write(static_cast<uint16_t>(HOST_PUSHED + i), static_cast<uint8_t>(255 - i));
    }
    const auto program = host_program();
    for (size_t i = 0; i < program.bytes.size(); ++i) {
        machine.write(static_cast<uint16_t>(HOST_CODE + i), program.bytes[i]);
    }

    // Stop at each marker and note the host cycle count there.
    std::map<uint16_t, uint64_t> reached;
    machine.set_breakpoint_hit_callback([&](const BreakpointEntry&, uint16_t pc) {
        if (!reached.count(pc)) {
            reached[pc] = machine.cycle_count();
            machine.pause();
        }
    });
    std::vector<BreakpointEntry> markers;
    uint32_t id = 1;
    for (uint16_t pc : {program.type6, program.type7, program.done}) {
        markers.push_back(BreakpointEntry{id++, pc, static_cast<uint32_t>(pc) + 1});
    }
    machine.set_breakpoint_entries(markers);

    machine.set_pc(HOST_CODE);
    for (int leg = 0; leg < 3 && !reached.count(program.done); ++leg) {
        machine.resume();
        machine.run(200'000);
    }
    REQUIRE(reached.count(program.type6));
    REQUIRE(reached.count(program.type7));
    REQUIRE(reached.count(program.done));

    // The bytes arrived intact both ways.
    for (int i = 0; i < 256; ++i) {
        INFO("byte " << i);
        CHECK(machine.peek(static_cast<uint16_t>(HOST_PULLED + i)) ==
              static_cast<uint8_t>(i ^ 0x5A));
        CHECK(coprocessor.memory().peek(static_cast<uint16_t>(PARASITE_TARGET + i)) ==
              static_cast<uint8_t>(255 - i));
    }

    const uint64_t type6_cycles = reached[program.type7] - reached[program.type6];
    const uint64_t type7_cycles = reached[program.done] - reached[program.type7];
    UNSCOPED_INFO("type 6: " << type6_cycles << " host cycles; type 7: " << type7_cycles);
    CHECK(type6_cycles + TOLERANCE >= TYPE6_CYCLES);
    CHECK(type6_cycles <= TYPE6_CYCLES + TOLERANCE);
    CHECK(type7_cycles + TOLERANCE >= TYPE7_CYCLES);
    CHECK(type7_cycles <= TYPE7_CYCLES + TOLERANCE);
}
