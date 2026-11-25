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
// ArmCore.cpp, the ARM1 processor core
// version 2.0.0, 20 November 2026.

// The ARM1 processor: it runs one instruction at a time and takes exceptions. It knows nothing
// of the AES board. Every memory access goes out through ArmBus (ArmCore.hpp), the interface the
// board implements, and the board decides what is at each address and how long the access takes.
// Behaviours the emulator may need to change are switches in ArmOptions (ArmCore.hpp).
//
// The pipeline. The ARM fetches two words ahead of the instruction it runs. pipe_[0] holds the
// instruction about to run and pipe_[1] the one after. Between instructions, the PC in r15_ is
// the address of the next fetch, 8 bytes past the instruction about to run. step() makes that
// fetch before running the instruction, so by then the PC has moved on again; that is why the
// code takes a program's view of R15 from pc_from_instruction, not from pc(). A program reading
// R15 sees its own address + 8, or + 12 when it reads R15 in its second cycle (after a shift by
// a register, or as the data of a store). A jump empties the pipeline and fetches two new words.
//
// The registers. There are 25: R0 to R15 as user mode sees them, and copies that replace some of
// them in the other modes: FIQ has its own R10 to R14, IRQ and SVC their own R13 and R14. kBank
// says which slot of regs_ holds each register in each mode. R15 is kept apart in r15_, because
// it holds more than the PC: the flags N, Z, C and V and the interrupt masks I and F in its top
// six bits, and the mode in its bottom two (Software Manual p.1 has the layout).
//
// The cycles. Each access goes to ArmBus with a flag saying whether it is sequential (the next
// word after the last access), because the board charges less for those. A cycle that uses no
// memory goes to bus_.internal, so that the board can charge for it too. This file holds no
// times, only the kind and order of the cycles each instruction makes (Software Manual p.32 has
// the table).
//
// The functions, in order: start-up and the ways in for the debugger and the tests; the views of
// R15; banks and mode changes; fetching; exceptions; condition codes; step(), one instruction;
// execute(), which sorts an instruction into its class; then the operand helpers and one function
// per class of instruction.
//
// The comments that cite "SW" pages and real-AES tests mark code that the fault check alters; they
// must keep their exact words.

#include "ArmCore.hpp"

#include <bit>

namespace {

// Bits of R15: the four flags, the parts of R15 each mode may write, and the masks for the
// address bits.
const uint32_t kFlagN = 1u << 31, kFlagZ = 1u << 30, kFlagC = 1u << 29, kFlagV = 1u << 28;
const uint32_t kUserPsr = 0xF0000000, kAllPsr = 0xFC000003;
const uint32_t kBadAddress = 0xFC000000, kAddress = 0x03FFFFFF;
const uint32_t kWordAddress = kAddress & ~3u;
// The modes, as R15's bottom two bits hold them, and the exception vectors.
const int kUser = 0, kFiq = 1, kIrq = 2, kSvc = 3;
const uint32_t kVecReset = 0x00, kVecUndefined = 0x04, kVecSwi = 0x08, kVecPrefetchAbort = 0x0C;
const uint32_t kVecDataAbort = 0x10, kVecAddress = 0x14, kVecIrq = 0x18, kVecFiq = 0x1C;

// Which slot of regs_ holds each of R0 to R14, one row per mode (user, FIQ, IRQ, SVC). Slots 0
// to 14 are user mode's; 15 to 19 FIQ's R10 to R14; 20 and 21 IRQ's R13 and R14; 22 and 23 SVC's.
const uint8_t kBank[4][15] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 18, 19},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 20, 21},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 22, 23},
};

// Rotates v right by n bits.
uint32_t ror32(uint32_t v, unsigned n) { return std::rotr(v, int(n)); }
// Bit n of v.
bool bit(uint32_t v, unsigned n) { return (v >> n) & 1; }

}  // namespace

// Starts the ARM as power-on leaves it: in SVC mode with both interrupts masked, the registers 0
// (a guess: nothing is known of their contents at power-on), and a reset pending, so that the
// first step() enters the reset vector.
ArmCore::ArmCore(ArmBus& bus, const ArmOptions& options) : bus_(bus), opt_(options) {
    r15_ = kI | kF | kSvc;
    use_banks_of_current_mode();
}

// Any register of any mode, not only the current one, so that the debugger can show every bank.
uint32_t& ArmCore::reg_in_mode(int mode, int n) { return regs_[kBank[mode][n]]; }

