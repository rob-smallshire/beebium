#pragma once

#include "ClockTypes.hpp"
#include <concepts>
#include <type_traits>

namespace beebium {

// Concept: Device declares which clock edges it needs via static member
template<typename T>
concept HasClockEdges = requires {
    { T::clock_edges } -> std::convertible_to<ClockEdge>;
};

// Concept: Device declares its clock rate via static member
template<typename T>
concept HasStaticClockRate = requires {
    { T::clock_rate } -> std::convertible_to<ClockRate>;
};

// Concept: Device has dynamic clock rate (runtime-determined)
template<typename T>
concept HasDynamicClockRate = requires(const T& device) {
    { device.clock_rate() } -> std::convertible_to<ClockRate>;
};

// Concept: Device has tick_rising() method for rising edge
template<typename T>
concept HasTickRising = requires(T& device) {
    { device.tick_rising() } -> std::same_as<void>;
};

// Concept: Device has tick_falling() method for falling edge
template<typename T>
concept HasTickFalling = requires(T& device) {
    { device.tick_falling() } -> std::same_as<void>;
};

// Helper to check if a device's declared edges include a specific edge
template<typename T>
    requires HasClockEdges<T>
constexpr bool device_has_edge(ClockEdge edge) {
    return has_edge(T::clock_edges, edge);
}

// Main concept: A valid clock subscriber
// - Must declare clock_edges
// - Must have tick_rising() if Rising declared
// - Must have tick_falling() if Falling declared
template<typename T>
concept ClockSubscriber = HasClockEdges<T> && (
    // If Rising edge declared, must have tick_rising()
    (!has_edge(T::clock_edges, ClockEdge::Rising) || HasTickRising<T>) &&
    // If Falling edge declared, must have tick_falling()
    (!has_edge(T::clock_edges, ClockEdge::Falling) || HasTickFalling<T>)
);

// Concept: Device with static clock rate (known at compile time)
template<typename T>
concept StaticRateSubscriber = ClockSubscriber<T> && HasStaticClockRate<T>;

// Concept: Device with dynamic clock rate (e.g., CRTC's 1/2MHz mode)
template<typename T>
concept DynamicRateSubscriber = ClockSubscriber<T> && HasDynamicClockRate<T>;

} // namespace beebium
