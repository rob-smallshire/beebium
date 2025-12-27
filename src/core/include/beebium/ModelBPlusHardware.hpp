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

#include "AddressableLatch.hpp"
#include "BankBinding.hpp"
#include "IrqAggregator.hpp"
#include "MemoryMap.hpp"
#include "MemoryRegion.hpp"
#include "OutputQueue.hpp"
#include "Saa5050.hpp"
#include "SystemViaPeripheral.hpp"
#include "Via6522.hpp"
#include "PixelBatch.hpp"
#include "devices/BankedMemory.hpp"
#include "devices/Crtc6845.hpp"
#include "devices/Ram.hpp"
#include "devices/Rom.hpp"
#include "devices/VideoUla.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace beebium {

// BBC Model B+ 64K hardware configuration.
//
// The B+ extends the Model B with:
// - 64KB total RAM (32KB main + 20KB shadow + 12KB private/ANDY)
// - ACCCON register at 0xFE34 controlling shadow RAM
// - Extended ROMSEL (0xFE30) bit 7 controlling private RAM (ANDY)
//
// Memory Map:
//   0x0000-0x7FFF: 32KB Main RAM
//   0x3000-0x7FFF: (also) 20KB Shadow RAM (when ACCCON bit 7 = 1, for MOS)
//   0x8000-0xAFFF: 12KB Private RAM (ANDY) or ROM bank (controlled by ROMSEL bit 7)
//   0xB000-0xBFFF: 4KB ROM bank (top 4KB of selected sideways ROM)
//   0xC000-0xFBFF: 16KB MOS ROM (minus I/O region)
//   0xFC00-0xFDFF: Reserved / External I/O (FRED/JIM)
//   0xFE00-0xFEFF: SHEILA (I/O devices)
//     0xFE00-0xFE07: CRTC (6845)
//     0xFE08-0xFE0F: Serial ACIA
//     0xFE10-0xFE1F: Serial ULA
//     0xFE20-0xFE2F: Video ULA
//     0xFE30-0xFE33: ROMSEL (extended with bit 7 for ANDY)
//     0xFE34-0xFE37: ACCCON (shadow control, bit 7)
//     0xFE40-0xFE5F: System VIA (16 regs, mirrored)
//     0xFE60-0xFE7F: User VIA (16 regs, mirrored)
//     0xFE80-0xFE9F: 1770 Disc controller (built-in on B+)
//     0xFEA0-0xFEBF: Econet
//     0xFEC0-0xFEDF: A/D converter
//     0xFEE0-0xFEFF: Tube
//
// Shadow RAM Behavior:
//   When ACCCON bit 7 = 1, MOS code (executing from 0xC000-0xFFFF) sees
//   shadow RAM at 0x3000-0x7FFF. User code always sees main RAM.
//   Video always reads from shadow RAM when enabled.
//
// Private RAM (ANDY) Behavior:
//   When ROMSEL bit 7 = 1, addresses 0x8000-0xAFFF map to 12KB ANDY RAM
//   instead of the ROM bank. 0xB000-0xBFFF still comes from ROM.
//
class ModelBPlusHardware {
public:
    // Machine identification and region names (compile-time constants)
    static constexpr std::string_view MACHINE_TYPE = "ModelBPlus";
    static constexpr std::string_view MACHINE_DISPLAY_NAME = "BBC Model B+ 64K";
    static constexpr std::string_view REGION_MAIN_RAM = "main_ram";
    static constexpr std::string_view REGION_SHADOW_RAM = "shadow_ram";
    static constexpr std::string_view REGION_ANDY_RAM = "andy_ram";
    static constexpr std::string_view REGION_MOS_ROM = "mos_rom";

    // Default ROM filenames for this machine
    static constexpr std::string_view DEFAULT_MOS_ROM = "acorn-mos_2_0.rom";
    static constexpr std::string_view DEFAULT_LANGUAGE_ROM = "bbc-basic_2.rom";
    static constexpr uint8_t DEFAULT_LANGUAGE_SLOT = 15;

    // Hardware devices (owned by this struct)
    Ram<32768> main_ram;           // Main RAM 0x0000-0x7FFF
    Ram<20480> shadow_ram;         // Shadow screen memory (20KB for 0x3000-0x7FFF)
    Ram<12288> andy_ram;           // Private RAM (ANDY) 0x8000-0xAFFF
    Rom<16384> mos_rom;            // MOS ROM