// Sets the whole of R15 and fetches from there, as a jump would. The tests use it to start a
// program at a chosen address and mode without running a reset.
void ArmCore::set_r15(uint32_t value) {
    r15_ = value;
    restart_pending_ = false;
    mode_just_changed_ = false;
    use_banks_of_current_mode();
    refill_pipeline();
}

// The debugger's way to move the PC: the instruction at `at` runs next. Its two words are given
// rather than fetched, so that moving the PC takes no time and makes no bus cycle. The state is
// left as refill_pipeline leaves it: the PC two words on, and the next fetch sequential.
void ArmCore::load_pipeline(uint32_t at, uint32_t first, uint32_t second) {
    set_pc(at + 8);
    pipe_[0] = first;
    pipe_[1] = second;
    pipe_aborted_[0] = pipe_aborted_[1] = false;
    next_fetch_sequential_ = true;
}

// The reset input. While it is held, step() only makes dummy fetches; when it is let go, the next
// step() enters the reset vector. Holding and letting go are separate because the host can hold
// the ARM in reset for as long as it likes.
void ArmCore::set_reset(bool active) {
    if (reset_line_ && !active) restart_pending_ = true;
    reset_line_ = active;
}

// The PC as the running program sees it, `bytes` past its own instruction's address (see the map
// for + 8 and + 12). Counted from the instruction, not from the PC, so the value stays right after
// the instruction has jumped.
uint32_t ArmCore::pc_from_instruction(int bytes) const {
    return (instruction_address_ + bytes) & kPcMask;
}
// The same, with the flags and mode from R15 added, as an instruction that reads all of R15
// sees it.
uint32_t ArmCore::r15_from_instruction(int bytes) const {
    return (r15_ & ~kPcMask) | pc_from_instruction(bytes);
}

// Points the register lookups at the current mode's bank. Also tells the bus whether the ARM is
// in user mode, because the board's abort strict setting starts checking at the first access made
// in user mode.
void ArmCore::use_banks_of_current_mode() {
    bus_.user_mode = (r15_ & 3) == kUser;
    bank_ = kBank[r15_ & 3];
    first_cycle_bank_ = bank_;
}

// Writes the flags and, outside user mode, the interrupt masks and the mode; in user mode only the
// flags change. A mode change does not take full effect at once: the switch cpu.mode_delay says
// which bank the next instruction uses. `jumping` is true when the same instruction also jumps.
void ArmCore::write_psr(uint32_t value, bool jumping) {
    const int old_mode = r15_ & 3;
    const uint32_t mask = old_mode == kUser ? kUserPsr : kAllPsr;
    r15_ = (r15_ & ~mask) | (value & mask);
    bus_.user_mode = (r15_ & 3) == kUser;
    if (int(r15_ & 3) == old_mode) return;

    // With a jump, or with no delay chosen, the new mode's banks are used from the next
    // instruction on.
    if (jumping || opt_.mode_delay == ArmOptions::ModeDelay::None) {
        mode_just_changed_ = false;
        use_banks_of_current_mode();
        return;
    }
    // Otherwise the next instruction keeps the old bank. step() switches the banks after it.
    mode_just_changed_ = true;
    // ReadsOldWritesNew: the next instruction's first-cycle reads use the old bank, and the rest
    // of it the new one.
    if (opt_.mode_delay == ArmOptions::ModeDelay::ReadsOldWritesNew) {
        first_cycle_bank_ = bank_;
        bank_ = kBank[r15_ & 3];
    }
}

// Reads and clears the bus's abort line. The board raises it during one bus cycle, so the core
// takes it straight after each access; true means that access aborted.
bool ArmCore::take_abort() {
    const bool aborted = bus_.abort_line;
    bus_.abort_line = false;
    return aborted;
}

// Moves the pipeline on by one word: the next instruction moves up, and a new word is fetched at
// the PC. Whether that fetch aborted is kept with the word, because a prefetch abort is taken only
// if the word reaches the front and would run.
void ArmCore::fetch_next_word() {
    pipe_[0] = pipe_[1];
    pipe_aborted_[0] = pipe_aborted_[1];
    pipe_[1] = bus_.read_word(pc(), next_fetch_sequential_);
    pipe_aborted_[1] = take_abort();
    set_pc(pc() + 4);
    next_fetch_sequential_ = true;
}

// After a jump the two words in the pipeline are the wrong ones, so two new ones are fetched: the
// first non-sequential (N), the second sequential (S).
void ArmCore::refill_pipeline() {
    next_fetch_sequential_ = false;
    fetch_next_word();
    fetch_next_word();
}

