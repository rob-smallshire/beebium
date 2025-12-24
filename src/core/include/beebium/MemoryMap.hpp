#pragma once

#include <cstdint>
#include <concepts>
#include <tuple>
#include <type_traits>

namespace beebium {

// Concept for any device that can be memory-mapped
template<typename T>
concept MemoryMappedDevice = requires(T& device, uint16_t offset, uint8_t value) {
    { device.read(offset) } -> std::convertible_to<uint8_t>;
    { device.write(offset, value) } -> std::same_as<void>;
};

// Mirror policies for address decoding

struct NoMirror {
    static constexpr uint16_t apply(uint16_t offset) noexcept { return offset; }
};

template<uint16_t Mask>
struct Mirror {
    static constexpr uint16_t apply(uint16_t offset) noexcept { return offset & Mask; }
};

// Region template: binds an address range to a device reference
template<uint16_t Base, uint16_t End, typename MirrorPolicy = NoMirror>
struct Region {
    static_assert(Base <= End, "Region base must be <= end");

    static constexpr uint16_t base = Base;
    static constexpr uint16_t end = End;
    static constexpr uint16_t size = End - Base + 1;

    template<MemoryMappedDevice Device>
    struct Binding {
        Device& device;

        constexpr bool contains(uint16_t addr) const noexcept {
            return addr >= Base && addr <= End;
        }

        uint8_t read(uint16_t addr) const {
            return device.read(MirrorPolicy::apply(addr - Base));
        }

        void write(uint16_t addr, uint8_t value) {
            device.write(MirrorPolicy::apply(addr - Base), value);
        }
    };

    template<MemoryMappedDevice Device>
    static constexpr Binding<Device> bind(Device& device) {
        return Binding<Device>{device};
    }
};

// Helper to create a region binding with deduced device type
template<uint16_t Base, uint16_t End, typename MirrorPolicy = NoMirror, MemoryMappedDevice Device>
constexpr auto make_region(Device& device) {
    return Region<Base, End, MirrorPolicy>::bind(device);
}

namespace detail {

// Recursive dispatch for read
template<size_t I, typename Tuple>
uint8_t dispatch_read(const Tuple& regions, uint16_t addr) {
    if constexpr (I < std::tuple_size_v<Tuple>) {
        const auto& region = std::get<I>(regions);
        if (region.contains(addr)) {
            return region.read(addr);
        }
        return dispatch_read<I + 1>(regions, addr);
    } else {
        return 0xFF;  // Unmapped address
    }
}

// Recursive dispatch for write
template<size_t I, typename Tuple>
void dispatch_write(Tuple& regions, uint16_t addr, uint8_t value) {
    if constexpr (I < std::tuple_size_v<Tuple>) {
        auto& region = std::get<I>(regions);
        if (region.contains(addr)) {
            region.write(addr, value);
            return;
        }
        dispatch_write<I + 1>(regions, addr, value);
    }
    // Unmapped write: silently ignored
}

} // namespace detail

// MemoryMap: variadic template composing multiple region bindings
// First matching region wins (order in constructor determines priority)
template<typename... RegionBindings>
class MemoryMap {
    std::tuple<RegionBindings...> regions_;

public:
    explicit MemoryMap(RegionBindings... regions)
        : regions_{std::move(regions)...} {}

    uint8_t read(uint16_t addr) const {
        return detail::dispatch_read<0>(regions_, addr);
    }

    void write(uint16_t addr, uint8_t value) {
        detail::dispatch_write<0>(regions_, addr, value);
    }

    // Allow read-only access for debugging/inspection
    uint8_t operator[](uint16_t addr) const {
        return read(addr);
    }
};


// Deduction guide for MemoryMap
template<typename... RegionBindings>
MemoryMap(RegionBindings...) -> MemoryMap<RegionBindings...>;

} // namespace beebium