    // Sideways ROM/RAM devices (owned, bound to BankedMemory)
    Rom<16384> basic_rom;          // Bank 0
    Rom<16384> dfs_rom;            // Bank 1
    Ram<16384> sideways_ram;       // Bank 4

    // Sideways banks type (same as Model B)
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

    // IRQ aggregator type - polls VIAs for IRQ status
    using IrqAggregatorType = IrqAggregator<
        IrqBinding<Via6522, 0>,  // System VIA → bit 0
        IrqBinding<Via6522, 1>   // User VIA → bit 1
    >;

    // Video hardware
    Crtc6845 crtc;
    VideoUla video_ula;
    Saa5050 saa5050;

    // Video output queue (optional - only created if video output is enabled)
    std::optional<OutputQueue<PixelBatch>> video_output;

    // System VIA peripherals
    AddressableLatch addressable_latch;
    SystemViaPeripheral system_via_peripheral{addressable_latch};

private:
    // B+ specific paging registers
    uint8_t romsel_ = 0;    // Bits 0-3: bank, Bit 7: ANDY enable
    uint8_t acccon_ = 0;    // Bit 7: shadow enable

    // ROMSEL register wrapper for B+ - handles bank switching and ANDY control
    struct BPlusRomselRegister {
        SidewaysType& sideways;
        uint8_t& romsel;

        uint8_t read(uint16_t) { return 0xFF; }  // Write-only register
        void write(uint16_t, uint8_t value) {
            romsel = value & 0x8F;  // Only bits 0-3 and 7 are writable
            sideways.select_bank(value & 0x0F);
        }
    };

    // ACCCON register wrapper for B+ - controls shadow RAM
    struct AccconRegister {
        uint8_t& acccon;

        uint8_t read(uint16_t) { return acccon; }  // Readable on B+
        void write(uint16_t, uint8_t value) {
            acccon = value & 0x80;  // Only bit 7 is writable on B+ 64K
        }
    };

public:
    BPlusRomselRegister romsel_reg{sideways, romsel_};
    AccconRegister acccon_reg{acccon_};

    // Memory map type - note: I/O regions handled first, then RAM/ROM
    // For B+, we need custom read/write that handles paging
    using MemoryMapType = decltype(
        MemoryMap{
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(std::declval<Crtc6845&>()),
            make_region<0xFE20, 0xFE2F, Mirror<0x01>>(std::declval<VideoUla&>()),
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE30, 0xFE33, Mirror<0x03>>(std::declval<BPlusRomselRegister&>()),
            make_region<0xFE34, 0xFE37, Mirror<0x03>>(std::declval<AccconRegister&>()),
            make_region<0x0000, 0x7FFF>(std::declval<Ram<32768>&>()),
            make_region<0x8000, 0xBFFF>(std::declval<SidewaysType&>()),
            make_region<0xC000, 0xFFFF>(std::declval<Rom<16384>&>())
        }
    );

    // Default constructor - uses internal system_via_peripheral
    ModelBPlusHardware()
        : system_via()
        , user_via()
        , memory_map_(make_memory_map())
        , irq_aggregator_(make_irq_aggregator())
    {
        // Connect internal peripheral to system VIA
        system_via.set_peripheral(&system_via_peripheral);
    }

    // Constructor with custom peripherals (for testing or alternative configurations)
    ModelBPlusHardware(ViaPeripheral& system_peripheral, ViaPeripheral& user_peripheral)
        : system_via(system_peripheral)
        , user_via(user_peripheral)
        , memory_map_(make_memory_map())
        , irq_aggregator_(make_irq_aggregator())
    {}

    // MemoryMappedDevice interface with B+ paging logic
    uint8_t read(uint16_t addr) {
        // Handle ANDY RAM region (0x8000-0xAFFF) when ROMSEL bit 7 is set
        if (addr >= 0x8000 && addr < 0xB000 && (romsel_ & 0x80)) {
            return andy_ram.read(addr - 0x8000);
        }
        // Default to normal memory map
        return memory_map_.read(addr);
    }

    void write(uint16_t addr, uint8_t value) {
        // Handle ANDY RAM region (0x8000-0xAFFF) when ROMSEL bit 7 is set
        if (addr >= 0x8000 && addr < 0xB000 && (romsel_ & 0x80)) {
            andy_ram.write(addr - 0x8000, value);
            return;
        }
        // Default to normal memory map
        memory_map_.write(addr, value);
    }

