#pragma once

#include "ClockConcepts.hpp"
#include <cstdint>

namespace beebium {

// ClockBinding wraps a device reference and provides clock edge dispatch
// The binding knows the device's edge requirements at compile time
//
// PHI2 Clock Model:
// The BBC Micro runs on a 2MHz master clock. The 6502 CPU uses a two-phase
// clock (PHI1/PHI2). Peripherals can run at either 2MHz or 1MHz:
//
// - 2MHz devices (e.g., CPU): tick every cycle
// - 1MHz devices (e.g., VIAs): tick every other cycle (on even cycle numbers)
//
// Cycle numbering: The cycle counter starts at 0 and increments each 2MHz tick.
// 1MHz devices tick when (cycle & 1) == 0, which corresponds to:
//   cycle 0, 2, 4, 6, ... -> 1MHz tick
//   cycle 1, 3, 5, 7, ... -> no 1MHz tick
//
// This models the BBC Micro's 1MHz bus, where slower peripherals (VIA, CRTC
// at 1MHz) are clocked at half the CPU rate.
template<typename Device>
    requires ClockSubscriber<Device>
struct ClockBinding {
    Device& device;

    static constexpr ClockEdge edges = Device::clock_edges;

    // Tick on rising edge (if device subscribes to rising)
    void tick_rising() {
        if constexpr (has_edge(edges, ClockEdge::Rising)) {
            device.tick_rising();
        }
    }

    // Tick on falling edge (if device subscribes to falling)
    void tick_falling() {
        if constexpr (has_edge(edges, ClockEdge::Falling)) {
            device.tick_falling();
        }
    }

    // Check if device should tick at this cycle based on its clock rate
    // 2MHz devices tick every cycle; 1MHz devices tick every other cycle
    bool should_tick(uint64_t cycle) const {
        if constexpr (HasStaticClockRate<Device>) {
            // Static rate known at compile time
            if constexpr (Device::clock_rate == ClockRate::Rate_2MHz) {
                return true;  // Tick every cycle
            } else {
                return (cycle & 1) == 0;  // Tick on even cycles (1MHz)
            }
        } else if constexpr (HasDynamicClockRate<Device>) {
            // Dynamic rate - query device at runtime
            return device.clock_rate() == ClockRate::Rate_2MHz || (cycle & 1) == 0;
        } else {
            // No rate specified - default to 2MHz (tick every cycle)
            return true;
        }
    }
};

// Helper to create clock binding with deduced device type
template<ClockSubscriber Device>
constexpr auto make_clock_binding(Device& device) {
    return ClockBinding<Device>{device};
}

} // namespace beebium
