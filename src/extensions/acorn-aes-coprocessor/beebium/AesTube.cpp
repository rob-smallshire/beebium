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
// AesTube.cpp, runs the ARM against the host's clock, through the Tube
// version 2.0.0, 20 November 2026.

// AesTube (AesTube.hpp) runs the ARM in step with the BBC. Beebium runs the BBC and, from time to
// time, calls run_until
// with the host's time; the ARM then catches up to it. Beebium's own Tube chip sits between
// the two machines, so each side meets the other only through its registers.
//
// The time budget. The host's time, turned into crystal ticks (10 to a 2 MHz host cycle), is
// added to what is due, ticks_due_. The ARM then runs whole instructions while the ticks it has
// spent (AesTiming's clock; the units are described at the top of AesTiming.cpp) are fewer than
// the ticks due. An instruction cannot be cut short, so the last one may run past; that overshoot
// is not lost, because the next batch starts from the ticks already spent. The two totals are
// counted from their own clocks, the host's and the board's, so neither drifts from the other.
// Only a Reset or a debugger stop sets what is due back to what is spent; time while paused is
// never added.
//
// Why a Tube instruction waits. The BBC meets the Tube chip at host time. If an instruction that
// reads or writes the Tube ran past the time due, the BBC would see an access from its own
// future. So before each instruction, must_wait_for_tube asks whether it could touch the Tube and
// might not finish inside the time due; if so, the batch stops before it, and it runs in a later
// batch once more time is due. Waiting costs the ARM nothing, because what is due carries over.

#include "AesTube.hpp"

#include <algorithm>
#include <bit>

namespace {

// The host's time in crystal ticks: kHostCycle (AesTiming.cpp) to each host cycle.
const beebium::ClockRatio kTicksPerHostCycle{uint32_t(kHostCycle), 1};
// What Beebium is told of the board's timing: the ticks per host cycle, and 3 ticks as the S
// cycle. Only a label: Beebium uses the rest of these figures for its 6502 only, and this board's
// timing is AesTiming's.
const beebium::BoardTiming kLabel{kTicksPerHostCycle, 3, 3, 0, 1};
const int kTubeSyncMost = kHostCycle - 1;  // time.tube_sync can add up to a host cycle less one.
const int kLoadOffsetReach = 4096;  // An immediate offset has 12 bits.

}  // namespace

// The board's parts, wired together. In the logged build a TubeLog sits between the core and the
// memory, and between the memory and the Tube, to record every Tube access; in the normal build
// they are wired directly. See TubeLog.hpp.
AesTube::AesTube(const AesOptions& options, beebium::TubeUla& ula)
    : opt_(options),
      ula_(ula),
      timing_(opt_),
#ifdef AES_TUBE_LOG
      log_(TubeLog::path_from_environment(), timing_),
      memory_(opt_, timing_, log_),
      core_(log_, opt_.cpu),
#else
      memory_(opt_, timing_, *this),
      core_(memory_, opt_.cpu),
#endif
      clock_(kTicksPerHostCycle) {
#ifdef AES_TUBE_LOG
    log_.connect(*this, memory_);
#endif
}

// The timing label, for Beebium and for the clock AesExtension shows.
const beebium::BoardTiming& AesTube::board_label() { return kLabel; }

// Beebium's Reset: restarts the ARM and the memory. RAM keeps its contents. The time budget
// starts afresh, because any overshoot carried over belonged to the program that has just been
// stopped.
void AesTube::reset() {
    restart();
    clock_.rebase();
    prst_was_active_ = false;
    ticks_due_ = timing_.ticks();
}

// A reset of the ARM and the memory map together: the core's reset line pulsed, and the boot
// latch set again so that the ROM answers at address 0.
void AesTube::restart() {
    core_.set_reset(true);
    memory_.reset();
    core_.set_reset(false);
}

// Follows the Tube chip's PRST line, which the host sets through the Tube's P flag (the only PRST
// Beebium shows). When PRST ends, the ARM and memory restart. The return value says whether the
// ARM is held now: with tube.prst_level set to Holds, the default, it is held for as long as PRST
// is active, and the callers then do not step the core at all, so that its reset fetches at
// climbing addresses cannot reach the Tube's registers; with Pulse it is never held.
bool AesTube::held_by_prst() {
    const bool active = ula_.coprocessor_reset_active();
    const bool ended = prst_was_active_ && !active;
    prst_was_active_ = active;
    if (ended) restart();
    return active && opt_.tube_prst_level == AesOptions::TubePrstLevel::Holds;
}

// The most a Tube instruction at `at` normally costs, so that must_wait_for_tube can tell whether
// it will finish in time: its fetch, each word at the Tube's price, its last cycle, and one
// refresh per refresh period of that. The prices are AesTiming's. Only a Tube word needs to fit,
// so a ROM word, dearer than a Tube word, needs no price of its own. Two things are not counted:
// a refill after the access (R15 loaded or written back, or an exception entered), and refreshes
// held back through a long run of sequential cycles. Either can make the instruction end a few
// ticks late.
int AesTube::tube_instruction_cost(uint32_t at, int words) const {
    const bool rom = ((at >> 24) & 3) == 3 || (memory_.boot_latch_set() && !(at & AesMemory::kA24));
    // The fetch is priced as ROM if it is in the ROM's region or under the boot latch; every RAM
    // bank is priced the same.
    const int fetch = kN + (rom ? opt_.time_rom_wait * kPalClock : 0);
    const bool sync = opt_.time_tube_sync == AesOptions::TubeSync::NextHostEdge;
    const int word = kN + opt_.time_tube_wait * kPalClock + (sync ? kTubeSyncMost : 0);
    const int cost = fetch + words * word + kS;
    return cost + (1 + cost / opt_.time_refresh_period) * opt_.time_refresh_cost;
}

