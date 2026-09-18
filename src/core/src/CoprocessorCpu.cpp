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

#include <beebium/tube/CoprocessorCpu.hpp>

namespace beebium {

CoprocessorCpu::CoprocessorCpu(CoprocessorMemoryMap& memory, TubeCoprocessorBackend& tube_port)
    : memory_(memory)
    , tube_port_(tube_port)
    , cycle_count_(0)
{
    M6502_Init(&cpu_, &M6502_rockwell65c02_config);
}

CoprocessorCpu::~CoprocessorCpu() {
    M6502_Destroy(&cpu_);
}

void CoprocessorCpu::reset() {
    // The host's RST line propagates through the Tube cable to the parasite, so
    // a parasite reset is a 6502 RES, not a power-on: M6502_Reset ONLY, which
    // preserves the registers and flags and sets the interrupt-disable flag
    // (config/fns were established by the constructor's M6502_Init). Using
    // M6502_Init here would memset the CPU -- a power-on state -- and clear the
    // interrupt-disable flag, so a parasite reset with PIRQ asserted could leave
    // reset through the IRQ vector instead of the reset handler (issue #78).
    M6502_Reset(&cpu_);
    memory_.reset();
    tube_port_.reset();
    cycle_count_ = 0;
    in_nmi_handler_ = false;
}

void CoprocessorCpu::tick() {
    // Detect NMI entry BEFORE tfn runs.  The 65C02's Cycle0_InterruptCMOS
    // clears nmi_flags and changes read to M6502ReadType_Instruction at T0,
    // destroying the evidence before we could check it post-tfn.  At this
    // point M6502_NextInstruction has set read=Interrupt and nmi_flags!=0,
    // but tfn hasn't executed yet.
    const bool entering_nmi = !in_nmi_handler_
                           && cpu_.read == M6502ReadType_Interrupt
                           && cpu_.nmi_flags != 0;

    // Execute one CPU cycle (determines address and r/w direction)
    (*cpu_.tfn)(&cpu_);

    // Perform bus access through the memory map.
    // Uninteresting reads (e.g. page-cross fixup cycles) use peek to
    // avoid Tube register side effects. This prevents the CE2023 hang
    // where a fixup-cycle read consumed a Tube R1 latch byte.
    const uint16_t addr = cpu_.abus.w;
    if (cpu_.read) {
        cpu_.dbus = (cpu_.read == M6502ReadType_Uninteresting)
            ? memory_.peek(addr)
            : memory_.read(addr);
        // Memory read watchpoint: record reads from watched addresses
        // in the instruction trace as pseudo-entries with opcode=$FE.
        // Also records ad.w (the base address register) in the cycle field
        // for diagnosing addressing mode bugs.
        if (addr == watch_read_addr_ && trace_.enabled()) {
            trace_.record(cpu_.ad.w, cpu_.opcode_pc.w,
                          cpu_.dbus, cpu_.x, cpu_.y, cpu_.s.b.l,
                          M6502_GetP(&cpu_).value, 0xFE);
        }
    } else {
        memory_.write(addr, cpu_.dbus);
        // Memory write watchpoint: record writes to watched addresses
        // in the instruction trace as pseudo-entries with opcode=$FF.
        if (addr == watch_write_addr_ && trace_.enabled()) {
            trace_.record(cycle_count_, addr,
                          cpu_.dbus, cpu_.x, cpu_.y, cpu_.s.b.l,
                          M6502_GetP(&cpu_).value, 0xFF);
        }
    }

    // Record instruction trace at opcode fetch (dbus has the opcode,
    // opcode_pc has the address).  The branch-on-disabled check is the
    // only cost when tracing is off.
    if (M6502_IsAboutToExecute(&cpu_)) {
        trace_.record(cycle_count_, cpu_.opcode_pc.w,
                      cpu_.a, cpu_.x, cpu_.y, cpu_.s.b.l,
                      M6502_GetP(&cpu_).value, cpu_.dbus);
    }

    // Route Tube interrupt lines to CPU.
    // PIRQ is level-sensitive (directly drives IRQ).
    M6502_SetDeviceIRQ(&cpu_, kPirqMask, tube_port_.pirq() ? 1 : 0);

    // Commit NMI entry: clear device_nmi_flags so that reasserting PNMI
    // after RTI produces a clean 0-to-1 edge in M6502_SetDeviceNMI.
    if (entering_nmi) {
        in_nmi_handler_ = true;
        M6502_SetDeviceNMI(&cpu_, kPnmiMask, 0);
    }

    // Detect NMI handler exit: about to execute RTI (opcode $40).
    if (in_nmi_handler_ && M6502_IsAboutToExecute(&cpu_) && cpu_.dbus == 0x40) {
        in_nmi_handler_ = false;
    }

    // PNMI: only forward the level to M6502 when not inside an NMI handler.
    // This prevents NMI nesting in the cross-process Tube model where the
    // host thread can write the next R3 byte before the coprocessor's NMI
    // handler completes.  When the handler returns (RTI), suppression ends
    // and the current PNMI level produces a clean edge if still asserted.
    if (!in_nmi_handler_) {
        M6502_SetDeviceNMI(&cpu_, kPnmiMask, tube_port_.pnmi_level() ? 1 : 0);
    }

    ++cycle_count_;
}

uint64_t CoprocessorCpu::step_instruction() {
    const uint64_t start = cycle_count_;
    do {
        tick();
    } while (!M6502_IsAboutToExecute(&cpu_));
    return cycle_count_ - start;
}

void CoprocessorCpu::run(uint64_t cycles) {
    const uint64_t target = cycle_count_ + cycles;
    while (cycle_count_ < target) {
        tick();
    }
}

}  // namespace beebium