// Every exception goes through here: it saves the return address, with the flags and mode, in the
// new mode's R14; switches mode; masks IRQ, and FIQ too if asked; and jumps to the vector.
void ArmCore::enter(uint32_t vector, int mode, bool mask_fiq, uint32_t link_pc) {
    regs_[kBank[mode][14]] = (r15_ & ~kPcMask) | (link_pc & kPcMask);
    r15_ = (r15_ & ~(kPcMask | 3)) | kI | (mask_fiq ? kF : 0) | vector | uint32_t(mode);
    mode_just_changed_ = false;
    use_banks_of_current_mode();
    refill_pipeline();
}

// The undefined-instruction trap. R14 is the address of the instruction after the undefined one,
// where a handler such as the floating-point emulator returns once it has done its work.
void ArmCore::undefined() { enter(kVecUndefined, kSvc, false, pc_from_instruction(4)); }

// Whether an instruction's condition (its top four bits) passes, from the flags. In order 0 to
// 15: EQ, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL (always), NV (never).
bool ArmCore::condition(uint32_t cond) const {
    const bool n = r15_ & kFlagN, z = r15_ & kFlagZ, c = r15_ & kFlagC, v = r15_ & kFlagV;
    switch (cond) {
        case 0: return z;
        case 1: return !z;
        case 2: return c;
        case 3: return !c;
        case 4: return n;
        case 5: return !n;
        case 6: return v;
        case 7: return !v;
        case 8: return c && !z;
        case 9: return !c || z;
        case 10: return n == v;
        case 11: return n != v;
        case 12: return n == v && !z;
        case 13: return n != v || z;
        case 14: return true;
        default: return false;
    }
}

// One step: one instruction, or one cycle of reset, or the entry to an exception. The board
// calls this over and over; the time a step takes is what the board charged for its cycles.
void ArmCore::step() {
    // While reset is held: a dummy fetch at a climbing address, and nothing else.
    if (reset_line_) {
        bus_.read_word(pc(), true);
        take_abort();
        set_pc(pc() + 4);
        return;
    }
    // Reset let go: enter the reset vector. R14 gets R15 less 4, or R15, by cpu.reset_r14.
    if (restart_pending_) {
        restart_pending_ = false;
        const bool minus_4 = opt_.reset_r14 == ArmOptions::ResetR14::R15Minus4;
        enter(kVecReset, kSvc, true, minus_4 ? pc() - 4 : pc());
        return;
    }

    // The instruction about to run is two words behind the next fetch (see the map).
    instruction_address_ = pc() - 8;
    const uint32_t ir = pipe_[0];
    const bool prefetch_aborted = pipe_aborted_[0];
    fetch_next_word();

    // Between instructions, an unmasked FIQ is taken first, then IRQ, then a prefetch abort on
    // the word about to run; otherwise the instruction runs. Each exception enters as SWI does.
    // R14 is the instruction + 4, so that the usual return, SUBS PC,R14,#4, comes back to the
    // instruction the exception displaced. A prefetch abort is taken whatever the condition.
    if (fiq_line_ && !(r15_ & kF)) {
        enter(kVecFiq, kFiq, true, pc_from_instruction(4));
    } else if (irq_line_ && !(r15_ & kI)) {
        enter(kVecIrq, kIrq, false, pc_from_instruction(4));
    } else if (prefetch_aborted) {
        enter(kVecPrefetchAbort, kSvc, false, pc_from_instruction(4));
    } else {
        execute(ir);
    }

    // The delay set by cpu.mode_delay lasts one instruction. Unless that instruction was the one
    // that changed mode, the banks follow the mode again from here.
    if (mode_just_changed_) {
        mode_just_changed_ = false;
    } else {
        use_banks_of_current_mode();
    }
}

// Runs one instruction, sorted by bits 24 to 27 (`top`): 00xx data processing, 01xx LDR and STR,
// 100x LDM and STM, 101x branch, 1100 to 1110 undefined, 1111 SWI.
void ArmCore::execute(uint32_t ir) {
    const uint32_t top = (ir >> 24) & 0xF;
    const bool is_undefined = top >= 0xC && top != 0xF;
    // An undefined instruction traps whatever its condition, so it is tested before the condition.
    if (is_undefined) {
        undefined();
        return;
    }
    if (!condition(ir >> 28)) return;

    switch (top >> 1) {
        case 0: case 1: exec_data(ir); break;
        case 2: case 3: exec_single(ir); break;
        case 4: exec_block(ir); break;
        case 5: exec_branch(ir); break;
        default:  // Only SWI (1111) reaches here: 1100 to 1110 trapped above.
            // SWI: R14 is the word after the SWI, where the handler returns.
            enter(kVecSwi, kSvc, false, pc_from_instruction(4));
            break;
    }
}