// Whether the next instruction must wait for more time to be due (the rule is Beebium's, in its
// coprocessor guide, section 6). An instruction that might touch the Tube runs only if it will end
// inside the time due, because the ARM cannot be stopped part-way through one. The word judged is
// the one in the core's pipeline, the one that will really run, even if memory has changed since
// it was fetched. The test errs towards waiting, which loses nothing.
bool AesTube::must_wait_for_tube() {
    const uint32_t at = (core_.r15() & ArmCore::kPcMask) - 8;  // where it was fetched from
    const uint32_t word = core_.next_instruction();
    const uint32_t top = word >> 24;
    // The bits tested (the fields as in ArmCore.cpp): `top` is bits 24-31. Bits 26-27 01 is LDR
    // or STR (single); bits 25-27 100 is LDM or STM (block). For LDR and STR, bit 25 (top & 2) is
    // a register offset and bit 4 (low & 0x10) a shift by a register. Bits 16-19 are the base;
    // bits 0-15 an LDM or STM's register list. Only these instructions reach memory.
    const bool single = ((top >> 2) & 3) == 1;
    const bool block = ((top >> 1) & 7) == 4;
    if (!single && !block) return false;

    const uint32_t low = word & 0xFFFFFF;
    const int words = block ? std::popcount(low & 0xFFFF) : 1;
    // An offset shifted by a register takes one more S cycle.
    const int shift = single && (top & 2) && (low & 0x10) ? kS : 0;
    // Enough time is due for even a Tube access: no need to look at the address.
    if (int64_t(ticks_due_ - timing_.ticks()) >= tube_instruction_cost(at, words) + shift) return false;

    if (single && (top & 2)) return true;  // A register offset could reach anywhere.
    // Otherwise the address is the base plus at most a 12-bit offset, so the instruction can only
    // reach the Tube if its base is within that reach of the Tube's region. R15 as the base reads
    // the instruction's address + 8.
    const int rn = (low >> 16) & 15;
    const uint32_t base = rn == 15 ? at + 8 : core_.reg_in_mode(core_.r15() & 3, rn);
    return in_tube(base) || in_tube(base - kLoadOffsetReach) || in_tube(base + kLoadOffsetReach);
}

// The debugger's step: one instruction, with the interrupt lines read as run_until reads them.
// There is no Tube wait, because nothing is due while paused; the ticks it spends run ahead of
// what is due, and the next batch pays them off. Returns the ticks the instruction cost. While
// PRST holds the ARM, nothing runs.
uint64_t AesTube::step_one() {
    if (ula_.coprocessor_reset_active() && opt_.tube_prst_level == AesOptions::TubePrstLevel::Holds) return 0;
    const uint64_t before = timing_.ticks();
    core_.set_irq(ula_.pirq());
    core_.set_fiq(ula_.pnmi_level());
    core_.step();
    instructions_++;
    return timing_.ticks() - before;
}

// Beebium's call: runs the ARM up to the host's time `host_cycle`, by the time budget above. Time
// while paused is dropped, as Beebium's contract asks, and so is time while PRST holds the ARM,
// which is a choice made here.
void AesTube::run_until(uint64_t host_cycle) {
    const uint64_t due = clock_.cycles_due(host_cycle);
    // PRST is read even while paused, so that the ARM restarts at once when it ends, as the host
    // expects. Nothing runs. This is not locked against a debugger read, which may then see a
    // reset half done.
    if (paused_) {
        held_by_prst();
        return;
    }
    if (held_by_prst()) return;
    ticks_due_ += due;
    while (timing_.ticks() < ticks_due_) {
        // The Tube chip's PIRQ line drives IRQ and PNMI drives FIQ. They are levels, read once per
        // instruction.
        core_.set_irq(ula_.pirq());
        core_.set_fiq(ula_.pnmi_level());
        // An interrupt about to be taken makes no Tube access of its own, so it never waits.
        if (!core_.interrupt_will_be_taken() && must_wait_for_tube()) break;
        // With no breakpoint or watchpoint set, the cost is one test of a pointer and the loop
        // runs on unchanged.
        if (stops_) {
            if (!step_watched()) break;
            continue;
        }
        core_.step();
        instructions_++;
    }
}

// One instruction with breakpoints or watchpoints set; false if one stopped the ARM. A breakpoint
// is asked before the instruction (if an interrupt displaces it, it is asked when it does run);
// a watchpoint stops the ARM after the access. A stop drops the rest of the batch, as paused time
// is dropped.
bool AesTube::step_watched() {
    if (core_.at_instruction() && !core_.interrupt_will_be_taken()) {
        stops_->before_instruction(((core_.r15() & ArmCore::kPcMask) - 8) & ArmCore::kPcMask);
        if (paused_) {
            ticks_due_ = std::min(ticks_due_, timing_.ticks());
            return false;
        }
    }
    core_.step();
    instructions_++;
    if (paused_) {
        ticks_due_ = std::min(ticks_due_, timing_.ticks());
        return false;
    }
    return true;
}
