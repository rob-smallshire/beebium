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
// AesTube.hpp, the coprocessor's interface
// version 2.0.0, 20 November 2026.

// AesTube: the AES on the far side of Beebium's Tube. It owns the core, the memory and the clock,
// and runs them in step with the BBC. Beebium's own Tube chip, TubeUla, is linked and called,
// never copied.

#pragma once

#include "../AesMemory.hpp"
#include "../AesOptions.hpp"
#include "../AesTiming.hpp"
#include "../ArmCore.hpp"

#include <beebium/tube/Coprocessor.hpp>
#include <beebium/tube/CoprocessorClock.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <atomic>

// In the logged build (AES_TUBE_LOG) a TubeLog sits between the core, the board and the Tube
// chip: see TubeLog.hpp.
#ifdef AES_TUBE_LOG
#include "TubeLog.hpp"
#endif

// The debugger's breakpoints, as an interface so that AesTube needs nothing from the debugger.
// AesDebug implements it; AesTube asks it before each instruction while any breakpoint or
// watchpoint is set.
class TubeStops {
public:
    virtual ~TubeStops() = default;
    // Called with the address of the instruction about to run.
    virtual void before_instruction(uint32_t pc) = 0;
};

// The AES as Beebium runs it. It implements Beebium's Coprocessor interface, through which Beebium
// runs it against the host's clock, and TubePort (AesMemory.hpp), through which its memory reaches
// the Tube chip. AesExtension owns it; AesDebug works through it.
class AesTube : public beebium::Coprocessor, public TubePort {
public:
    // The board for the switches in `options`, its Tube on Beebium's chip `ula`.
    AesTube(const AesOptions& options, beebium::TubeUla& ula);

    // The ROM image: from the file the rom setting names, or given directly by a test.
    bool load_rom() { return memory_.load_rom(); }
    void set_rom(const std::vector<uint8_t>& image) { memory_.set_rom(image); }

    // Beebium's Coprocessor interface: run up to the host's time, pause, resume, reset.
    void run_until(uint64_t host_cycle) override;
    // paused_ is atomic because the debugger pauses from its own thread.
    void pause() override { paused_ = true; }
    void resume() override { paused_ = false; }
    bool is_paused() const override { return paused_; }
    void reset() override;
    uint64_t step_one();  // The debugger's step: one instruction; returns the ticks it cost.
    void set_stops(TubeStops* stops) { stops_ = stops; }  // Null while none is set.
    // The board's timing as Beebium is told it: a label only (see AesTube.cpp).
    beebium::BoardTiming board_timing() const override { return board_label(); }
    static const beebium::BoardTiming& board_label();

    // TubePort: the memory's register accesses, passed on to Beebium's Tube chip.
    uint8_t read(int reg) override { return ula_.coprocessor_read(uint8_t(reg)); }
    void write(int reg, uint8_t value) override { ula_.coprocessor_write(uint8_t(reg), value); }
    // A look at a register that changes nothing, for the debugger.
    uint8_t peek(int reg) const override { return ula_.coprocessor_peek(uint8_t(reg)); }

    // For the debugger and the tests: the time budget, instructions run, and the parts.
    uint64_t ticks_due() const { return ticks_due_; }
    uint64_t ticks_spent() const { return timing_.ticks(); }
    uint64_t instructions() const { return instructions_; }
    ArmCore& core() { return core_; }
    AesMemory& memory() { return memory_; }

private:
    // Follows the host's PRST line; true while it holds the ARM.
    bool held_by_prst();
    // Resets the core and the memory together.
    void restart();
    // The most a Tube instruction can cost, for the Tube wait.
    int tube_instruction_cost(uint32_t at, int words) const;
    // Whether the next instruction must wait for more host time.
    bool must_wait_for_tube();
    // One instruction with breakpoints or watchpoints set.
    bool step_watched();

    const AesOptions opt_;
    beebium::TubeUla& ula_;
    AesTiming timing_;
#ifdef AES_TUBE_LOG
    TubeLog log_;  // The test build only.
#endif
    AesMemory memory_;
    ArmCore core_;
    // Turns the host's time into ticks.
    beebium::CoprocessorClock clock_;
    // The ticks the ARM may run to: the host's time so far, less what was dropped.
    uint64_t ticks_due_ = 0;
    bool prst_was_active_ = false;
    uint64_t instructions_ = 0;  // Steps of the core, for the debugger's sequence().
    TubeStops* stops_ = nullptr;
    std::atomic<bool> paused_ = false;
};
