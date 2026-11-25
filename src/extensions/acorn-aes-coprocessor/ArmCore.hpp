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
// ArmCore.hpp, the core's interface, switches and bus
// version 2.0.0, 20 November 2026.

// The ARM1 processor's interface: its switches (ArmOptions), the bus it reaches memory through
// (ArmBus), the debugger's watch on loads and stores (ArmWatch), and the core itself (ArmCore).
// docs/arm1-and-aes.md, Part 2, explains the switches.

#pragma once

#include <cstdint>

// The core's switches: behaviours the emulator may need to change. The field x is the setting
// cpu.x (a few are named adx.x, ldm.x or time.x), listed with its choices in AesOptions.cpp and
// carried inside AesOptions as its `cpu` member. What each decides:
//   ror_reg_carry:      the carry out of ROR by a register amount of 32 or more.
//   enc_a_bit7:         whether a register-shifted operand with bit 7 set is undefined.
//   word_unaligned:     what LDR of a word from an address that is not a multiple of 4 gives.
//   mode_delay:         which bank the instruction just after a mode change reads and writes.
//   ldm_user_bank:      whether a privileged LDM with the S bit, R15 not in the list, loads the
//                       user bank.
//   ldm_pc_wb:          for that LDM of R15 alone, which bank's base is written back.
//   reset_r14:          what R14_svc holds after reset: R15 less 4, or R15.
//   adx_store_to_load:  whether a store to an address past the 26-bit range runs as a read.
//   ldm_fault_loads:    whether an LDM that faults still loads every register.
//   time_reg_shift_next: how a data-processing register shift's extra cycle is made.
//   time_ldr_reg_shift: whether a register-shifted LDR or STR offset adds a cycle.
struct ArmOptions {
    enum class RorRegCarry { RealAes, ZeroAbove31, ZeroAtMultiplesOf32, Arm2 };
    enum class EncABit7 { Ignored, Traps };
    enum class WordUnaligned { IgnoreLowBits, Rotate };
    enum class ModeDelay { ReadsOldWritesNew, OldBankWholeInstruction, None };
    enum class LdmPcWb { UserBank, CurrentBank };
    enum class ResetR14 { R15Minus4, R15 };
    enum class LdmFaultLoads { Complete, StopOverwriting };
    enum class RegShiftNext { NFetch, TwoInternal };
    enum class LdrRegShiftTime { OneS, None };

    RorRegCarry ror_reg_carry = RorRegCarry::RealAes;
    EncABit7 enc_a_bit7 = EncABit7::Ignored;
    WordUnaligned word_unaligned = WordUnaligned::IgnoreLowBits;

    ModeDelay mode_delay = ModeDelay::ReadsOldWritesNew;
    bool ldm_user_bank = true;
    LdmPcWb ldm_pc_wb = LdmPcWb::UserBank;
    ResetR14 reset_r14 = ResetR14::R15Minus4;

    bool adx_store_to_load = true;
    LdmFaultLoads ldm_fault_loads = LdmFaultLoads::Complete;

    RegShiftNext time_reg_shift_next = RegShiftNext::NFetch;
    LdrRegShiftTime time_ldr_reg_shift = LdrRegShiftTime::OneS;
};

// The bus: the only way the core reaches memory. The core knows nothing of RAM, ROM or timing;
// the board implements this interface (AesMemory, and in the logged build TubeLog in front of
// it) and decides what each address holds and what each cycle costs. Addresses have 26 bits.
class ArmBus {
public:
    virtual ~ArmBus() = default;
    // One memory cycle each. `seq` says the access is to the word after the last one, which the
    // board charges less for.
    virtual uint32_t read_word(uint32_t addr, bool seq) = 0;
    virtual uint32_t read_byte(uint32_t addr, bool seq) = 0;
    virtual void write_word(uint32_t addr, uint32_t value, bool seq) = 0;
    virtual void write_byte(uint32_t addr, uint8_t value, bool seq) = 0;
    // A cycle that uses no memory. It still takes time, so the board must be told of it; `addr`
    // is the address the ARM gives for it.
    virtual void internal(uint32_t addr) = 0;

    // Set by the board to abort the access under way. It applies to that one cycle, so the core
    // clears it after each access (take_abort in ArmCore.cpp).
    bool abort_line = false;
    // Whether the ARM is in user mode, kept up to date by the core. The board needs it because
    // abort strict starts checking at the first access made in user mode. Only the mode bits
    // count: LDRT and STRT, which make one access as if in user mode, do not.
    bool user_mode = false;
};

// The debugger's watchpoints. The core tells it of every load and store, never a fetch, with
// the value as it went on the bus, a byte or a word at its bus address. It is an interface so
// that the core needs nothing from the debugger; AesDebug implements it.
class ArmWatch {
public:
    virtual ~ArmWatch() = default;
    // One load or store: its address, the value, 1 or 4 bytes, and whether it was a write.
    virtual void data_access(uint32_t addr, uint32_t value, int bytes, bool write) = 0;
};

// The ARM1 core. The board (AesTube) owns it, sets its inputs and calls step(); the debugger reads
// and sets its registers. See ArmCore.cpp for how it works.
class ArmCore {
public:
    static constexpr uint32_t kPcMask = 0x03FFFFFC;  // The PC's bits within R15.
    static constexpr uint32_t kI = 1u << 27, kF = 1u << 26;  // The interrupt masks within R15.

    // A core on `bus`, with its switches; it starts as power-on leaves it.
    ArmCore(ArmBus& bus, const ArmOptions& options);

    // Runs one instruction, or one cycle of reset, or the entry to an exception.
    void step();

