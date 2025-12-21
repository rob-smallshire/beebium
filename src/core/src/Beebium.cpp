#include "beebium/Beebium.hpp"

namespace beebium {

Beebium::Beebium() {
    reset();
}

Beebium::~Beebium() {
    M6502_Destroy(&state_.cpu);
}

void Beebium::reset() {
    state_.reset();
}

void Beebium::step() {
    // Execute one cycle of the 6502
    // The tfn (timing function) advances the CPU by one cycle
    (*state_.cpu.tfn)(&state_.cpu);

    // Handle the memory access based on read/write
    if (state_.cpu.read) {
        // CPU is reading from memory
        state_.cpu.dbus = state_.memory.read(state_.cpu.abus.w);
    } else {
        // CPU is writing to memory
        state_.memory.write(state_.cpu.abus.w, state_.cpu.dbus);
    }

    state_.cycle_count++;
}

void Beebium::run(uint64_t cycles) {
    uint64_t target = state_.cycle_count + cycles;
    while (state_.cycle_count < target) {
        step();
    }
}

uint64_t Beebium::step_instruction() {
    uint64_t start_cycles = state_.cycle_count;

    // Execute cycles until we're about to execute the next instruction
    // The 6502 sets read to M6502ReadType_Opcode when ready for next instruction
    do {
        step();
    } while (!M6502_IsAboutToExecute(&state_.cpu));

    return state_.cycle_count - start_cycles;
}

void Beebium::load_state(const BeebiumState& state) {
    state_ = state;
}

uint8_t Beebium::read(uint16_t addr) const {
    return state_.memory.read(addr);
}

void Beebium::write(uint16_t addr, uint8_t value) {
    state_.memory.write(addr, value);
}

void Beebium::load_mos(const uint8_t* data, size_t size) {
    state_.memory.load_mos(data, size);
}

void Beebium::load_rom_bank(RomBankIndex bank, const uint8_t* data, size_t size) {
    state_.memory.load_rom_bank(bank, data, size);
}

} // namespace beebium
