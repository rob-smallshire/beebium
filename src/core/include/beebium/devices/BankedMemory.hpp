#pragma once

#include "../BankBinding.hpp"
#include "EmptySlot.hpp"
#include <array>
#include <cstdint>
#include <tuple>
#include <utility>

namespace beebium {

// BankedMemory: 16 ROM/RAM banks with compile-time typed devices and
// runtime bank selection via ROMSEL.
//
// Design:
// - Slot contents are configured at compile time (design-time configuration)
// - Bank selection (ROMSEL) is runtime, using function pointer dispatch
// - Unpopulated banks automatically return 0xFF (empty slot behavior)
// - Supports recursive composition: any MemoryMappedDevice can be a bank,
//   including nested MemoryMaps (e.g., PALPROM = PAL overlaying ROM)
//
// Usage:
//   Rom<16384> basic_rom, dfs_rom;
//   Ram<16384> sideways_ram;
//
//   BankedMemory sideways{
//       make_bank<0>(basic_rom),
//       make_bank<1>(dfs_rom),
//       make_bank<4>(sideways_ram)
//   };
//
//   sideways.select_bank(0);
//   uint8_t byte = sideways.read(0x0000);  // Reads from basic_rom
//
template<typename... BankBindings>
class BankedMemory {
    static_assert(sizeof...(BankBindings) <= 16, "Maximum 16 banks allowed");

    std::tuple<BankBindings...> bindings_;
    uint8_t selected_bank_ = 0;

    // Function pointer types for dispatch
    using ReadFn = uint8_t (*)(const void*, uint16_t);
    using WriteFn = void (*)(void*, uint16_t, uint8_t);

    // Dispatch tables and device pointers for all 16 banks
    std::array<ReadFn, 16> read_table_{};
    std::array<WriteFn, 16> write_table_{};
    std::array<void*, 16> device_ptrs_{};

    // Empty slot handlers
    static uint8_t empty_read(const void*, uint16_t) { return 0xFF; }
    static void empty_write(void*, uint16_t, uint8_t) {}

public:
    static constexpr size_t num_banks = 16;
    static constexpr size_t bank_size = 16384;

    explicit BankedMemory(BankBindings... bindings)
        : bindings_{std::move(bindings)...}
    {
        // Initialize all 16 banks to empty
        for (size_t i = 0; i < 16; ++i) {
            read_table_[i] = empty_read;
            write_table_[i] = empty_write;
            device_ptrs_[i] = nullptr;
        }

        // Override with provided bindings
        init_banks(std::make_index_sequence<sizeof...(BankBindings)>{});
    }

    // MemoryMappedDevice interface
    uint8_t read(uint16_t offset) const {
        return read_table_[selected_bank_](device_ptrs_[selected_bank_], offset);
    }

    void write(uint16_t offset, uint8_t value) {
        write_table_[selected_bank_](device_ptrs_[selected_bank_], offset, value);
    }

    // Bank selection (via ROMSEL)
    uint8_t selected_bank() const { return selected_bank_; }

    void select_bank(uint8_t bank) {
        selected_bank_ = bank & 0x0F;  // Mask to 0-15
    }

    // Check if a bank is populated (not empty)
    bool is_bank_populated(uint8_t bank) const {
        return bank < 16 && device_ptrs_[bank] != nullptr;
    }

private:
    template<size_t... Is>
    void init_banks(std::index_sequence<Is...>) {
        (init_bank<Is>(), ...);
    }

    template<size_t I>
    void init_bank() {
        auto& binding = std::get<I>(bindings_);
        constexpr size_t bank_idx = std::decay_t<decltype(binding)>::bank_index;

        using DeviceType = typename std::decay_t<decltype(binding)>::device_type;

        device_ptrs_[bank_idx] = &binding.device;

        read_table_[bank_idx] = [](const void* ptr, uint16_t offset) -> uint8_t {
            return static_cast<const DeviceType*>(ptr)->read(offset);
        };

        write_table_[bank_idx] = [](void* ptr, uint16_t offset, uint8_t value) {
            static_cast<DeviceType*>(ptr)->write(offset, value);
        };
    }
};

// Deduction guide
template<typename... BankBindings>
BankedMemory(BankBindings...) -> BankedMemory<BankBindings...>;

// Type alias for BBC Micro's standard 16-bank configuration (empty by default)
// Note: For a configured system, use explicit template instantiation with make_bank()
using SidewaysMemory = BankedMemory<>;

} // namespace beebium