    // PC-aware read for VDU driver code shadow RAM routing.
    // Per B+ Service Manual Section 5.4.3:
    // When shadow is enabled (ACCCON bit 7 = 1) and address is 0x3000-0x7FFF,
    // VDU driver code (MOS 0xC000-0xDFFF, or paged RAM 0xA000-0xAFFF) reads shadow RAM,
    // while all other code reads main RAM.
    uint8_t read_with_pc(uint16_t addr, uint16_t pc) {
        // Shadow RAM routing for VDU driver code
        if (shadow_enabled() && addr >= 0x3000 && addr < 0x8000) {
            if (is_vdu_driver_code(pc)) {
                return shadow_ram.read(addr - 0x3000);
            }
            // Non-VDU code sees main RAM
            return main_ram.read(addr);
        }
        // All other addresses use normal handling
        return read(addr);
    }

    // PC-aware write for VDU driver code shadow RAM routing.
    void write_with_pc(uint16_t addr, uint8_t value, uint16_t pc) {
        // Shadow RAM routing for VDU driver code
        if (shadow_enabled() && addr >= 0x3000 && addr < 0x8000) {
            if (is_vdu_driver_code(pc)) {
                shadow_ram.write(addr - 0x3000, value);
                return;
            }
            // Non-VDU code writes to main RAM
            main_ram.write(addr, value);
            return;
        }
        // All other addresses use normal handling
        write(addr, value);
    }

    // Side-effect-free read for debugger inspection.
    // Always reads from main RAM (not shadow).
    uint8_t peek(uint16_t addr) const {
        // VIA regions need special handling to avoid side effects
        if (addr >= 0xFE40 && addr <= 0xFE5F) {
            return system_via.peek(addr & 0x0F);
        }
        if (addr >= 0xFE60 && addr <= 0xFE7F) {
            return user_via.peek(addr & 0x0F);
        }
        // Handle ANDY RAM for debugger
        if (addr >= 0x8000 && addr < 0xB000 && (romsel_ & 0x80)) {
            return andy_ram.read(addr - 0x8000);
        }
        // All other regions - use main memory map
        return memory_map_.read(addr);
    }

    // Video memory access - reads from currently configured video RAM.
    // When ACCCON bit 7 = 1, video reads from shadow RAM at 0x3000-0x7FFF.
    // Otherwise, video reads from main RAM.
    // Used by VideoBinding for screen rendering.
    uint8_t peek_video(uint16_t addr) const {
        if (addr >= 0x3000 && addr < 0x8000 && (acccon_ & 0x80)) {
            return shadow_ram.read(addr - 0x3000);
        }
        return main_ram.read(addr);
    }

    // Direct shadow RAM access for testing/debugging
    uint8_t peek_shadow(uint16_t addr) const {
        if (addr >= 0x3000 && addr < 0x8000) {
            return shadow_ram.read(addr - 0x3000);
        }
        return 0xFF;  // Outside shadow RAM range
    }

    // Write to shadow RAM directly (for testing)
    void write_shadow(uint16_t addr, uint8_t value) {
        if (addr >= 0x3000 && addr < 0x8000) {
            shadow_ram.write(addr - 0x3000, value);
        }
    }

    // Reset all devices
    void reset() {
        main_ram.clear();
        shadow_ram.clear();
        andy_ram.clear();
        system_via.reset();
        user_via.reset();
        crtc.reset();
        video_ula.reset();
        saa5050.reset();
        addressable_latch.reset();
        sideways.select_bank(0);
        romsel_ = 0;
        acccon_ = 0;
    }

    // Enable video output with optional custom queue capacity
    void enable_video_output(size_t capacity = OutputQueue<PixelBatch>::DEFAULT_CAPACITY) {
        video_output.emplace(capacity);
    }

    // Disable video output and free queue memory
    void disable_video_output() {
        video_output.reset();
    }

    // Check if video output is enabled
    bool video_output_enabled() const {
        return video_output.has_value();
    }

    // Poll IRQ status from VIAs (called from Machine::step after clock tick)
    uint8_t poll_irq() {
        return irq_aggregator_.poll();
    }

    // Paging register accessors for testing
    uint8_t romsel() const { return romsel_; }
    uint8_t acccon() const { return acccon_; }
    bool andy_enabled() const { return (romsel_ & 0x80) != 0; }
    bool paged_ram_enabled() const { return (romsel_ & 0x80) != 0; }  // Official Acorn terminology
    bool shadow_enabled() const { return (acccon_ & 0x80) != 0; }

