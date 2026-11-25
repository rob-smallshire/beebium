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
// AesDebug.cpp, the debugger's view of the AES
// version 2.0.0, 20 November 2026.

// AesDebug (AesDebug.hpp): the debugger's link to the AES.

#include "AesDebug.hpp"

#include <iterator>
#include <stdexcept>
#include <string>

namespace {

using beebium::cpu::RegisterRole;

// One banked register the debugger shows, named, with its mode and number.
struct Bank {
    const char* name;
    int mode;  // 0 user, 1 FIQ, 2 IRQ, 3 SVC.
    int n;
};
// The banked registers the debugger shows besides the current mode's, so that every one can be
// read: FIQ has R10-R14 of its own, IRQ and SVC R13-R14 (as kBank in ArmCore.cpp).
const Bank kBanks[] = {
    {"R10_usr", 0, 10}, {"R11_usr", 0, 11}, {"R12_usr", 0, 12}, {"R13_usr", 0, 13}, {"R14_usr", 0, 14},
    {"R10_fiq", 1, 10}, {"R11_fiq", 1, 11}, {"R12_fiq", 1, 12}, {"R13_fiq", 1, 13}, {"R14_fiq", 1, 14},
    {"R13_irq", 2, 13}, {"R14_irq", 2, 14}, {"R13_svc", 3, 13}, {"R14_svc", 3, 14},
};

// The debugger's memory regions: RAM from &0, the Tube's 32 bytes at &1000000, the ROM at
// &3000000. Only the fitted RAM and one copy of each are shown, though the ARM sees repeats,
// because a repeat adds nothing to look at.
const uint32_t kTubeBase = 0x1000000, kTubeBytes = 32, kRomBase = 0x3000000;

// The CPU descriptor: the ARM1's 26-bit little-endian addresses, R0 to R15, the PSR with its flag
// names, every banked register, and the IRQ and FIQ signals.
beebium::cpu::CpuDescriptor describe() {
    beebium::cpu::CpuDescriptor d;
    d.family = "arm1";
    d.address_bits = 26;
    d.little_endian = true;
    for (int n = 0; n < 15; n++) d.registers.push_back({"R" + std::to_string(n), 32, RegisterRole::None, {}});
    d.registers.push_back({"R15", 32, RegisterRole::ProgramCounter, {}});
    // The PSR is R15's top six bits and bottom two (Software Manual p.1). It is shown read-only.
    std::vector<std::string> flags(32);
    flags[0] = "M0", flags[1] = "M1", flags[26] = "F", flags[27] = "I";
    flags[28] = "V", flags[29] = "C", flags[30] = "Z", flags[31] = "N";
    d.registers.push_back({"PSR", 32, RegisterRole::Flags, flags});
    for (const Bank& bank : kBanks) d.registers.push_back({bank.name, 32, RegisterRole::None, {}});
    d.signals = {"IRQ", "FIQ"};
    return d;
}

}  // namespace

// The descriptor, built once.
const beebium::cpu::CpuDescriptor& AesDebug::cpu_descriptor() const {
    static const beebium::cpu::CpuDescriptor d = describe();
    return d;
}

// One register by its index in the descriptor: R0-R14 of the current mode, R15, the PSR, or a
// banked register.
uint64_t AesDebug::register_value(size_t index) const {
    ArmCore& core = tube_.core();
    const uint32_t r15 = core.r15();
    if (index < kR15) return core.reg_in_mode(r15 & 3, int(index));
    // R15 holds the fetch address, 8 past the instruction about to run; the debugger shows the
    // instruction about to run, which is what a user wants to see.
    if (index == kR15) return ((r15 & ArmCore::kPcMask) - 8) & ArmCore::kPcMask;
    if (index == kPsr) return r15 & ~ArmCore::kPcMask;
    if (index < kFirstBank + std::size(kBanks)) return bank_register(index);
    return 0;
}

// Sets one register by its index. Setting R15 moves the PC; the PSR cannot be set.
void AesDebug::set_register_value(size_t index, uint64_t value) {
    ArmCore& core = tube_.core();
    const uint32_t v = uint32_t(value);
    if (index < kR15) {
        core.reg_in_mode(core.r15() & 3, int(index)) = v;
    } else if (index == kR15) {
        // The two words are peeked, not fetched, so moving the PC takes no cycle, no tick and no
        // Tube access.
        const uint32_t at = v & ArmCore::kPcMask;
        core.load_pipeline(at, peek_word(at), peek_word(at + 4));
    } else if (index == kPsr) {
        // Read-only: the flags change only by running code.
    } else if (index < kFirstBank + std::size(kBanks)) {
        bank_register(index) = v;
    }
}

// IRQ and FIQ as the debugger shows them: the line as run_until last set it, once an
// instruction (AesTube.cpp); pending if the line is up and not masked by I or F; in the handler
// while the ARM is in that interrupt's mode.
beebium::cpu::SignalStateValue AesDebug::signal_state(size_t index) const {
    const ArmCore& core = tube_.core();
    const bool irq = index == 0;
    const bool line = irq ? core.irq_line() : core.fiq_line();
    const uint32_t mask = irq ? ArmCore::kI : ArmCore::kF;
    return {line, line && !(core.r15() & mask), (core.r15() & 3) == (irq ? 2u : 1u)};
}

