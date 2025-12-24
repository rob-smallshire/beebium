// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <concepts>
#include <cstdint>
#include <tuple>

namespace beebium {

// Concept for devices that can generate IRQs
template<typename T>
concept IrqSource = requires(const T& device) {
    { device.irq_pending() } -> std::convertible_to<bool>;
};

// IrqBinding associates a device with its IRQ bit position
template<typename Device, uint8_t Bit>
    requires IrqSource<Device>
struct IrqBinding {
    Device& device;
    static constexpr uint8_t bit = Bit;

    uint8_t poll() const {
        return device.irq_pending() ? (1 << bit) : 0;
    }
};

namespace detail {

// Recursive fold to aggregate all IRQ bits
template<size_t I, typename Tuple>
uint8_t fold_irqs(const Tuple& bindings) {
    if constexpr (I < std::tuple_size_v<Tuple>) {
        return std::get<I>(bindings).poll() | fold_irqs<I + 1>(bindings);
    } else {
        return 0;
    }
}

} // namespace detail

// IrqAggregator - collects IRQ status from all bound devices
// Usage:
//   auto aggregator = make_irq_aggregator(
//       make_irq_binding<0>(system_via),
//       make_irq_binding<1>(user_via)
//   );
//   uint8_t irq_mask = aggregator.poll();
//
template<typename... Bindings>
class IrqAggregator {
    std::tuple<Bindings...> bindings_;

public:
    explicit IrqAggregator(Bindings... bindings)
        : bindings_{std::move(bindings)...} {}

    // Poll all IRQ sources and return aggregated mask
    uint8_t poll() const {
        return detail::fold_irqs<0>(bindings_);
    }

    // Number of IRQ sources
    static constexpr size_t size() { return sizeof...(Bindings); }

    // Access specific binding by index (for testing/debugging)
    template<size_t I>
    auto& get() { return std::get<I>(bindings_); }

    template<size_t I>
    const auto& get() const { return std::get<I>(bindings_); }
};

// Deduction guide for IrqAggregator
template<typename... Bindings>
IrqAggregator(Bindings...) -> IrqAggregator<Bindings...>;

// Helper to create IRQ binding with specified bit position
template<uint8_t Bit, IrqSource Device>
constexpr auto make_irq_binding(Device& device) {
    return IrqBinding<Device, Bit>{device};
}

// Helper to create aggregator with bindings
template<typename... Bindings>
constexpr auto make_irq_aggregator(Bindings... bindings) {
    return IrqAggregator<Bindings...>{std::move(bindings)...};
}

} // namespace beebium