// B and BL. Bit 24 is the link bit; bits 0 to 23 are a signed offset in words, from R15 as the
// branch reads it (+ 8). Cycles: its own fetch, then the refill's two.
void ArmCore::exec_branch(uint32_t ir) {
    // BL saves the address of the next instruction, with the flags and mode, in R14, so that
    // MOVS PC,R14 returns and restores them.
    if (bit(ir, 24)) reg(14) = r15_from_instruction(4);
    const int32_t words = int32_t(ir << 8) >> 8;
    set_pc(pc_from_instruction(8) + uint32_t(words) * 4);
    refill_pipeline();
}

// Operand 2 from a register, shifted, for data processing and for LDR and STR alike. Rm is bits 0
// to 3. Bit 4 set means the amount comes from register Rs (bits 8 to 11); clear, the amount is
// bits 7 to 11. Bits 5 and 6 give the shift: 0 LSL, 1 LSR, 2 ASR, 3 ROR (RRX when the amount is
// 0). `carry` comes in as the C flag and goes out as the shifter's carry, which a logical
// instruction with S puts in C. R15 as Rm, or as an LDR or STR offset, includes the flags and mode.
uint32_t ArmCore::shifted_rm(uint32_t ir, bool& carry) {
    const int rm = ir & 15;
    const int type = (ir >> 5) & 3;
    const bool by_reg = bit(ir, 4);
    // R15 reads + 12 after a shift by a register (see the map). Rs is read in the first cycle and
    // Rm in the second, which matters only just after a mode change (cpu.mode_delay).
    uint32_t v;
    if (rm == 15) {
        v = r15_from_instruction(by_reg ? 12 : 8);
    } else {
        v = by_reg ? reg(rm) : first_cycle_reg(rm);
    }

    unsigned n;
    if (!by_reg) {
        n = (ir >> 7) & 31;
        // An amount of 0 in the instruction has a special meaning for each shift but LSL.
        if (n == 0) {
            if (type == 0) return v;  // LSL #0: the value and the carry unchanged.
            if (type == 3) {
                // RRX: a one-bit rotate right through the carry. Its carry out is bit 0 OR bit 31.
                const uint32_t in = carry ? 0x80000000 : 0;
                carry = bit(v, 0) || bit(v, 31);
                return in | (v >> 1);
            }
            n = 32;  // DOC, SW p.13: LSR and ASR by 0 mean 32.
        }
    } else {
        const int rs = (ir >> 8) & 15;
        // Only the bottom byte of Rs counts. R15 as Rs is the PC alone, + 8.
        n = (rs == 15 ? pc_from_instruction(8) : first_cycle_reg(rs)) & 0xFF;
        if (n == 0) {
            // A register amount of 0 changes nothing and keeps the carry; see test 5 Q2, s = 0
            // (results/real-aes-compare.md).
            return v;
        }
    }
    // The shifts proper, amounts 1 and up; a register can give 32 or more (Software Manual
    // pp.14-15 has the table).
    switch (type) {
        case 0:
            carry = n < 32 ? bit(v, 32 - n) : (n == 32 && bit(v, 0));
            return n < 32 ? v << n : 0;
        case 1:
            carry = n < 32 ? bit(v, n - 1) : (n == 32 && bit(v, 31));
            return n < 32 ? v >> n : 0;
        case 2:
            carry = n < 32 ? bit(v, n - 1) : bit(v, 31);
            return uint32_t(int32_t(v) >> (n < 32 ? n : 31));
        default: {
            const unsigned r = n & 31;
            if (n < 32) {
                carry = bit(v, n - 1);
            } else {
                // ROR by 32 or more: the carry is set by cpu.ror_reg_carry. RealAes gives bit 31
                // at 32, 64, 96 and so on, and 0 at other amounts; the other choices give other
                // rules.
                switch (opt_.ror_reg_carry) {
                    case ArmOptions::RorRegCarry::RealAes:
                        carry = r == 0 && bit(v, 31);
                        break;
                    case ArmOptions::RorRegCarry::ZeroAbove31:
                        carry = false;
                        break;
                    case ArmOptions::RorRegCarry::ZeroAtMultiplesOf32:
                        carry = r != 0 && bit(v, r - 1);
                        break;
                    case ArmOptions::RorRegCarry::Arm2:
                        carry = bit(v, r == 0 ? 31 : r - 1);
                        break;
                }
            }
            return ror32(v, r);
        }
    }
}

