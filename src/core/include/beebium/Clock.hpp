#pragma once

#include "ClockBinding.hpp"
#include <tuple>
#include <cstdint>

namespace beebium {

namespace detail {

// Recursive dispatch for rising edge
template<size_t I, typename Tuple>
void dispatch_rising(Tuple& bindings, uint64_t cycle) {
    if constexpr (I < std::tuple_size_v<Tuple>) {
        auto& binding = std::get<I>(bindings);
        if (binding.should_tick(cycle)) {
            binding.tick_rising();
        }
        dispatch_rising<I + 1>(bindings, cycle);
    }
}

// Recursive dispatch for falling edge
template<size_t I, typename Tuple>
void dispatch_falling(Tuple& bindings, uint64_t cycle) {
    if constexpr (I < std::tuple_size_v<Tuple>) {
        auto& binding = std::get<I>(bindings);
        if (binding.should_tick(cycle)) {
            binding.tick_falling();
        }
        dispatch_falling<I + 1>(bindings, cycle);
    }
}

} // namespace detail

// Clock template - holds bindings to all subscribed devices
// Devices are ticked on their subscribed edges at their declared rates
//
// Usage:
//   auto clock = make_clock(
//       make_clock_binding(system_via),
//       make_clock_binding(user_via),
//       make_clock_binding(crtc)
//   );
//   clock.tick(cycle_count);
//
template<typename... Bindings>
class Clock {
    std::tuple<Bindings...> bindings_;

public:
    explicit Clock(Bindings... bindings)
        : bindings_{std::move(bindings)...} {}

    // Tick all subscribers for the given cycle
    // Rising edge occurs on odd cycles, falling edge on even cycles
    void tick(uint64_t cycle) {
        bool phi2_rising = (cycle & 1) != 0;

        if (phi2_rising) {
            detail::dispatch_rising<0>(bindings_, cycle);
        } else {
            detail::dispatch_falling<0>(bindings_, cycle);
        }
    }

    // Access specific binding by index (for testing/debugging)
    template<size_t I>
    auto& get() { return std::get<I>(bindings_); }

    template<size_t I>
    const auto& get() const { return std::get<I>(bindings_); }

    // Number of subscribers
    static constexpr size_t size() { return sizeof...(Bindings); }
};

// Deduction guide for Clock
template<typename... Bindings>
Clock(Bindings...) -> Clock<Bindings...>;

// Helper to create clock with bindings
template<typename... Bindings>
constexpr auto make_clock(Bindings... bindings) {
    return Clock<Bindings...>{std::move(bindings)...};
}

} // namespace beebium
