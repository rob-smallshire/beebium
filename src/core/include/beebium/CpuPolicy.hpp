#ifndef BEEBIUM_CPU_POLICY_HPP
#define BEEBIUM_CPU_POLICY_HPP

#include <6502/6502.h>

namespace beebium {

// CPU policies select the 6502 variant at compile time.
// Each policy provides a static config pointer to the M6502Config.

struct Nmos6502 {
    static constexpr const M6502Config* config = &M6502_nmos6502_config;
    static constexpr const char* name = "NMOS 6502";
};

struct Cmos65C02 {
    static constexpr const M6502Config* config = &M6502_cmos6502_config;
    static constexpr const char* name = "CMOS 65C02";
};

struct Rockwell65C02 {
    static constexpr const M6502Config* config = &M6502_rockwell65c02_config;
    static constexpr const char* name = "Rockwell 65C02";
};

} // namespace beebium

#endif // BEEBIUM_CPU_POLICY_HPP
