#include "beebium/BeebiumState.hpp"

namespace beebium {

void BeebiumState::reset() {
    // Initialize CPU with NMOS 6502 configuration
    M6502_Init(&cpu, &M6502_nmos6502_config);
    M6502_Reset(&cpu);

    // Reset memory
    memory.reset();

    // Reset cycle counter
    cycle_count = 0;
}

} // namespace beebium