    // The reset, IRQ and FIQ inputs. Each is a level, acting for as long as it is set; the board
    // sets them from the Tube chip's lines before each instruction.
    void set_reset(bool active);
    void set_irq(bool active) { irq_line_ = active; }
    void set_fiq(bool active) { fiq_line_ = active; }

    // For the board's Tube wait (AesTube.cpp, must_wait_for_tube), which must know before an
    // instruction runs whether it could touch the Tube: the word step() will run next, as the
    // pipeline holds it, even if memory has changed since it was fetched.
    uint32_t next_instruction() const { return pipe_[0]; }
    // Also for the Tube wait: whether step() will take an interrupt instead of running that word.
    bool interrupt_will_be_taken() const { return (fiq_line_ && !(r15_ & kF)) || (irq_line_ && !(r15_ & kI)); }

    // Any register of any mode, so that the debugger can show every bank.
    uint32_t& reg_in_mode(int mode, int n);
    // R15 as the core holds it: the PC is the next fetch address (see ArmCore.cpp's map).
    uint32_t r15() const { return r15_; }
    // Sets R15 whole and fetches from there; the tests use it to start a program.
    void set_r15(uint32_t value);
    // The debugger's way to move the PC: the instruction at `at` runs next. Its word and the next
    // are given rather than fetched, so moving the PC takes no time. Flags and mode are kept.
    void load_pipeline(uint32_t at, uint32_t first, uint32_t second);
    // Null unless a watchpoint is set, so that with none set a load or store pays only one test of
    // a pointer.
    void set_watch(ArmWatch* watch) { watch_ = watch; }
    // False while reset holds the core or its restart is still to run. The board asks, because
    // the next step then runs no instruction at R15, so no breakpoint should be checked.
    bool at_instruction() const { return !reset_line_ && !restart_pending_; }
    // The interrupt lines as last set, for the debugger to show.
    bool irq_line() const { return irq_line_; }
    bool fiq_line() const { return fiq_line_; }

private:
    // The PC part of R15: the address of the next fetch.
    uint32_t pc() const { return r15_ & kPcMask; }
    // R15 as the running program reads it, `bytes` past its instruction; without and with the
    // flags and mode.
    uint32_t pc_from_instruction(int bytes) const;
    uint32_t r15_from_instruction(int bytes) const;
    // Register n in the bank the instruction uses; first_cycle_reg is the bank its first cycle
    // reads, which differs only just after a mode change.
    uint32_t& reg(int n) { return regs_[bank_[n]]; }
    uint32_t first_cycle_reg(int n) const { return regs_[first_cycle_bank_[n]]; }
    // Writes the PC part of R15 only, keeping the flags and mode.
    void set_pc(uint32_t value) { r15_ = (r15_ & ~kPcMask) | (value & kPcMask); }
    // Sets or clears one flag in R15.
    void set_flag(uint32_t flag, bool on) { r15_ = on ? (r15_ | flag) : (r15_ & ~flag); }
    // Points the register lookups at the current mode's bank.
    void use_banks_of_current_mode();
    // Writes the flags, masks and mode, as far as the mode allows.
    void write_psr(uint32_t value, bool jumping);
    // Reads and clears the bus's abort line after an access.
    bool take_abort();
    // Moves the pipeline on by one fetched word.
    void fetch_next_word();
    // Fetches two new words after a jump.
    void refill_pipeline();
    // Enters an exception: saves R14, changes mode, masks interrupts, jumps to the vector.
    void enter(uint32_t vector, int mode, bool mask_fiq, uint32_t link_pc);
    // Enters the undefined-instruction trap.
    void undefined();
    // Whether a condition code passes on the current flags.
    bool condition(uint32_t cond) const;
    // Operand 2 or an offset from a shifted register; `carry` is the shifter's carry.
    uint32_t shifted_rm(uint32_t ir, bool& carry);
    // Rn, or a load's or store's base.
    uint32_t rn_value(int rn, bool by_reg);
    // Runs one instruction, by its class.
    void execute(uint32_t ir);
    // One function per class: data processing, LDR and STR, LDM and STM, B and BL.
    void exec_data(uint32_t ir);
    void exec_single(uint32_t ir);
    void exec_block(uint32_t ir);
    void exec_branch(uint32_t ir);

    ArmBus& bus_;
    const ArmOptions opt_;

    // The 24 banked registers R0 to R14 of every mode; kBank in ArmCore.cpp says which is which.
    uint32_t regs_[24] = {};
    // R15 as the ARM holds it: the PC part is the address of the next fetch, not what a program
    // reads (that is pc_from_instruction).
    uint32_t r15_ = 0;
    // The address of the instruction running, from which R15's + 8 and + 12 are counted.
    uint32_t instruction_address_ = 0;

    // Two bank pointers, because cpu.mode_delay can make the first cycle of the instruction after
    // a mode change read a different bank from the one the rest of it uses.
    const uint8_t* bank_ = nullptr;
    const uint8_t* first_cycle_bank_ = nullptr;
    // True for the one instruction after a mode change that still uses the old bank.
    bool mode_just_changed_ = false;

    // The two words fetched ahead, and whether each fetch aborted.
    uint32_t pipe_[2] = {};
    bool pipe_aborted_[2] = {};
    // Whether the next fetch follows on from the last access; a jump or a store clears it.
    bool next_fetch_sequential_ = true;

    ArmWatch* watch_ = nullptr;
    // The inputs as last set. restart_pending_ starts true, so the first step after power-on
    // enters the reset vector.
    bool reset_line_ = false;
    bool restart_pending_ = true;
    bool irq_line_ = false;
    bool fiq_line_ = false;
};
