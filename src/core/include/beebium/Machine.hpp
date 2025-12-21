#ifndef BEEBIUM_MACHINE_HPP
#define BEEBIUM_MACHINE_HPP

#include <6502/6502.h>
#include <cstdint>
#include <cstddef>

namespace beebium {

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

        if (state_.cpu.read) {
            state_.cpu.dbus = state_.memory.read(state_.cpu.abus.w);
        } else {
            state_.memory.write(state_.cpu.abus.w, state_.cpu.dbus);
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
    uint8_t read(uint16_t addr) const { return state_.memory.read(addr); }
    void write(uint16_t addr, uint8_t value) { state_.memory.write(addr, value); }

private:
    State state_;
};

} // namespace beebium

#endif // BEEBIUM_MACHINE_HPP
