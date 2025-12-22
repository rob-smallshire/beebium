#pragma once

#include "AddressableLatch.hpp"
#include "BankBinding.hpp"
#include "MemoryMap.hpp"
#include "SystemViaPeripheral.hpp"
#include "Via6522.hpp"
#include "devices/BankedMemory.hpp"
#include "devices/Crtc6845.hpp"
#include "devices/Ram.hpp"
#include "devices/Rom.hpp"
#include "devices/VideoUla.hpp"
#include <cstdint>

namespace beebium {

// BBC Model B hardware configuration using the new MemoryMap infrastructure.
//
// This struct holds all hardware devices and provides:
// - Memory-mapped access via the memory_map
// - Direct access to devices for clocking and configuration
// - Dependency injection for VIA peripherals
//
// Memory Map:
//   0x0000-0x7FFF: 32KB RAM
//   0x8000-0xBFFF: 16KB Paged ROM (16 banks, selected via ROMSEL at 0xFE30)
//   0xC000-0xFBFF: 16KB MOS ROM (minus I/O region)
//   0xFC00-0xFDFF: Reserved / External I/O (FRED/JIM)
//   0xFE00-0xFEFF: SHEILA (I/O devices)
//     0xFE00-0xFE07: CRTC (6845)
//     0xFE08-0xFE0F: Serial ACIA
//     0xFE10-0xFE1F: Serial ULA
//     0xFE20-0xFE2F: Video ULA
//     0xFE30-0xFE3F: Paging registers (ROMSEL)
//     0xFE40-0xFE5F: System VIA (16 regs, mirrored)
//     0xFE60-0xFE7F: User VIA (16 regs, mirrored)
//     0xFE80-0xFE9F: Disc controller
//     0xFEA0-0xFEBF: Econet
//     0xFEC0-0xFEDF: A/D converter
//     0xFEE0-0xFEFF: Tube
//
// Sideways ROM/RAM Configuration:
//   Bank 0: ROM (typically BASIC)
//   Bank 1: ROM (typically DFS or other language)
//   Bank 4: Sideways RAM (for testing/user programs)
//   Banks 2-3, 5-15: Empty (return 0xFF)
//
class ModelBHardware {
public:
    // Hardware devices (owned by this struct)
    Ram<32768> main_ram;
    Rom<16384> mos_rom;

    // Sideways ROM/RAM devices (owned, bound to BankedMemory)
    Rom<16384> basic_rom;      // Bank 0
    Rom<16384> dfs_rom;        // Bank 1
    Ram<16384> sideways_ram;   // Bank 4

    // Sideways banks type
    using SidewaysType = BankedMemory<
        decltype(make_bank<0>(std::declval<Rom<16384>&>())),
        decltype(make_bank<1>(std::declval<Rom<16384>&>())),
        decltype(make_bank<4>(std::declval<Ram<16384>&>()))
    >;

    SidewaysType sideways{
        make_bank<0>(basic_rom),
        make_bank<1>(dfs_rom),
        make_bank<4>(sideways_ram)
    };

    Via6522 system_via;
    Via6522 user_via;

    // Video hardware (stubs for boot)
    Crtc6845 crtc;
    VideoUla video_ula;

    // System VIA peripherals
    AddressableLatch addressable_latch;
    SystemViaPeripheral system_via_peripheral{addressable_latch};

    // ROMSEL register wrapper - handles bank switching and returns 0xFF on read
    struct RomselRegister {
        SidewaysType& sideways;

        uint8_t read(uint16_t) { return 0xFF; }  // Write-only register
        void write(uint16_t, uint8_t value) {
            sideways.select_bank(value & 0x0F);
        }
    };

    RomselRegister romsel{sideways};

    // Memory map type (deduced from make_memory_map)
    using MemoryMapType = decltype(
        MemoryMap{
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(std::declval<Crtc6845&>()),
            make_region<0xFE20, 0xFE2F, Mirror<0x01>>(std::declval<VideoUla&>()),
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE30, 0xFE3F, Mirror<0x0F>>(std::declval<RomselRegister&>()),
            make_region<0x0000, 0x7FFF>(std::declval<Ram<32768>&>()),
            make_region<0x8000, 0xBFFF>(std::declval<SidewaysType&>()),
            make_region<0xC000, 0xFFFF>(std::declval<Rom<16384>&>())
        }
    );

    // Default constructor - uses internal system_via_peripheral
    ModelBHardware()
        : system_via()
        , user_via()
        , memory_map_(make_memory_map())
    {
        // Connect internal peripheral to system VIA
        system_via.set_peripheral(&system_via_peripheral);
    }

    // Constructor with custom peripherals (for testing or alternative configurations)
    ModelBHardware(ViaPeripheral& system_peripheral, ViaPeripheral& user_peripheral)
        : system_via(system_peripheral)
        , user_via(user_peripheral)
        , memory_map_(make_memory_map())
    {}

    // MemoryMappedDevice interface (delegates to memory_map)
    uint8_t read(uint16_t addr) {
        return memory_map_.read(addr);
    }

    void write(uint16_t addr, uint8_t value) {
        memory_map_.write(addr, value);
    }

    // Reset all devices
    void reset() {
        main_ram.clear();
        system_via.reset();
        user_via.reset();
        crtc.reset();
        video_ula.reset();
        addressable_latch.reset();
        sideways.select_bank(0);
    }

    // Clock peripherals (called from Machine::step)
    // Returns IRQ mask (bit 0 = system VIA, bit 1 = user VIA)
    uint8_t tick_peripherals(uint64_t cycle) {
        bool phi2_rising = (cycle & 1) != 0;

        uint8_t irq_mask = 0;
        if (phi2_rising) {
            if (system_via.update_phi2_leading_edge()) irq_mask |= 0x01;
            if (user_via.update_phi2_leading_edge()) irq_mask |= 0x02;
        } else {
            if (system_via.update_phi2_trailing_edge()) irq_mask |= 0x01;
            if (user_via.update_phi2_trailing_edge()) irq_mask |= 0x02;
        }
        return irq_mask;
    }

    // ROM loading - directly access the owned ROM devices
    void load_mos(const uint8_t* data, size_t size) {
        mos_rom.load(data, size);
    }

    void load_basic(const uint8_t* data, size_t size) {
        basic_rom.load(data, size);
    }

    void load_dfs(const uint8_t* data, size_t size) {
        dfs_rom.load(data, size);
    }

private:
    MemoryMapType memory_map_;

    MemoryMapType make_memory_map() {
        // Order matters: first match wins
        // I/O regions before ROM regions to handle overlap at 0xFExx
        return MemoryMap{
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(crtc),
            make_region<0xFE20, 0xFE2F, Mirror<0x01>>(video_ula),
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(system_via),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(user_via),
            make_region<0xFE30, 0xFE3F, Mirror<0x0F>>(romsel),
            make_region<0x0000, 0x7FFF>(main_ram),
            make_region<0x8000, 0xBFFF>(sideways),
            make_region<0xC000, 0xFFFF>(mos_rom)
        };
    }
};

} // namespace beebium
