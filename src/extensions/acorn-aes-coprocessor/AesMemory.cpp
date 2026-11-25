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
// AesMemory.cpp, the board's memory map, ROM swap and Tube addresses
// version 2.0.0, 20 November 2026.

// AesMemory: the board's memory and I/O as the ARM core sees it, through ArmBus (the core's
// interface to memory, ArmCore.hpp). Every access the core makes comes here, goes to RAM, the ROM,
// the Tube or nowhere by its address, and is passed to AesTiming (the board's clock) to be costed.
//
// The map. Address bits A25 and A24 pick one of four regions:
//   &0000000  RAM, repeating every 4 MB (ram.repeat). After reset the ROM answers reads here
//             instead, until the first write of any kind (the boot latch, boot.latch), so that the
//             ARM can start from address 0.
//   &1000000  the Tube's eight registers, repeating every 32 bytes up to &17FFFFF
//             (tube.addr_decode).
//   &2000000  region 10: nothing answers (region10.read, region10.write).
//   &3000000  the 16 KB Executive ROM, repeating every 16 KB.
//
// Lanes. The 32-bit data bus is four lanes, each one byte wide: lane 0 is bits 0-7 and carries
// the byte whose address ends in binary 00, lane 1 bits 8-15 (01), lane 2 bits 16-23 (10), lane 3
// bits 24-31 (11). A byte access uses the lane its address selects. A device only one byte wide,
// such as the Tube chip, answers on lane 0 alone, and the other lanes are undriven: they read as
// undriven() gives.

#include "AesMemory.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <random>

namespace {

// Address masks: 1 MB; the ARM's 26 address bits; the offset within a region; the Tube region's
// upper half.
const uint32_t kMb = 0x100000;
const uint32_t kAddress = 0x03FFFFFF;
const uint32_t kRegion = 0x00FFFFFF;
const uint32_t kUpperHalf = 0x00800000;
// The Tube's registers and the ROM, at the addresses the Executive uses for them.
const uint32_t kTube = 0x01000000, kTubeBytes = 32;
const uint32_t kRom = 0x03000000;
// What an undriven line reads: all ones, a guess, as nothing is known of it.
const uint32_t kNothing = 0xFFFFFFFF;

// Which of the Tube's eight registers an address picks: address bits A2-A4.
int tube_register(uint32_t addr) { return (addr >> 2) & 7; }

// A word from a byte image, lowest address in the low byte (lane 0; see the map).
uint32_t word_at(const std::vector<uint8_t>& bytes, uint32_t at) {
    return bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | uint32_t(bytes[at + 3]) << 24;
}

}  // namespace

// The emulator's RAM array is 4 MB whatever the machine has, so every RAM address the board can
// produce lands inside it and none needs checking against its size. The ram setting says how much
// is fitted (2 or 4 MB); the rest behaves as missing RAM. Anything but 2 or 4 is a programming
// error, stopped here.
AesMemory::AesMemory(const AesOptions& options, AesTiming& timing, TubePort& tube)
    : opt_(options), timing_(timing), tube_(tube), ram_(4 * kMb), rom_(kRomBytes) {
    const int mb = opt_.ram;
    if (mb != 2 && mb != 4) throw std::invalid_argument("ram: 2 or 4");
    fitted_bytes_ = uint32_t(mb) * kMb;
    power_on();
}

// Reads the ROM image from the file the rom setting names; none is built in.
bool AesMemory::load_rom() { return load_rom(opt_.rom); }

