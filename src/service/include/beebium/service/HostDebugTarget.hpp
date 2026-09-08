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

#pragma once

#include "beebium/extension/CpuDebugTarget.hpp"
#include "beebium/Cpu6502Descriptor.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace beebium {

// Detects a host memory map whose routing depends on the program counter (the
// B+ shadow-RAM modes). Only such maps get PC-aware access; the rest fall back
// to the plain access, as they did before the debugger was de-templated.
template <typename T>
concept HostHasPcAwareMemory = requires(T& m, uint16_t addr, uint16_t pc, uint8_t val) {
    { m.read_with_pc(addr, pc) } -> std::same_as<uint8_t>;
    { m.write_with_pc(addr, val, pc) } -> std::same_as<void>;
};

// Adapts the host Machine to the CpuDebugTarget interface so the one debugger
// service serves the host through the same contract as a coprocessor. The
// Machine template itself carries no debug vtable: this thin adapter forwards
// each call, and only the debugger holds one. The host CPU is a 6502, so it
// reuses the shared 6502 descriptor and register/signal mapping.
template <typename MachineType>
class HostDebugTarget final : public CpuDebugTarget {
public:
    explicit HostDebugTarget(MachineType& machine) : machine_(machine) {}

    // --- CPU description and register/signal access ---
    const cpu::CpuDescriptor& cpu_descriptor() const override { return cpu6502_descriptor(); }
    uint64_t register_value(size_t index) const override {
        return cpu6502_register_value(index, machine_);
    }
    void set_register_value(size_t index, uint64_t value) override {
        cpu6502_set_register_value(index, machine_, value);
    }
    cpu::SignalStateValue signal_state(size_t index) const override {
        return cpu6502_signal_state(index, machine_.cpu(), machine_.p(),
                                    machine_.in_nmi_handler(), machine_.in_irq_handler());
    }

    // --- Execution control ---
    uint64_t cycle_count() const override { return machine_.cycle_count(); }
    uint64_t sequence() const override { return machine_.sequence(); }
    bool is_paused() const override { return machine_.is_paused(); }
    void pause() override { machine_.pause(); }
    void resume() override { machine_.resume(); }
    void reset() override { machine_.reset(); }
    void step() override { machine_.step(); }
    uint64_t step_instruction() override { return machine_.step_instruction(); }
    void prepare_for_step() override { machine_.prepare_for_step(); }
    void wait_until_idle() override { machine_.wait_until_idle(); }
    void finish_step() override { machine_.finish_step(); }

    // --- Flat memory access ---
    uint8_t read(uint16_t addr) override { return machine_.read(addr); }
    uint8_t peek(uint16_t addr) const override { return machine_.peek(addr); }
    void write(uint16_t addr, uint8_t value) override { machine_.write(addr, value); }

    // The host's memory routing depends on the program counter (shadow-RAM
    // modes), so it overrides the PC-aware access. peek_with_pc uses the
    // side-effect-free PC-aware read.
    uint8_t read_with_pc(uint16_t addr, uint16_t pc) override {
        if constexpr (HostHasPcAwareMemory<decltype(machine_.memory())>) {
            return machine_.memory().read_with_pc(addr, pc);
        } else {
            return machine_.read(addr);
        }
    }
    uint8_t peek_with_pc(uint16_t addr, uint16_t pc) const override {
        if constexpr (HostHasPcAwareMemory<decltype(machine_.memory())>) {
            return machine_.memory().read_with_pc(addr, pc);
        } else {
            return machine_.peek(addr);
        }
    }
    void write_with_pc(uint16_t addr, uint8_t value, uint16_t pc) override {
        if constexpr (HostHasPcAwareMemory<decltype(machine_.memory())>) {
            machine_.memory().write_with_pc(addr, value, pc);
        } else {
            machine_.write(addr, value);
        }
    }

    // --- Memory-region model ---
    std::vector<MemoryRegionDescriptor> get_memory_regions() const override {
        return machine_.memory().get_memory_regions();
    }
    uint8_t peek_region(std::string_view name, uint32_t address) const override {
        return machine_.memory().peek_region(name, address);
    }
    uint8_t read_region(std::string_view name, uint32_t address) override {
        return machine_.memory().read_region(name, address);
    }
    void write_region(std::string_view name, uint32_t address, uint8_t value) override {
        machine_.memory().write_region(name, address, value);
    }
    std::string_view machine_type() const override { return machine_.memory().machine_type(); }

    // --- Breakpoints ---
    const std::vector<BreakpointEntry>& breakpoint_entries() const override {
        return machine_.breakpoint_entries();
    }
    void set_breakpoint_entries(std::vector<BreakpointEntry> entries) override {
        machine_.set_breakpoint_entries(std::move(entries));
    }
    void set_breakpoint_hit_callback(BreakpointHitCallback cb) override {
        machine_.set_breakpoint_hit_callback(std::move(cb));
    }

    // --- Watchpoints ---
    const std::vector<WatchpointEntry>& watchpoint_entries() const override {
        return machine_.watchpoint_entries();
    }
    void set_watchpoint_entries(std::vector<WatchpointEntry> entries) override {
        machine_.set_watchpoint_entries(std::move(entries));
    }
    void set_watchpoint_hit_callback(WatchpointHitCallback cb) override {
        machine_.set_watchpoint_hit_callback(std::move(cb));
    }

private:
    MachineType& machine_;
};

}  // namespace beebium
