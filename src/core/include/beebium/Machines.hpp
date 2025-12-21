#ifndef BEEBIUM_MACHINES_HPP
#define BEEBIUM_MACHINES_HPP

#include "Machine.hpp"
#include "CpuPolicy.hpp"
#include "ModelBMemory.hpp"

namespace beebium {

// Convenience type aliases for common machine configurations.
// New configurations can be created by composing policies.

// BBC Model B: NMOS 6502 + standard Model B memory
using ModelB = Machine<Nmos6502, ModelBMemory>;

// BBC Model B with CMOS upgrade (e.g., for testing)
using ModelBCmos = Machine<Cmos65C02, ModelBMemory>;

} // namespace beebium

#endif // BEEBIUM_MACHINES_HPP
