#ifndef BEEBIUM_MACHINES_HPP
#define BEEBIUM_MACHINES_HPP

#include "Machine.hpp"
#include "CpuPolicy.hpp"
#include "ModelBHardware.hpp"

namespace beebium {

// Convenience type aliases for common machine configurations.
// New configurations can be created by composing policies.

// BBC Model B: NMOS 6502 + Model B hardware
using ModelB = Machine<Nmos6502, ModelBHardware>;

// BBC Model B with CMOS 65C02 upgrade
using ModelBCmos = Machine<Cmos65C02, ModelBHardware>;

} // namespace beebium

#endif // BEEBIUM_MACHINES_HPP
