#ifndef BEEBIUM_HPP
#define BEEBIUM_HPP

#include "BeebiumState.hpp"
#include <cstdint>
#include <cstddef>

namespace beebium {

// BBC Micro emulator core.
// Provides the runtime interface for the emulator.
class Beebium {
public:
    Beebium();
    ~Beebium();

    // Non-copyable
    Beebium(const Beebium&) = delete;
    Beebium& operator=(const Beebium&) = delete;

    // Reset the emulator to power-on state
    void reset();

    // Execute one CPU cycle
    void step();

    // Execute until the given number of cycles have elapsed
    void run(uint64_t cycles);

    // Execute one complete instruction (variable number of cycles)
    // Returns the number of cycles taken
    uint64_t step_instruction();

    // State access
    const BeebiumState& state() const { return state_; }
    void load_state(const BeebiumState& state);

    // Direct memory access (for testing/debugging)
    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);

    // ROM loading
    void load_mos(const uint8_t* data, size_t size);
    void load_rom_bank(RomBankIndex bank, const uint8_t* data, size_t size);

    // CPU access
    const M6502& cpu() const { return state_.cpu; }
    M6502& cpu() { return state_.cpu; }

    // Cycle counter
    uint64_t cycle_count() const { return state_.cycle_count; }

private:
    BeebiumState state_;
};

} // namespace beebium

#endif // BEEBIUM_HPP