// The first operand, Rn, or the base of a load or store. As either, R15 is the PC alone, without
// the flags: + 8, or + 12 after a shift by a register.
uint32_t ArmCore::rn_value(int rn, bool by_reg) {
    if (rn == 15) return pc_from_instruction(by_reg ? 12 : 8);
    return by_reg ? reg(rn) : first_cycle_reg(rn);
}

// Data processing. Bit 25 means operand 2 is an immediate; bits 21 to 24 are the operation; bit
// 20 (S) says whether to set the flags; bits 16 to 19 are Rn, 12 to 15 Rd, and 0 to 11 operand 2:
// either 8 bits rotated right by twice bits 8 to 11, or a register as shifted_rm reads it.
// Cycles: its own fetch; one internal cycle more for a shift by a register, and then either the
// next fetch made non-sequential or a second internal cycle (time.reg_shift_next); a refill if it
// writes R15.
void ArmCore::exec_data(uint32_t ir) {
    const int op = (ir >> 21) & 15;
    const int rn = (ir >> 16) & 15;
    const int rd = (ir >> 12) & 15;
    const bool set = bit(ir, 20);
    const bool is_compare = op >= 8 && op <= 11;
    const bool old_c = r15_ & kFlagC;
    bool shifter_c = old_c;
    bool by_reg = false;
    uint32_t op2;

    if (bit(ir, 25)) {
        const unsigned rot = ((ir >> 8) & 15) * 2;
        op2 = ror32(ir & 0xFF, rot);
        // A rotated immediate sets the shifter's carry to the last bit rotated into bit 31;
        // unrotated, the carry is unchanged.
        if (rot != 0) shifter_c = bit(op2, 31);
    } else {
        by_reg = bit(ir, 4);
        // With cpu.enc_a_bit7 set to Traps, a shift by a register with bit 7 set is undefined.
        if (by_reg && bit(ir, 7) && opt_.enc_a_bit7 == ArmOptions::EncABit7::Traps) {
            undefined();
            return;
        }
        op2 = shifted_rm(ir, shifter_c);
        if (by_reg) {
            bus_.internal(pc_from_instruction(12));  // DOC, SW p.14: one extra cycle.
            // A shift by a register takes one cycle more again, by time.reg_shift_next: either
            // the next fetch is made non-sequential (NFetch) or a second internal cycle is added.
            if (opt_.time_reg_shift_next == ArmOptions::RegShiftNext::NFetch) {
                next_fetch_sequential_ = false;
            } else {
                bus_.internal(pc_from_instruction(12));
            }
        }
    }
    const uint32_t op1 = rn_value(rn, by_reg);
    // One sum serves every arithmetic operation, because a - b is a + NOT b + 1, and C after a
    // subtraction is "not borrow". SBC and RSC put the C flag in place of the 1.
    uint32_t a = op1, b = op2;
    bool carry_in = false;
    bool arithmetic = true;
    uint32_t result = 0;
    // The operations by number: 0 AND, 1 EOR, 2 SUB, 3 RSB, 4 ADD, 5 ADC, 6 SBC, 7 RSC, 8 TST,
    // 9 TEQ, 10 CMP, 11 CMN, 12 ORR, 13 MOV, 14 BIC, 15 MVN. 8 to 11 are the compares: they only
    // set flags. RSB and RSC subtract the other way round, so their operands are swapped.
    switch (op) {
        case 2: case 10: b = ~op2; carry_in = true; break;
        case 3: a = op2; b = ~op1; carry_in = true; break;
        case 4: case 11: break;
        case 5: carry_in = old_c; break;
        case 6: b = ~op2; carry_in = old_c; break;
        case 7: a = op2; b = ~op1; carry_in = old_c; break;
        case 0: case 8: result = op1 & op2; arithmetic = false; break;
        case 1: case 9: result = op1 ^ op2; arithmetic = false; break;
        case 12: result = op1 | op2; arithmetic = false; break;
        case 13: result = op2; arithmetic = false; break;
        case 14: result = op1 & ~op2; arithmetic = false; break;
        default: result = ~op2; arithmetic = false; break;
    }
    const uint64_t wide = uint64_t(a) + b + carry_in;
    if (arithmetic) result = uint32_t(wide);

    // A compare with S clear does nothing more: Rd is not written and the flags are kept.
    if (is_compare && !set) return;
    if (rd == 15) {
        if (is_compare) {
            // A compare with Rd as R15 (TEQP and the like) writes the flags, masks and mode from
            // the result. It does not jump, because the next instruction is already fetched.
            write_psr(result, false);
            return;
        }
        // Any other instruction writing R15 jumps; with S it also writes the flags, masks and
        // mode, and without S only the PC.
        if (set) write_psr(result, true);
        set_pc(result);
        refill_pipeline();
        return;
    }
    if (!is_compare) reg(rd) = result;
    if (set) {
        set_flag(kFlagN, bit(result, 31));
        set_flag(kFlagZ, result == 0);
        if (arithmetic) {
            // C is the carry out of bit 31; V is set when both inputs have one sign and the
            // result the other.
            set_flag(kFlagC, wide >> 32);
            set_flag(kFlagV, bit((a ^ result) & (b ^ result), 31));
        } else {
            set_flag(kFlagC, shifter_c);  // DOC, SW p.12: V unaffected.
        }
    }
}