    // Per B+ Service Manual Section 5.4.3:
    // VDU driver code detection determines which code can access shadow RAM.
    // - Code at 0xC000-0xDFFF (lower 8K of MOS) is always VDU driver
    // - Code at 0xA000-0xAFFF is VDU driver ONLY when paged RAM is selected
    // - "This special attribute is not available to any other sideways memory, ROM or RAM"
    bool is_vdu_driver_code(uint16_t pc) const {
        // Lower 8K of MOS ROM (0xC000-0xDFFF) - always VDU driver
        if (pc >= 0xC000 && pc < 0xE000) return true;

        // Top 4K at 0xA000-0xAFFF - VDU driver ONLY when paged RAM is selected
        // Per service manual: sideways ROMs at same address do NOT get VDU status
        if (pc >= 0xA000 && pc < 0xB000 && paged_ram_enabled()) return true;

        // Code at 0x0000-0x9FFF or 0xE000-0xFFFF never has VDU driver status
        return false;
    }

public:
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

    // Memory region discovery for debugger
    std::vector<MemoryRegionDescriptor> get_memory_regions() const {
        std::vector<MemoryRegionDescriptor> regions;

        // Main RAM (0x0000-0x7FFF)
        regions.push_back({
            REGION_MAIN_RAM,
            0x0000,  // base_address
            32768,
            RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
        });

        // Shadow RAM (B+ specific, 0x3000-0x7FFF)
        regions.push_back({
            REGION_SHADOW_RAM,
            0x3000,  // base_address
            20480,
            RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
        });

        // ANDY private RAM (B+ specific, 0x8000-0xAFFF)
        regions.push_back({
            REGION_ANDY_RAM,
            0x8000,  // base_address
            12288,
            RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
        });

        // MOS ROM (0xC000-0xFFFF)
        regions.push_back({
            REGION_MOS_ROM,
            0xC000,  // base_address
            16384,
            RegionFlags::Readable | RegionFlags::Populated
        });

        // Sideways banks (bank_0 through bank_15, all mapped at 0x8000-0xBFFF)
        for (uint8_t bank = 0; bank < 16; ++bank) {
            RegionFlags flags = RegionFlags::Readable | RegionFlags::Writable;
            if (sideways.is_bank_populated(bank)) {
                flags = flags | RegionFlags::Populated;
            }
            if (bank == sideways.selected_bank()) {
                flags = flags | RegionFlags::Active;
            }
            regions.push_back({
                bank_names_[bank],
                0x8000,  // base_address (all banks share same address range)
                16384,
                flags
            });
        }

        return regions;
    }

    // Region access - side-effect free read (uses absolute address)
    uint8_t peek_region(std::string_view name, uint32_t address) const {
        if (name == REGION_MAIN_RAM) {
            // Main RAM: 0x0000-0x7FFF
            if (address < 0x8000) {
                return main_ram.read(static_cast<uint16_t>(address));
            }
            return 0xFF;
        }
        if (name == REGION_SHADOW_RAM) {
            // Shadow RAM: 0x3000-0x7FFF
            if (address >= 0x3000 && address < 0x8000) {
                return shadow_ram.read(static_cast<uint16_t>(address - 0x3000));
            }
            return 0xFF;
        }
        if (name == REGION_ANDY_RAM) {
            // ANDY RAM: 0x8000-0xAFFF
            if (address >= 0x8000 && address < 0xB000) {
                return andy_ram.read(static_cast<uint16_t>(address - 0x8000));
            }
            return 0xFF;
        }
        if (name == REGION_MOS_ROM) {
            // MOS ROM: 0xC000-0xFFFF
            if (address >= 0xC000) {
                return mos_rom.read(static_cast<uint16_t>(address - 0xC000));
            }
            return 0xFF;
        }
        // Check for bank_N pattern (0x8000-0xBFFF)
        if (name.substr(0, 5) == "bank_" && name.size() <= 7) {
            uint8_t bank = parse_bank_number(name);
            if (bank < 16 && address >= 0x8000 && address < 0xC000) {
                return sideways.peek_bank(bank, static_cast<uint16_t>(address - 0x8000));
            }
        }
        return 0xFF;
    }

