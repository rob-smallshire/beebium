#ifndef BEEBIUM_STATE_HPP
#define BEEBIUM_STATE_HPP

#include "Types.hpp"
#include "Memory.hpp"
#include <6502/6502.h>
#include <cstdint>

namespace beebium {

// Serializable emulator state.
// Contains all state needed to save/restore the emulator.
struct BeebiumState {
    // 6502 CPU state
    M6502 cpu{};

    // Memory system state
    Memory memory;

    // Cycle counter (total cycles since reset)
    uint64_t cycle_count = 0;

    // Reset the state to power-on defaults
    void reset();
};

} // namespace beebium

#endif // BEEBIUM_STATE_HPP