// LDR and STR. Bit 25 means the offset is a register (shifted as by shifted_rm), else bits 0 to
// 11 are a 12-bit offset; bit 24 means pre-indexed (the offset is added before the access); 23
// up (add, else subtract); 22 a byte; 21 write the address back to the base; 20 load. Bits 16 to
// 19 are Rn, the base, and 12 to 15 Rd. Cycles: its own fetch; one internal cycle for an offset
// shifted by a register (time.ldr_reg_shift); the data cycle, non-sequential; then a load adds an
// internal cycle and the next fetch is sequential, while after a store it is not; a refill if R15
// is written. In the post-indexed forms bit 21 is T (LDRT, STRT); nothing here uses it.
void ArmCore::exec_single(uint32_t ir) {
    const bool has_rm = bit(ir, 25), pre = bit(ir, 24), up = bit(ir, 23), is_byte = bit(ir, 22);
    const bool wb_bit = bit(ir, 21), load = bit(ir, 20);
    const int rn = (ir >> 16) & 15;
    const int rd = (ir >> 12) & 15;
    const bool by_reg = has_rm && bit(ir, 4);

    // An offset shifted by a register with bit 7 set: undefined if cpu.enc_a_bit7 is Traps, as in
    // data processing.
    const bool bit7_traps = bit(ir, 7) && opt_.enc_a_bit7 == ArmOptions::EncABit7::Traps;
    if (by_reg && bit7_traps) {
        undefined();
        return;
    }
    bool carry_for_rrx = r15_ & kFlagC;
    const uint32_t offset = has_rm ? shifted_rm(ir, carry_for_rrx) : ir & 0xFFF;
    // time.ldr_reg_shift set to OneS: an offset shifted by a register costs one internal cycle.
    if (by_reg && opt_.time_ldr_reg_shift == ArmOptions::LdrRegShiftTime::OneS) {
        bus_.internal(pc_from_instruction(12));
    }

    const uint32_t base = rn_value(rn, by_reg);
    const uint32_t moved = up ? base + offset : base - offset;
    const uint32_t address = pre ? moved : base;
    // Post-indexed always writes back; pre-indexed only with bit 21.
    const bool writeback = !pre || wb_bit;
    const uint32_t bus_address = is_byte ? address & kAddress : address & kWordAddress;
    // The data cycle, as functions because the address exception below needs them too. The
    // debugger's watch, if one is set, is told of each access.
    const auto read_bus = [&] {
        const uint32_t value = is_byte ? bus_.read_byte(bus_address, false) : bus_.read_word(bus_address, false);
        if (watch_) watch_->data_access(bus_address, value, is_byte ? 1 : 4, false);
        return value;
    };

    // The value a store writes: R15 is + 12 (see the map). The register is read in the
    // instruction's second cycle, so just after a mode change it comes from the new bank.
    const uint32_t store_value = load ? 0 : (rd == 15 ? r15_from_instruction(12) : reg(rd));
    const auto write_bus = [&] {
        is_byte ? bus_.write_byte(bus_address, uint8_t(store_value), false)
                : bus_.write_word(bus_address, store_value, false);
        if (watch_) watch_->data_access(bus_address, is_byte ? store_value & 0xFF : store_value, is_byte ? 1 : 4, true);
    };

    // An address beyond the ARM's 26 bits is an address exception. The address checked is the
    // one used: post-indexed, the base before the offset. See test 7 C1, C3, C5,
    // C6 (results/real-aes-compare.md).
    if (address & kBadAddress) {
        // The bus cycle still runs, at the low 26 bits, and its result is thrown away. With
        // adx.store_to_load a store runs as a read, so nothing is written.
        if (load || opt_.adx_store_to_load) {
            read_bus();
        } else {
            write_bus();
        }
        take_abort();
        // The base is not written back, and R14 is the instruction + 8. See
        // test 7 C4 to C6 (results/real-aes-compare.md).
        enter(kVecAddress, kSvc, false, pc_from_instruction(8));
        return;
    }

    // Writes the address back to the base, if this form writes back. It is called before a load
    // lands, so that a load into the base register wins, and after a store's data is read, so
    // that a store of the base stores the old value. Writing back to R15 moves the PC and keeps
    // the flags.
    bool pc_changed = false;
    const auto write_base = [&] {
        if (!writeback) return;
        if (rn == 15) {
            set_pc(moved);
            pc_changed = true;
        } else {
            reg(rn) = moved;
        }
    };
    if (load) {
        uint32_t value = read_bus();
        // A word load from an address that is not a multiple of 4: cpu.word_unaligned. By default
        // (IgnoreLowBits) the bus has already dropped the low two bits; Rotate turns the word.
        if (!is_byte && opt_.word_unaligned == ArmOptions::WordUnaligned::Rotate) {
            value = ror32(value, (address & 3) * 8);
        }
        // An aborted load leaves every register as it was and enters the data abort.
        if (take_abort()) {
            enter(kVecDataAbort, kSvc, false, pc_from_instruction(8));
            return;
        }
        bus_.internal(pc_from_instruction(12));
        write_base();
        // A load into R15 writes the PC only, never the flags or mode.
        if (rd == 15) {
            set_pc(value);  // DOC, SW p.21: never the PSR.
            pc_changed = true;
        } else {
            reg(rd) = value;
        }
    } else {
        write_bus();
        if (take_abort()) {
            enter(kVecDataAbort, kSvc, false, pc_from_instruction(8));
            return;
        }
        write_base();
    }

    // A jump refills the pipeline; otherwise LDR ends with a sequential fetch and STR does not.
    if (pc_changed) {
        refill_pipeline();
    } else {
        next_fetch_sequential_ = load;
    }
}

