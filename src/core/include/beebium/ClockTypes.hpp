#pragma once

#include <cstdint>

namespace beebium {

// Clock edge types for subscription
// Used as bitmask for declaring which edges a device needs
enum class ClockEdge : uint8_t {
    None    = 0x00,
    Rising  = 0x01,   // PHI2 rising (odd cycles)
    Falling = 0x02,   // PHI2 falling (even cycles)
    Both    = 0x03,   // Both edges
};

constexpr ClockEdge operator|(ClockEdge a, ClockEdge b) {
    return static_cast<ClockEdge>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr ClockEdge operator&(ClockEdge a, ClockEdge b) {
    return static_cast<ClockEdge>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr bool has_edge(ClockEdge required, ClockEdge edge) {
    return (static_cast<uint8_t>(required) & static_cast<uint8_t>(edge)) != 0;
}

// Clock rate for devices
enum class ClockRate : uint8_t {
    Rate_1MHz = 1,   // 1MHz peripheral clock (VIAs, etc.)
    Rate_2MHz = 2,   // 2MHz CPU clock
};

// BBC Micro timing constants
namespace timing {
    constexpr uint64_t MASTER_HZ     = 16'000'000;  // 16MHz master crystal
    constexpr uint64_t RAM_HZ        = 4'000'000;   // 4MHz RAM access
    constexpr uint64_t CPU_HZ        = 2'000'000;   // 2MHz CPU clock
    constexpr uint64_t PERIPHERAL_HZ = 1'000'000;   // 1MHz peripherals

    // Cycles per second at our native rate (2MHz)
    constexpr uint64_t CYCLES_PER_SECOND = CPU_HZ;
}

} // namespace beebium
