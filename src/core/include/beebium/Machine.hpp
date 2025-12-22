#ifndef BEEBIUM_MACHINE_HPP
#define BEEBIUM_MACHINE_HPP

#include "Types.hpp"

#include <6502/6502.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

namespace beebium {

// Watchpoint callback: addr, value, is_write, cycle
using WatchCallback = std::function<void(uint16_t addr, uint8_t value, bool is_write, uint64_t cycle)>;

// PC callback: called before each instruction executes
using InstructionCallback = std::function<bool(uint16_t pc, uint64_t cycle)>;  // return false to stop

// Simple watchpoint structure
struct Watchpoint {
    uint32_t start_addr;
    uint32_t end_addr;  // exclusive, allows 0x10000 for full address space
    WatchType type;
    WatchCallback callback;

    Watchpoint(uint32_t start, uint32_t end, WatchType t, WatchCallback cb)
        : start_addr(start), end_addr(end), type(t), callback(std::move(cb))
    {
        assert(start_addr <= 0xFFFF && "start_addr must fit in 16-bit address space");
        assert(end_addr <= 0x10000 && "end_addr must be <= 0x10000");
        assert(start_addr < end_addr && "start_addr must be less than end_addr");
    }

    bool matches(uint16_t addr, bool is_write) const {
        if (addr < start_addr || addr >= end_addr) return false;
        if (is_write && (type & WATCH_WRITE)) return true;
        if (!is_write && (type & WATCH_READ)) return true;
        return false;
    }
};

// Type trait to detect if MemoryPolicy has tick_peripherals method
template<typename T, typename = void>
struct has_tick_peripherals : std::false_type {};

template<typename T>
struct has_tick_peripherals<T,
    std::void_t<decltype(std::declval<T>().tick_peripherals(uint64_t{}))>
> : std::true_type {};

template<typename T>
inline constexpr bool has_tick_peripherals_v = has_tick_peripherals<T>::value;

// Machine state that can be serialized/deserialized.
// Parameterized by MemoryPolicy to include memory state.
template<typename MemoryPolicy>
struct MachineState {
    M6502 cpu{};
    MemoryPolicy memory;
    uint64_t cycle_count = 0;
};

// Core BBC Micro emulator, parameterized by CPU and Memory policies.
//
// CpuPolicy must provide:
//   - static constexpr const M6502Config* config
//
// MemoryPolicy must provide:
//   - uint8_t read(uint16_t addr) const
//   - void write(uint16_t addr, uint8_t value)
//   - void reset()
//
template<typename CpuPolicy, typename MemoryPolicy>
class Machine {
public:
    using State = MachineState<MemoryPolicy>;

    Machine() {
        reset();
    }

    ~Machine() {
        M6502_Destroy(&state_.cpu);
    }

    // Non-copyable (contains M6502 with pointers)
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    // Reset to power-on state
    void reset() {
        M6502_Init(&state_.cpu, CpuPolicy::config);
        M6502_Reset(&state_.cpu);
        state_.memory.reset();
        state_.cycle_count = 0;
    }

    // Execute one CPU cycle
    void step() {
        (*state_.cpu.tfn)(&state_.cpu);

        const uint16_t addr = state_.cpu.abus.w;
        const bool is_read = state_.cpu.read;

        if (is_read) {
            state_.cpu.dbus = state_.memory.read(addr);
        } else {
            state_.memory.write(addr, state_.cpu.dbus);
        }

        // Fire watchpoints
        if (!watchpoints_.empty()) {
            for (const auto& wp : watchpoints_) {
                if (wp.matches(addr, !is_read)) {
                    wp.callback(addr, state_.cpu.dbus, !is_read, state_.cycle_count);
                }
            }
        }

        // Update peripherals if supported by the memory policy
        if constexpr (has_tick_peripherals_v<MemoryPolicy>) {
            uint8_t irq_mask = state_.memory.tick_peripherals(state_.cycle_count);
            // Set IRQ line based on VIA pending flags
            // Device bit 0 = System VIA, bit 1 = User VIA
            M6502_SetDeviceIRQ(&state_.cpu, 0x03, irq_mask ? 1 : 0);
        }

        ++state_.cycle_count;
    }

    // Execute for the given number of cycles
    void run(uint64_t cycles) {
        const uint64_t target = state_.cycle_count + cycles;
        while (state_.cycle_count < target) {
            step();
        }
    }

    // Execute one complete instruction (variable cycles)
    // Returns the number of cycles taken
    uint64_t step_instruction() {
        const uint64_t start = state_.cycle_count;
        do {
            step();
        } while (!M6502_IsAboutToExecute(&state_.cpu));
        return state_.cycle_count - start;
    }

    // State access
    const State& state() const { return state_; }
    State& state() { return state_; }

    // CPU access
    const M6502& cpu() const { return state_.cpu; }
    M6502& cpu() { return state_.cpu; }

    // Memory access
    const MemoryPolicy& memory() const { return state_.memory; }
    MemoryPolicy& memory() { return state_.memory; }

    // Cycle counter
    uint64_t cycle_count() const { return state_.cycle_count; }

    // Direct memory access (convenience)
    // Note: read() is non-const because some devices have read side effects (e.g., VIA interrupt flags)
    uint8_t read(uint16_t addr) { return state_.memory.read(addr); }
    void write(uint16_t addr, uint8_t value) { state_.memory.write(addr, value); }

    // Watchpoint management
    void add_watchpoint(uint32_t addr, uint32_t length, WatchType type, WatchCallback callback) {
        watchpoints_.emplace_back(addr, addr + length, type, std::move(callback));
    }

    void clear_watchpoints() { watchpoints_.clear(); }

    const std::vector<Watchpoint>& watchpoints() const { return watchpoints_; }

    // Instruction callback
    void set_instruction_callback(InstructionCallback cb) { on_instruction_ = std::move(cb); }

    void clear_callbacks() {
        on_instruction_ = nullptr;
        watchpoints_.clear();
    }

    // Execute one complete instruction with optional callback
    // Returns false if callback requested stop, true otherwise
    bool step_instruction_debug() {
        if (on_instruction_) {
            if (!on_instruction_(state_.cpu.pc.w, state_.cycle_count)) {
                return false;  // Callback requested stop
            }
        }
        step_instruction();
        return true;
    }

private:
    State state_;
    std::vector<Watchpoint> watchpoints_;
    InstructionCallback on_instruction_;
};

} // namespace beebium

#endif // BEEBIUM_MACHINE_HPP