// Reads a ROM image from `path`. False if the file is missing or not exactly 16 KB, so that the
// caller can say why the machine did not start; the old image is then kept.
bool AesMemory::load_rom(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> image{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (image.size() != kRomBytes) return false;
    rom_ = image;
    return true;
}

// Power-on: fills RAM, then resets. By ram.power_on, RAM is filled with zeros, the default, an
// emulator aid, not hardware; or with random bytes from a fixed seed, so that two runs can be
// compared.
void AesMemory::power_on() {
    if (opt_.ram_power_on == AesOptions::RamPowerOn::Random) {
        std::mt19937 random(1);
        for (uint8_t& byte : ram_) byte = uint8_t(random());
    } else {
        std::fill(ram_.begin(), ram_.end(), 0);
    }
    reset();
}

// Reset: sets the boot latch again, so that the ROM answers at address 0, and turns abort strict
// back to lax, because the Executive's start-up probes memory in privileged modes and would
// otherwise be aborted.
void AesMemory::reset() {
    boot_latch_ = true;
    strict_on_ = false;
}

// Where a region-00 address lands in RAM (ram.repeat). AddressLines: address bits A20 and A21
// pick the 1 MB bank and A22 and A23 are ignored. FittedSize: the address wraps at the fitted
// size. An address in a bank that is not fitted comes back marked unfitted; with ram.absent_bank
// set to LowRam it repeats fitted RAM.
AesMemory::Target AesMemory::ram_target(uint32_t addr) const {
    if (opt_.ram_repeat == AesOptions::RamRepeat::FittedSize) {
        return {Kind::Ram, (addr & kRegion) % fitted_bytes_, true};
    }
    const uint32_t offset = addr & (4 * kMb - 1);
    if (offset < fitted_bytes_) return {Kind::Ram, offset, true};
    const bool repeats = opt_.ram_absent_bank == AesOptions::RamAbsentBank::LowRam;
    return {Kind::Ram, repeats ? offset % fitted_bytes_ : offset, false};
}

// Where an address lands in the ROM: only its low 14 bits count, so the ROM repeats every 16 KB.
AesMemory::Target AesMemory::rom_target(uint32_t addr) {
    return {Kind::Rom, (addr & kRegion) & (kRomBytes - 1), true};
}

// Whether an address is outside abort strict's map. abort strict is an emulator aid, not
// hardware: it helps find program faults by allowing only fitted RAM, the Tube's 32 bytes and
// the 16 KB ROM; every repeat counts as outside.
bool AesMemory::outside_strict_map(uint32_t addr) const {
    const bool in_ram = addr < fitted_bytes_;
    const bool in_tube_regs = addr >= kTube && addr < kTube + kTubeBytes;
    const bool in_rom = addr >= kRom && addr < kRom + kRomBytes;
    return !in_ram && !in_tube_regs && !in_rom;
}

// Sends an address to the device that answers it, by the map. `for_write` matters because the boot
// latch sends reads and writes at low addresses to different places.
AesMemory::Target AesMemory::decode(uint32_t addr, bool for_write) const {
    addr &= kAddress;
    // Under abort strict, once checking has started, an address outside its map reaches nothing.
    if (strict_on_ && outside_strict_map(addr)) {
        return {Kind::Empty, addr, false};
    }
    switch (addr >> 24) {
        case 0:
            // While the boot latch is set, reads get the ROM; a write goes to RAM, and clears the
            // latch (note_write).
            return boot_latch_ && !for_write ? rom_target(addr) : ram_target(addr);
        case 1: {
            // tube.addr_decode: LowerHalf, the default, answers only in the region's lower half;
            // in the upper half the access still counts as a Tube cycle but nothing answers.
            const bool lower = !(addr & kUpperHalf);
            return {Kind::Tube, addr, lower || opt_.tube_addr_decode == AesOptions::TubeAddrDecode::WholeRegion};
        }
        case 2: {
            // Region 10 reaches nothing, unless region10.read or region10.write is set to repeat
            // RAM there.
            const bool repeats = for_write
                                     ? opt_.region10_write == AesOptions::Region10Write::RamRepeat
                                     : opt_.region10_read == AesOptions::Region10Read::RamRepeat;
            return repeats ? ram_target(addr) : Target{Kind::Region10, addr, false};
        }
        default:
            return rom_target(addr);
    }
}

// Which price AesTiming charges for a cycle, by where it went.
AesTiming::Place AesMemory::place_of(const Target& target, uint32_t addr) const {
    using Place = AesTiming::Place;
    // While the boot latch is set, every cycle with address bit A24 clear (region 00 and region 10)
    // is priced as a ROM cycle, writes included.
    if (boot_latch_ && !(addr & kA24)) return Place::Rom;
    // Every RAM bank is priced the same.
    if (target.kind == Kind::Ram) return Place::Ram;
    if (target.kind == Kind::Rom) return Place::Rom;
    if (target.kind == Kind::Tube) return Place::Tube;
    return Place::Nothing;
}

// The common part of every read and write: has the cycle costed, and raises the abort line if
// this access should abort.
void AesMemory::note_cycle(const Target& target, uint32_t addr, bool seq, bool for_write) {
    timing_.memory_cycle(seq, place_of(target, addr), addr);

    // abort strict, once checking has started: an access outside its map aborts.
    if (target.kind == Kind::Empty) abort_line = true;
    // region10.read set to Abort: a read of region 10 aborts, unless the abort setting is off.
    if (!for_write && target.kind == Kind::Region10 &&
        opt_.region10_read == AesOptions::Region10Read::Abort && !abort_tied()) abort_line = true;
}

// Starts abort strict's checking at the first access the core makes in user mode. Until then
// everything is lax, so that the start-up and the Executive's memory probe, which run in
// privileged modes, are never aborted.
void AesMemory::watch_mode() {
    if (user_mode && opt_.abort == AesOptions::MachineAbort::Strict) strict_on_ = true;
}

// The start of every read: abort strict's check, the address decoded, the cycle costed.
AesMemory::Target AesMemory::start_read(uint32_t addr, bool seq) {
    watch_mode();
    const Target target = decode(addr, false);
    note_cycle(target, addr, seq, false);
    return target;
}

// What a lane reads when nothing drives it: &FF in every byte, or, with ram.absent_bank set to
// LastBus, the last value on the bus. One value serves every such case: unfitted RAM, empty
// addresses, the Tube's unused lanes. A guess, as nothing is known of it.
uint32_t AesMemory::undriven() const {
    return opt_.ram_absent_bank == AesOptions::RamAbsentBank::LastBus ? last_bus_ : kNothing;
}

// Reads a Tube register: its byte on lane 0, the other lanes undriven (see the map).
uint32_t AesMemory::tube_lanes(uint32_t addr) {
    return (undriven() & ~0xFFu) | tube_.read(tube_register(addr));
}

// The word a read sees from anything but the Tube, whose reads have a side effect and are made
// apart.
uint32_t AesMemory::read_lanes(const Target& target) const {
    const uint32_t at = target.offset & ~3u;
    if (target.kind == Kind::Ram && target.fitted) return word_at(ram_, at);
    if (target.kind == Kind::Rom && target.fitted) return word_at(rom_, at);
    // Unfitted RAM with ram.absent_bank set to LowRam reads RAM again, at its repeat.
    if (target.kind == Kind::Ram && opt_.ram_absent_bank == AesOptions::RamAbsentBank::LowRam) {
        return word_at(ram_, at);
    }
    return undriven();
}

// A word read, for ArmBus. A word access to the Tube is suspect (see suspect()).
uint32_t AesMemory::read_word(uint32_t addr, bool seq) {
    const Target target = start_read(addr, seq);
    uint32_t value;
    if (target.kind == Kind::Tube) suspect();
    if (target.kind == Kind::Tube && target.fitted) {
        // tube.width: LowByte gives the register's byte on lane 0 and the rest undriven;
        // Undefined gives all lanes undriven.
        const uint32_t lanes = tube_lanes(addr);
        value = opt_.tube_width == AesOptions::TubeWidth::LowByte ? lanes : undriven();
    } else {
        value = read_lanes(target);
    }
    // Remembered as the bus's last value, which undriven() can return later.
    last_bus_ = value;
    return value;
}

// A byte read, for ArmBus: the word on the bus, then the lane the address selects (see the map).
// The Tube register is read whatever the address's low two bits, so a byte read from the Tube at
// an address not ending in 00 gets an undriven lane.
uint32_t AesMemory::read_byte(uint32_t addr, bool seq) {
    const Target target = start_read(addr, seq);
    const uint32_t word = target.kind == Kind::Tube && target.fitted ? tube_lanes(addr) : read_lanes(target);
    // Remembered as the bus's last value, which undriven() can return later.
    last_bus_ = word;
    return (word >> (8 * (target.offset & 3))) & 0xFF;
}

// boot.latch: notes a write's effect on the boot latch, and marks a ROM write as suspect.
// AnyWrite, the default, clears the latch on any write, Tube and ROM included; RamWritesOnly only
// on a RAM write.
void AesMemory::note_write(const Target& target) {
    if (opt_.boot_latch == AesOptions::BootLatch::AnyWrite || target.kind == Kind::Ram) {
        boot_latch_ = false;
    }
    if (target.kind == Kind::Rom) suspect();
}

// The start of every write: abort strict's check, the address decoded, the cycle costed, the
// boot latch noted.
AesMemory::Target AesMemory::start_write(uint32_t addr, bool seq) {
    watch_mode();
    const Target target = decode(addr, true);
    note_cycle(target, addr, seq, true);
    note_write(target);
    return target;
}

// A word write, for ArmBus. Only a Tube register or fitted RAM keeps it; a write to unfitted RAM,
// the ROM, region 10 or an empty address is lost. A word access to the Tube is suspect.
void AesMemory::write_word(uint32_t addr, uint32_t value, bool seq) {
    const Target target = start_write(addr, seq);
    if (target.kind == Kind::Tube) {
        suspect();
        // tube.width: LowByte sends the value's low byte to the register; Undefined sends the
        // undriven value's.
        const bool low_byte = opt_.tube_width == AesOptions::TubeWidth::LowByte;
        if (target.fitted) tube_.write(tube_register(addr), uint8_t(low_byte ? value : undriven()));
    } else if (target.kind == Kind::Ram && target.fitted) {
        // ram.absent_write: only fitted RAM keeps a write (see the function's comment).
        const uint32_t at = target.offset & ~3u;
        for (int i = 0; i < 4; i++) ram_[at + i] = uint8_t(value >> (8 * i));
    }
    // Remembered as the bus's last value, which undriven() can return later.
    last_bus_ = value;
}

// A byte write, for ArmBus. Only a Tube register or fitted RAM keeps it, as for a word.
void AesMemory::write_byte(uint32_t addr, uint8_t value, bool seq) {
    const Target target = start_write(addr, seq);
    if (target.kind == Kind::Tube) {
        if (target.fitted) tube_.write(tube_register(addr), value);
    } else if (target.kind == Kind::Ram && target.fitted) {
        ram_[target.offset] = value;
    }
    // A byte write puts the byte on all four lanes; remembered as the bus's last value, which
    // undriven() can return later.
    last_bus_ = value * 0x01010101u;
}

// An internal cycle, for ArmBus: it takes time like any other, and where its address points
// decides whether a refresh may start, so it is costed with its place.
void AesMemory::internal(uint32_t addr) {
    timing_.internal_cycle(place_of(decode(addr, false), addr), addr);
}

// Notes a suspect access: a write to the ROM, or a word access to the Tube, which a program
// probably did not mean. By access.check, an emulator aid to help find program faults: Off does
// nothing; Log counts it; Abort counts it and aborts the access, unless the abort setting is off.
void AesMemory::suspect() {
    if (opt_.access_check == AesOptions::MachineCheck::Off) return;
    suspect_accesses_++;
    if (opt_.access_check == AesOptions::MachineCheck::Abort && !abort_tied()) abort_line = true;
}

// The debugger's look at one byte, as the core would read it, but with no time passing and
// nothing changed: the Tube chip is looked at, not read, because reading a Tube register takes its
// byte.
uint8_t AesMemory::peek(uint32_t addr) const {
    const Target target = decode(addr, false);
    const uint32_t lanes = target.kind == Kind::Tube && target.fitted
                               ? (undriven() & ~0xFFu) | tube_.peek(tube_register(addr))
                               : read_lanes(target);
    return (lanes >> (8 * (target.offset & 3))) & 0xFF;
}

// The debugger's write of one byte. Where the core's write would reach RAM, the byte goes there,
// with no time, no change to the latch and nothing on the bus. Anywhere else it is refused,
// because the ROM cannot be written and a write to a Tube register would act on the host's side.
bool AesMemory::poke(uint32_t addr, uint8_t value) {
    const Target target = decode(addr, true);
    if (target.kind != Kind::Ram || !target.fitted) return false;
    ram_[target.offset] = value;
    return true;
}