// LDM and STM. Bit 24 means pre-indexed; 23 up; 22 (S, written ^) loads the flags with R15, or
// uses the user bank; 21 write back; 20 load. Bits 16 to 19 are Rn, the base, and 0 to 15 the
// list, one bit per register. Cycles: its own fetch; one data cycle per register, the first
// non-sequential and the rest sequential; then LDM adds an internal cycle, and STM makes the next
// fetch non-sequential; a refill if LDM loads R15.
void ArmCore::exec_block(uint32_t ir) {
    const bool pre = bit(ir, 24), up = bit(ir, 23), psr_bit = bit(ir, 22);
    const bool load = bit(ir, 20);
    const int rn = (ir >> 16) & 15;
    const uint32_t list = ir & 0xFFFF;
    const bool writeback = bit(ir, 21) && rn != 15;  // Never to R15.
    const int count = std::popcount(list);
    // An empty list does nothing: a guess, as nothing is known of what it does.
    if (count == 0) return;

    // In user mode the S bit is ignored.
    const int start_mode = r15_ & 3;
    const bool privileged = start_mode != kUser;
    // With S and outside user mode, STM uses the user bank, and so does LDM without R15 if
    // cpu.ldm_user_bank is set.
    const bool user_bank = privileged && psr_bit &&
                           (load ? !bit(list, 15) && opt_.ldm_user_bank : true);
    // LDM with S of R15 alone, with writeback: cpu.ldm_pc_wb set to UserBank writes the user
    // bank's base back.
    const bool pc_only_user_bank = load && privileged && psr_bit && list == 0x8000 && rn != 15 &&
                    kBank[start_mode][rn] != rn &&
                    opt_.ldm_pc_wb == ArmOptions::LdmPcWb::UserBank;
    // Writes the new base back, to the user bank's base whenever the transfer uses the user bank.
    const auto write_back = [&](uint32_t value) {
        ((user_bank || pc_only_user_bank) ? regs_[kBank[kUser][rn]] : reg(rn)) = value;
    };

    // With R15 as the base, the flags and mode are part of the address, and R15 reads + 8.
    const uint32_t base = rn == 15 ? r15_from_instruction(8) : first_cycle_reg(rn);
    const uint32_t lowest = up ? base : base - 4u * count;
    const uint32_t new_base = up ? base + 4u * count : lowest;
    // A block always runs upwards from its lowest address, whichever way the base moves. "Up,
    // before" and "down, after" skip the first word of that range.
    uint32_t address = lowest + (pre == up ? 4 : 0);

    // Only the first address is checked for an address exception; later words wrap to 26 bits.
    bool address_fault = (address & kBadAddress) != 0;
    bool data_abort = false;
    if (!load) {
        bool first = true;
        for (int i = 0; i < 16; i++) {
            if (!bit(list, i)) continue;
            const uint32_t at = address & kWordAddress;
            uint32_t value;
            if (i == 15) {
                // R15 is stored + 12 (see the map).
                value = r15_from_instruction(12);
            } else {
                value = user_bank ? regs_[kBank[kUser][i]] : reg(i);
            }
            // After an address exception, with adx.store_to_load, the cycles run as reads, so
            // nothing is written.
            if (address_fault && opt_.adx_store_to_load) {
                const uint32_t seen = bus_.read_word(at, !first);
                if (watch_) watch_->data_access(at, seen, 4, false);
            } else {
                bus_.write_word(at, value, !first);
                if (watch_) watch_->data_access(at, value, 4, true);
            }
            if (take_abort()) data_abort = true;
            address += 4;
            // The base is written back after the first register goes out, so an STM whose base is
            // first in the list stores the old base.
            if (first && writeback) write_back(new_base);
            first = false;
        }
        next_fetch_sequential_ = false;  // DOC, SW p.32: the 2 N of STM.
    } else {
        // What an LDM that faults still loads is set by ldm.fault_loads: Complete loads every
        // register; StopOverwriting stops loading at the fault.
        //
        // Waiting and landing: each word read is held in `waiting` and put in its register
        // (`land`) only once the next word has been read, or after the last. That is how
        // StopOverwriting loses the word before a fault as well as the faulting one. Under
        // Complete every word lands. A loaded R15 is kept aside and used only at the end, and
        // only if nothing faulted.
        const bool complete = opt_.ldm_fault_loads == ArmOptions::LdmFaultLoads::Complete;
        bool stopped = address_fault && !complete;
        int waiting = -1;
        uint32_t waiting_value = 0;
        uint32_t loaded_r15 = 0;
        bool r15_loaded = false;
        // Puts a loaded word in its register: R15 aside, the others in the user bank or the
        // current one.
        const auto land = [&](int i, uint32_t value) {
            if (i == 15) {
                loaded_r15 = value;
                r15_loaded = true;
            } else if (user_bank) {
                regs_[kBank[kUser][i]] = value;
            } else {
                reg(i) = value;
            }
        };

        bool first = true;
        for (int i = 0; i < 16; i++) {
            if (!bit(list, i)) continue;
            const uint32_t at = address & kWordAddress;
            const uint32_t value = bus_.read_word(at, !first);
            if (watch_) watch_->data_access(at, value, 4, false);
            if (take_abort()) {
                stopped = !complete;
                data_abort = true;
            }
            address += 4;
            if (first && writeback) write_back(new_base);
            first = false;
            if (!stopped) {
                if (waiting >= 0) land(waiting, waiting_value);
                waiting = i;
                waiting_value = value;
            }
        }
        if (!stopped && waiting >= 0) land(waiting, waiting_value);
        // The internal cycle after the last word.
        bus_.internal(pc_from_instruction(12));

        // After a fault the base is updated even if the list loaded over it. Without writeback,
        // StopOverwriting puts the old base back.
        const bool faulted = address_fault || data_abort;
        if (faulted && rn != 15) {
            if (writeback) {
                write_back(new_base);
            } else if (!complete) {
                reg(rn) = base;
            }
        }

        // DOC, SW p.40: after a fault the PC is not loaded, so the handler can return to the LDM.
        if (r15_loaded && !faulted) {
            // With the S bit, the flags and mode are loaded with R15, as far as write_psr allows
            // in the current mode.
            if (psr_bit) write_psr(loaded_r15, true);
            set_pc(loaded_r15);
            refill_pipeline();
        }
    }

    // After the transfer, an address exception or a data abort is entered, with R14 the
    // instruction + 8, so that the handler subtracts 8 to run the LDM or STM again.
    if (address_fault) {
        enter(kVecAddress, kSvc, false, pc_from_instruction(8));
    } else if (data_abort) {
        enter(kVecDataAbort, kSvc, false, pc_from_instruction(8));  // DOC, SW p.29: "subtract 8".
    }
}