    // Region access - normal read (may have side effects, uses absolute address)
    uint8_t read_region(std::string_view name, uint32_t address) {
        if (name == REGION_MAIN_RAM) {
            // Main RAM: 0x0000-0x7FFF
            if (address < 0x8000) {
                return main_ram.read(static_cast<uint16_t>(address));
            }
            return 0xFF;
        }
        if (name == REGION_SHADOW_RAM) {
            // Shadow RAM: 0x3000-0x7FFF
            if (address >= 0x3000 && address < 0x8000) {
                return shadow_ram.read(static_cast<uint16_t>(address - 0x3000));
            }
            return 0xFF;
        }
        if (name == REGION_ANDY_RAM) {
            // ANDY RAM: 0x8000-0xAFFF
            if (address >= 0x8000 && address < 0xB000) {
                return andy_ram.read(static_cast<uint16_t>(address - 0x8000));
            }
            return 0xFF;
        }
        if (name == REGION_MOS_ROM) {
            // MOS ROM: 0xC000-0xFFFF
            if (address >= 0xC000) {
                return mos_rom.read(static_cast<uint16_t>(address - 0xC000));
            }
            return 0xFF;
        }
        // Check for bank_N pattern (0x8000-0xBFFF)
        if (name.substr(0, 5) == "bank_" && name.size() <= 7) {
            uint8_t bank = parse_bank_number(name);
            if (bank < 16 && address >= 0x8000 && address < 0xC000) {
                return sideways.read_bank(bank, static_cast<uint16_t>(address - 0x8000));
            }
        }
        return 0xFF;
    }

    // Region access - write (uses absolute address)
    void write_region(std::string_view name, uint32_t address, uint8_t value) {
        if (name == REGION_MAIN_RAM) {
            // Main RAM: 0x0000-0x7FFF
            if (address < 0x8000) {
                main_ram.write(static_cast<uint16_t>(address), value);
            }
            return;
        }
        if (name == REGION_SHADOW_RAM) {
            // Shadow RAM: 0x3000-0x7FFF
            if (address >= 0x3000 && address < 0x8000) {
                shadow_ram.write(static_cast<uint16_t>(address - 0x3000), value);
            }
            return;
        }
        if (name == REGION_ANDY_RAM) {
            // ANDY RAM: 0x8000-0xAFFF
            if (address >= 0x8000 && address < 0xB000) {
                andy_ram.write(static_cast<uint16_t>(address - 0x8000), value);
            }
            return;
        }
        // MOS ROM is read-only, ignore writes
        if (name == REGION_MOS_ROM) {
            return;
        }
        // Check for bank_N pattern (0x8000-0xBFFF)
        if (name.substr(0, 5) == "bank_" && name.size() <= 7) {
            uint8_t bank = parse_bank_number(name);
            if (bank < 16 && address >= 0x8000 && address < 0xC000) {
                sideways.write_bank(bank, static_cast<uint16_t>(address - 0x8000), value);
            }
        }
    }

private:
    // Static bank names for string_view references
    static constexpr std::string_view bank_names_[16] = {
        "bank_0", "bank_1", "bank_2", "bank_3",
        "bank_4", "bank_5", "bank_6", "bank_7",
        "bank_8", "bank_9", "bank_10", "bank_11",
        "bank_12", "bank_13", "bank_14", "bank_15"
    };

    // Parse bank number from "bank_N" string
    static uint8_t parse_bank_number(std::string_view name) {
        if (name.size() < 6) return 255;
        if (name.size() == 6) {
            // Single digit: bank_0 through bank_9
            char c = name[5];
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        } else if (name.size() == 7) {
            // Two digits: bank_10 through bank_15
            if (name[5] == '1') {
                char c = name[6];
                if (c >= '0' && c <= '5') return static_cast<uint8_t>(10 + (c - '0'));
            }
        }
        return 255;  // Invalid
    }

private:
    MemoryMapType memory_map_;
    IrqAggregatorType irq_aggregator_;

    IrqAggregatorType make_irq_aggregator() {
        return beebium::make_irq_aggregator(
            make_irq_binding<0>(system_via),
            make_irq_binding<1>(user_via)
        );
    }

    MemoryMapType make_memory_map() {
        // Order matters: first match wins
        // I/O regions before ROM regions to handle overlap at 0xFExx
        return MemoryMap{
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(crtc),
            make_region<0xFE20, 0xFE2F, Mirror<0x01>>(video_ula),
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(system_via),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(user_via),
            make_region<0xFE30, 0xFE33, Mirror<0x03>>(romsel_reg),
            make_region<0xFE34, 0xFE37, Mirror<0x03>>(acccon_reg),
            make_region<0x0000, 0x7FFF>(main_ram),
            make_region<0x8000, 0xBFFF>(sideways),
            make_region<0xC000, 0xFFFF>(mos_rom)
        };
    }
};

} // namespace beebium