// The three regions, with what the debugger may do in each: only ram can be written.
std::vector<beebium::MemoryRegionDescriptor> AesDebug::get_memory_regions() const {
    using F = beebium::RegionFlags;
    const AesMemory& memory = tube_.memory();
    const F shown = F::Readable | F::Populated;
    // RAM is shown as not active while the boot latch is set, because the ARM's reads at low
    // addresses then reach the ROM, not RAM.
    return {
        {"ram", 0, memory.ram_bytes(), shown | F::Writable | (memory.boot_latch_set() ? F::None : F::Active)},
        {"tube", kTubeBase, kTubeBytes, shown | F::Active},
        {"rom", kRomBase, AesMemory::kRomBytes, shown | F::Active},
    };
}

// One byte of a region. ram is RAM itself, latch or not; tube and rom are as the ARM sees them,
// by peek.
uint8_t AesDebug::peek_region(std::string_view name, uint32_t address) const {
    const AesMemory& memory = tube_.memory();
    if (name == "ram") {
        beebium::validate_region_address({name, 0, memory.ram_bytes()}, address);
        return memory.peek_ram(address);
    }
    if (name == "tube") {
        beebium::validate_region_address({name, kTubeBase, kTubeBytes}, address);
    } else if (name == "rom") {
        beebium::validate_region_address({name, kRomBase, AesMemory::kRomBytes}, address);
    } else {
        throw std::invalid_argument("no region '" + std::string(name) + "' on the AES");
    }
    return memory.peek(address);
}

// Writes one byte of a region. Only ram can be written; any other region throws, which Beebium
// reports.
void AesDebug::write_region(std::string_view name, uint32_t address, uint8_t value) {
    if (name != "ram") {
        peek_region(name, address);  // says if the name or the address is wrong
        throw std::invalid_argument("region '" + std::string(name) + "' cannot be written by the debugger");
    }
    beebium::validate_region_address({name, 0, tube_.memory().ram_bytes()}, address);
    tube_.memory().poke_ram(address, value);
}

// A word as the core would read it, with nothing changed, lowest address in the low byte; for
// moving the PC.
uint32_t AesDebug::peek_word(uint32_t at) const {
    const AesMemory& memory = tube_.memory();
    return memory.peek(at) | memory.peek(at + 1) << 8 | memory.peek(at + 2) << 16 | uint32_t(memory.peek(at + 3)) << 24;
}

// The banked register a descriptor index names.
uint32_t& AesDebug::bank_register(size_t index) const {
    const Bank& bank = kBanks[index - kFirstBank];
    return tube_.core().reg_in_mode(bank.mode, bank.n);
}

// Runs fn with the AES halted, through Beebium's quiescer, which halts the host thread that runs
// the AES. With none set, as in the tests, fn runs at once.
void AesDebug::with_execution_stopped(const std::function<void()>& fn) {
    if (quiescer_) {
        quiescer_(fn);
    } else {
        fn();
    }
}

// The breakpoint list, as Beebium sets it. Beebium sets the entries with the emulation thread
// halted, so they can be set directly here. The hooks are attached only while there is something
// to check, so that with none set the ARM runs at full speed and nothing is called.
void AesDebug::set_breakpoint_entries(std::vector<beebium::BreakpointEntry> entries) {
    breakpoints_ = std::move(entries);
    attach_hooks();
}

// The watchpoint list, likewise.
void AesDebug::set_watchpoint_entries(std::vector<beebium::WatchpointEntry> entries) {
    watchpoints_ = std::move(entries);
    attach_hooks();
}

// Connects the hooks while anything is set: AesTube asks before each instruction while any
// breakpoint or watchpoint is set, and the core tells of loads and stores while any watchpoint is.
void AesDebug::attach_hooks() {
    const bool any = !breakpoints_.empty() || !watchpoints_.empty();
    tube_.set_stops(any ? this : nullptr);
    tube_.core().set_watch(watchpoints_.empty() ? nullptr : this);
}

// Resuming from a breakpoint: the instruction at the PC now is not checked when it next runs, or
// the ARM would stop on it again at once.
void AesDebug::step_over_breakpoints_once() {
    skip_once_ = true;
    skip_pc_ = uint32_t(register_value(kR15));
}

// Called before each instruction while breakpoints are set. The callback is Beebium's: it counts
// the hit, tests any condition and pauses the AES. Several entries at one address are each told.
// The skip lasts until the instruction at skip_pc_ runs: an interrupt taken on resuming runs its
// handler first, and the return to that instruction must not stop again (review, 21 Nov 2026).
void AesDebug::before_instruction(uint32_t pc) {
    if (skip_once_ && pc == skip_pc_) {
        skip_once_ = false;
        return;
    }
    if (!breakpoint_hit_) return;
    for (const beebium::BreakpointEntry& bp : breakpoints_) {
        if (pc >= bp.start && pc < bp.end) breakpoint_hit_(bp, pc);
    }
}

// Called by the core for each load and store while watchpoints are set. Each byte the access
// moved is checked with its own address, lowest address in the low byte, because a watchpoint
// may cover only part of a word.
void AesDebug::data_access(uint32_t addr, uint32_t value, int bytes, bool is_write) {
    if (!watchpoint_hit_) return;
    const uint8_t kind = is_write ? beebium::WATCH_WRITE : beebium::WATCH_READ;
    for (int i = 0; i < bytes; i++) {
        const uint32_t at = addr + uint32_t(i);
        for (const beebium::WatchpointEntry& wp : watchpoints_) {
            if (at >= wp.start && at < wp.end && (wp.type & kind)) {
                watchpoint_hit_(wp, at, uint8_t(value >> (8 * i)), is_write);
            }
        }
    }
}
