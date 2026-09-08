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

#include "extension/OneMHzBusPort.hpp"
#include "extension/UserPort.hpp"
#include "extension/SerialPort.hpp"
#include "AddressableLatch.hpp"
#include "AudioBuffer.hpp"
#include "BankBinding.hpp"
#include "ClockTypes.hpp"
#include "IrqAggregator.hpp"
#include "NmiAggregator.hpp"
#include "PacingConfig.hpp"
#include "MemoryMap.hpp"
#include "MemoryRegion.hpp"
#include "MotherboardLinks.hpp"
#include "OutputQueue.hpp"
#include "Saa5050.hpp"
#include "SlotTopology.hpp"
#include "SystemViaPeripheral.hpp"
#include "Via6522.hpp"
#include "PixelBatch.hpp"
#include "devices/BankedMemory.hpp"
#include "devices/ConfigurableSlot.hpp"
#include "devices/UserRomSocketBank.hpp"
#include "devices/Crtc6845.hpp"
#include "devices/Ram.hpp"
#include "devices/Rom.hpp"
#include "devices/Sn76489.hpp"
#include "devices/VideoUla.hpp"
#include "disc/DiscDrive.hpp"
#include "disc/WD1770.hpp"
#include "econet/EconetSocket.hpp"
#include "serial/SerialSocket.hpp"
#include "tube/TubeSocket.hpp"
#include "indicators/IndicatorFilter.hpp"
#include "indicators/Indicators.hpp"
#include <algorithm>
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
    static constexpr std::string_view MACHINE_TYPE = "model-b-plus";
    constexpr std::string_view machine_type() const { return MACHINE_TYPE; }
    static constexpr std::string_view MACHINE_DISPLAY_NAME = "BBC Model B+ 64K";
    static constexpr std::string_view MACHINE_DESCRIPTION = "Enhanced Model B with 64KB RAM and built-in FDC";
    static constexpr std::string_view REGION_MAIN_RAM = "main_ram";
    static constexpr std::string_view REGION_SHADOW_RAM = "shadow_ram";
    static constexpr std::string_view REGION_ANDY_RAM = "andy_ram";
    static constexpr std::string_view REGION_MOS_ROM = "mos_rom";

    // Default ROM filenames for this machine
    static constexpr std::string_view DEFAULT_MOS_ROM = "acorn-mos_2_0.rom";
    static constexpr std::string_view DEFAULT_LANGUAGE_ROM = "bbc-basic_2.rom";
    static constexpr uint8_t DEFAULT_LANGUAGE_SLOT = 15;
    static constexpr std::string_view DEFAULT_DFS_ROM = "acorn-dfs_2_26.rom";
    static constexpr uint8_t DEFAULT_DFS_SLOT = 11;

    // Default pacing configuration for this machine
    static constexpr PacingConfig default_pacing_config() {
        return {
            .base_clock_hz = timing::CPU_HZ,  // 2 MHz
            .pacing_hz = 200,                  // 200 Hz sync rate
            .speed_multiplier = 1.0            // Real-time
        };
    }

    // Hardware devices (owned by this struct)
    Ram<32768> main_ram;           // Main RAM 0x0000-0x7FFF
    Ram<20480> shadow_ram;         // Shadow screen memory (20KB for 0x3000-0x7FFF)
    Ram<12288> andy_ram;           // Private RAM (ANDY) 0x8000-0xAFFF
    Rom<16384> mos_rom;            // MOS ROM

    // Sideways ROM devices for 6 ROM sockets (Model B+ has 6 sockets, each supporting 16K or 32K EPROMs)
    // For 16K ROMs, the same device appears in both slots of a pair (mirrored)
    // Socket IC71 (slots 1/15): BASIC - link S13 selects which slot number addresses it
    // Socket IC68 (slots 10/11): DFS
    // Socket IC62 (slots 8/9): User ROM
    // Socket IC57 (slots 6/7): User ROM
    // Socket IC44 (slots 4/5): User ROM
    // Socket IC35 (slots 2/3): User ROM
    // IC71 is the system ROM: a 32K device holding the MOS in the high
    // half (mapped to &C000-&FFFF, not sideways) and BASIC in the low
    // half (sideways). The PCB wiring forces sideways accesses to read
    // the BASIC half regardless of ROMSEL bit 0, so both slot numbers
    // of the S13-selected pair return BASIC. We model this faithfully
    // by binding `basic_rom` to both slots of the active pair.
    Rom<16384> basic_rom;          // IC71 - slots 14/15 or 0/1 per S13 (aliased)

    // The five user ROM sockets (IC35..IC68) and their 10 16K halves
    // are owned by UserRomSocketBank, shared by both B+ variants. See
    // devices/UserRomSocketBank.hpp for the rationale (ConfigurableSlot
    // backing per half, runtime kind, *SRLOAD support, etc.). The
    // member must be declared before `sideways` so the make_bank<>
    // references below capture stable storage.
    UserRomSocketBank user_sockets_;

    // Filepath of the BASIC ROM image loaded into IC71, captured at
    // load time so GetSlotStatus can report it for the BASIC slots.
    // The IC71 BASIC half is a plain Rom<16384>, not a
    // ConfigurableSlot, so image_name lives here rather than on the
    // device.
    std::string basic_rom_image_name_;

    // Cache of the S13 link state for use by slot_info() / load
    // routing. apply_motherboard_links() updates it; default South
    // matches factory configuration.
    bool s13_is_south_ = true;

    using SidewaysType = BankedMemory<
        decltype(make_bank<15>(std::declval<Rom<16384>&>())),
        decltype(make_bank<14>(std::declval<Rom<16384>&>())),
        decltype(make_bank<11>(std::declval<ConfigurableSlot&>())),
        decltype(make_bank<10>(std::declval<ConfigurableSlot&>())),
        decltype(make_bank<9>(std::declval<ConfigurableSlot&>())),
        decltype(make_bank<8>(std::declval<ConfigurableSlot&>())),
        decltype(make_bank<7>(std::declval<ConfigurableSlot&>())),
        decltype(make_bank<6>(std::declval<ConfigurableSlot&>())),
        decltype(make_bank<5>(std::declval<ConfigurableSlot&>())),
        decltype(make_bank<4>(std::declval<ConfigurableSlot&>())),
        decltype(make_bank<3>(std::declval<ConfigurableSlot&>())),
        decltype(make_bank<2>(std::declval<ConfigurableSlot&>())),
        decltype(make_bank<1>(std::declval<Rom<16384>&>())),
        decltype(make_bank<0>(std::declval<Rom<16384>&>()))
    >;

    SidewaysType sideways{
        make_bank<15>(basic_rom),                          // IC71 - slot 15 (BASIC, S13=South alias)
        make_bank<14>(basic_rom),                          // IC71 - slot 14 (BASIC, S13=South alias)
        make_bank<11>(user_sockets_.at_slot(11)),          // IC68 high
        make_bank<10>(user_sockets_.at_slot(10)),          // IC68 low
        make_bank<9>(user_sockets_.at_slot(9)),            // IC62 high
        make_bank<8>(user_sockets_.at_slot(8)),            // IC62 low
        make_bank<7>(user_sockets_.at_slot(7)),            // IC57 high
        make_bank<6>(user_sockets_.at_slot(6)),            // IC57 low
        make_bank<5>(user_sockets_.at_slot(5)),             // IC44 high
        make_bank<4>(user_sockets_.at_slot(4)),             // IC44 low
        make_bank<3>(user_sockets_.at_slot(3)),             // IC35 high
        make_bank<2>(user_sockets_.at_slot(2)),             // IC35 low
        make_bank<1>(basic_rom),     // IC71 - slot 1 (BASIC, S13=North alias)
        make_bank<0>(basic_rom)      // IC71 - slot 0 (BASIC, S13=North alias)
    };

    Via6522 system_via;
    Via6522 user_via;

    // User Port handle (must be declared after user_via for init order)
    UserPort user_port_{user_via};

    // IRQ aggregator type - polls VIAs, Tube, and 1 MHz bus for IRQ status
    using IrqAggregatorType = IrqAggregator<
        IrqBinding<Via6522, 0>,        // System VIA → bit 0
        IrqBinding<Via6522, 1>,        // User VIA → bit 1
        IrqBinding<TubeSocket, 2>,     // Tube HIRQ → bit 2
        IrqBinding<OneMHzBusPort, 3>,  // 1 MHz bus devices (e.g. SCSI) → bit 3
        IrqBinding<SerialSocket, 4>    // Serial ACIA (MC6850) → bit 4
    >;

    // Video hardware
    Crtc6845 crtc;
    VideoUla video_ula;
    Saa5050 saa5050;

    // Video output queue (optional - only created if video output is enabled)
    std::optional<OutputQueue<PixelBatch>> video_output;

    // Sound hardware
    Sn76489 sound_chip{4'000'000, 48'000};  // 4 MHz clock, 48 kHz sample rate

    // Audio output buffer (optional - only created if audio output is enabled)
    std::optional<AudioBuffer> audio_buffer;

    // Hardware indicators (LEDs, motors) - must be declared before components that use them
    Indicators indicators;

    // System VIA peripherals (registers its own LED indicators)
    AddressableLatch addressable_latch;
    SystemViaPeripheral system_via_peripheral{addressable_latch, indicators};

    // Disc controller (WD1770 built-in on Model B+)
    WD1770 disc_controller;
    DiscDrive disc_drive_0{indicators, "floppy-0-activity-led", "FDD 0", "568nm"};
    DiscDrive disc_drive_1{indicators, "floppy-1-activity-led", "FDD 1", "568nm"};

    // Econet subsystem -- optional networking hardware
    EconetSocket econet_socket;

    // Serial subsystem -- on-board MC6850 ACIA (&FE08) + Serial ULA (&FE10).
    // Always fitted on a real BBC; its IRQ output drives the shared CPU IRQ line.
    SerialSocket serial_socket;

    // BBC serial port handle (RS423): attach point for a SerialPortDevice, the
    // UserPort analogue. Exposed to extensions via ExtensionContext.
    SerialPort serial_port_{serial_socket};

    // Tube subsystem -- optional second processor interface
    TubeSocket tube_socket;

    // Econet memory-mapped region adapters (thin wrappers for MemoryMappedDevice concept)
    struct EconetStationIdRegion {
        EconetSocket& econet_socket;
        uint8_t read(uint16_t offset) { return econet_socket.read_station_id(offset); }
        void write(uint16_t offset, uint8_t value) { econet_socket.write_station_id(offset, value); }
    };

    struct EconetAdlcRegion {
        EconetSocket& econet_socket;
        uint8_t read(uint16_t offset) { return econet_socket.read_adlc(offset); }
        void write(uint16_t offset, uint8_t value) { econet_socket.write_adlc(offset, value); }
    };

    // Video ULA wrapper that fires INTON on every access to &FE20-&FE2F.
    struct VideoUlaWithInton {
        VideoUla& video_ula;
        EconetSocket& econet_socket;
        uint8_t read(uint16_t offset) { econet_socket.on_inton(); return video_ula.read(offset); }
        void write(uint16_t offset, uint8_t value) { econet_socket.on_inton(); video_ula.write(offset, value); }
    };

    EconetStationIdRegion econet_station_id_region_{econet_socket};
    EconetAdlcRegion econet_adlc_region_{econet_socket};

    // Serial memory-mapped region adapters (thin wrappers for MemoryMappedDevice)
    struct SerialAciaRegion {
        SerialSocket& serial_socket;
        uint8_t read(uint16_t offset) { return serial_socket.read_acia(offset); }
        void write(uint16_t offset, uint8_t value) { serial_socket.write_acia(offset, value); }
    };

    struct SerialUlaRegion {
        SerialSocket& serial_socket;
        uint8_t read(uint16_t offset) { return serial_socket.read_ula(offset); }
        void write(uint16_t offset, uint8_t value) { serial_socket.write_ula(offset, value); }
    };

    SerialAciaRegion serial_acia_region_{serial_socket};
    SerialUlaRegion serial_ula_region_{serial_socket};
    VideoUlaWithInton video_ula_with_inton_{video_ula, econet_socket};

private:
    // B+ specific paging registers
    uint8_t romsel_ = 0;    // Bits 0-3: bank, Bit 7: ANDY enable
    uint8_t acccon_ = 0;    // Bit 7: shadow enable

    // Disc control register state
    // Bit 0: Drive select (0=drive 0, 1=drive 1)
    // Bit 1: Side select
    // Bit 2: Density (0=double/MFM, 1=single/FM)
    // Bit 4: Motor on
    // Bit 5: Reset (directly resets WD1770)
    // Bit 6: NMI enable (gates INTRQ to NMI line)
    uint8_t disc_control_ = 0;
    bool nmi_enabled_ = false;

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

    // Disc control register wrapper (0xFE80) - controls 1770 interface
    struct DiscControlRegister {
        WD1770& controller;
        DiscDrive& drive0;
        DiscDrive& drive1;
        uint8_t& control;
        bool& nmi_enabled;

        uint8_t read(uint16_t) { return 0xFF; }  // Write-only register, open bus

        void write(uint16_t, uint8_t value) {
            // Save old control value for edge detection before updating
            uint8_t old_control = control;
            control = value;

            // Bit 0/1: Drive select (bit 0 = drive 0, bit 1 = drive 1)
            // DFS uses bits 0-1 for drive select, not as a binary number
            if (value & 0x01) {
                controller.set_drive(0);
            } else if (value & 0x02) {
                controller.set_drive(1);
            }
            // If neither bit set, drive selection is indeterminate

            // Bit 2: Side select (propagated to FDC and drives)
            uint8_t side = (value & 0x04) ? 1 : 0;
            controller.set_side(side);
            drive0.set_side(side);
            drive1.set_side(side);

            // Bit 3: Density (0=double/MFM, 1=single/FM)
            controller.set_density((value & 0x08) == 0);

            // Bit 4: Motor on (for 8271-style explicit control)
            // Note: WD1770 handles motor internally via spin_up()/spin_down()
            // during command execution, so this bit is effectively ignored.
            // DFS writes 0x29 (motor bit clear), relying on WD1770's motor control.

            // Bit 5: Reset (active low)
            // Reset is active when bit 5 is LOW (0), inactive when HIGH (1)
            // Only reset on the falling edge (transition from 1 to 0)
            bool reset_active = (value & 0x20) == 0;
            bool was_reset_active = (old_control & 0x20) == 0;
            if (reset_active && !was_reset_active) {
                controller.reset();
            }

            // Bit 6: NMI enable
            nmi_enabled = (value & 0x40) != 0;
        }
    };

    // 1 MHz expansion bus (FRED/JIM, 0xFC00-0xFDFF).
    OneMHzBusPort one_mhz_bus_;

public:
    BPlusRomselRegister romsel_reg{sideways, romsel_};
    AccconRegister acccon_reg{acccon_};
    DiscControlRegister disc_control_reg{disc_controller, disc_drive_0, disc_drive_1, disc_control_, nmi_enabled_};

    // Memory map type - note: I/O regions handled first, then RAM/ROM
    // For B+, we need custom read/write that handles paging
    // Note: FRED/JIM overlay MOS ROM (first match wins)
    using MemoryMapType = decltype(
        MemoryMap{
            make_region<0xFC00, 0xFDFF>(std::declval<OneMHzBusPort&>()),   // FRED/JIM (overlays MOS ROM)
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(std::declval<Crtc6845&>()),
            make_region<0xFE08, 0xFE0F, Mirror<0x01>>(std::declval<SerialAciaRegion&>()),       // Serial ACIA (MC6850)
            make_region<0xFE10, 0xFE17, Mirror<0x07>>(std::declval<SerialUlaRegion&>()),        // Serial ULA (SERPROC)
            make_region<0xFE18, 0xFE1F, Mirror<0x07>>(std::declval<EconetStationIdRegion&>()),  // Econet station ID + INTOFF
            make_region<0xFE20, 0xFE2F, Mirror<0x01>>(std::declval<VideoUlaWithInton&>()),       // Video ULA + INTON
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE30, 0xFE33, Mirror<0x03>>(std::declval<BPlusRomselRegister&>()),
            make_region<0xFE34, 0xFE37, Mirror<0x03>>(std::declval<AccconRegister&>()),
            make_region<0xFE80, 0xFE83, Mirror<0x03>>(std::declval<DiscControlRegister&>()),
            make_region<0xFE84, 0xFE87, Mirror<0x03>>(std::declval<WD1770&>()),
            make_region<0xFEA0, 0xFEBF, Mirror<0x03>>(std::declval<EconetAdlcRegion&>()),        // Econet ADLC
            make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(std::declval<TubeSocket&>()),              // Tube ULA
            make_region<0x0000, 0x7FFF>(std::declval<Ram<32768>&>()),
            make_region<0x8000, 0xBFFF>(std::declval<SidewaysType&>()),
            make_region<0xC000, 0xFFFF>(std::declval<Rom<16384>&>())       // MOS ROM (occluded by I/O regions)
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
        // Connect sound chip to system VIA peripheral
        system_via_peripheral.set_sound_chip(&sound_chip);
        // Connect disc drives to disc controller
        disc_controller.attach_drive(0, &disc_drive_0);
        disc_controller.attach_drive(1, &disc_drive_1);
        // Note: indicators.start() is deferred to the server bootstrap, after
        // extension init() runs. This keeps the registration window open for
        // extensions; closing it before extensions can register would violate
        // the Indicators register-before-start contract.
        // Wire 2MHz open bus regions to memory map's last bus value for open bus emulation
        econet_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        serial_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        tube_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
    }

    // Constructor with custom peripherals (for testing or alternative configurations)
    ModelBPlusHardware(ViaPeripheral& system_peripheral, ViaPeripheral& user_peripheral)
        : system_via(system_peripheral)
        , user_via(user_peripheral)
        , memory_map_(make_memory_map())
        , irq_aggregator_(make_irq_aggregator())
    {
        // Connect disc drives to disc controller
        disc_controller.attach_drive(0, &disc_drive_0);
        disc_controller.attach_drive(1, &disc_drive_1);
        // Wire 2MHz open bus regions to memory map's last bus value for open bus emulation
        econet_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        serial_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        tube_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
    }

    // Destructor - stops indicator consumer thread
    ~ModelBPlusHardware() {
        indicators.stop();
    }

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
        // Tube registers have side effects (FIFO dequeue, flag clear)
        if (addr >= 0xFEE0 && addr <= 0xFEFF) {
            return tube_socket.peek(addr & 0x07);
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

    // Power-on reset: clear RAM and reset all devices including System VIA
    // This is the original reset() behavior, equivalent to powering off and on.
    void reset() {
        main_ram.clear();
        shadow_ram.clear();
        andy_ram.clear();
        system_via.reset();
        user_via.reset();
        crtc.reset();
        video_ula.reset();
        saa5050.reset();
        sound_chip.reset();
        addressable_latch.reset();
        sideways.select_bank(15);  // Default to BASIC ROM at slot 15
        romsel_ = 0x0F;  // ROMSEL bits 0-3 = 15 (BASIC), bit 7 = 0 (ANDY disabled)
        acccon_ = 0;
        disc_controller.reset();
        disc_control_ = 0;
        nmi_enabled_ = false;
        econet_socket.reset();
        serial_socket.reset();
        tube_socket.reset();
    }

    // Soft reset (Break key): reset peripherals but preserve System VIA state
    // The System VIA's preserved IER allows MOS to detect this as a warm reset.
    // Does NOT clear RAM - allows variables and programs to survive.
    // Note: Hardware does NOT distinguish Ctrl-Break from Break - MOS checks
    // the keyboard matrix during reset and clears VIA config if Ctrl is held.
    void soft_reset() {
        // Do NOT reset system_via - this is how MOS detects soft vs hard reset
        user_via.reset();
        crtc.reset();
        video_ula.reset();
        saa5050.reset();
        sound_chip.reset();
        addressable_latch.reset();
        disc_controller.reset();
        disc_control_ = 0;
        nmi_enabled_ = false;
        econet_socket.reset();
        serial_socket.reset();
        tube_socket.reset();
        // Do NOT clear RAM
        // Do NOT reset romsel_, acccon_, or sideways bank selection
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

    // Enable audio output with optional custom buffer capacity
    void enable_audio_output(size_t capacity = AudioBuffer::DEFAULT_CAPACITY) {
        if (!audio_buffer) {
            audio_buffer.emplace(capacity);
        }
    }

    // Disable audio output and free buffer memory
    void disable_audio_output() {
        audio_buffer.reset();
    }

    // Check if audio output is enabled
    bool audio_output_enabled() const {
        return audio_buffer.has_value();
    }

    // Poll IRQ status from VIAs (called from Machine::step after clock tick)
    uint8_t poll_irq() {
        return irq_aggregator_.poll();
    }

    // Poll NMI status from disc controller (called from Machine::step after clock tick)
    // Returns non-zero if NMI is pending from disc controller.
    // NMI is generated when the WD1770 DRQ or INTRQ line is asserted.
    //
    // DRQ fires during data transfer (each byte needs an NMI to trigger read),
    // INTRQ fires at command completion.
    //
    // Note: The disc control register bit 6 is nominally an NMI enable bit,
    // but B2 emulator doesn't gate NMI via this bit and DFS doesn't appear
    // to set it before disc operations. Following B2's approach here.
    //
    // Note: This also ticks the disc controller as this method is called
    // once per 1MHz cycle from Machine::step().
    uint8_t poll_nmi() {
        // Check NMI state BEFORE ticking the disc controller.
        // This is critical for edge detection: after the CPU reads the DATA
        // register (clearing DRQ), we return 0. Then tick() sets DRQ for the
        // next byte. On the next poll_nmi(), we return 1, creating an edge.
        // If we ticked first, DRQ would be re-set before we return, and the
        // NMI line would never go low.
        uint8_t nmi = disc_controller.nmi_pending() ? 0x01 : 0x00;

        // Tick the disc controller (1MHz peripheral clock)
        disc_controller.tick();

        // Tick 1 MHz bus devices
        one_mhz_bus_.tick();

        return nmi;
    }

    OneMHzBusPort& one_mhz_bus() { return one_mhz_bus_; }

    UserPort& user_port() { return user_port_; }
    SerialPort& serial_port() { return serial_port_; }

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

    // =========================================================================
    // Open Bus Configuration
    // =========================================================================

    // Configure open bus behavior mode for unmapped address reads.
    // Default is Accurate mode which returns:
    //   - 0xFF for FRED/JIM (74LS245 transceiver)
    //   - 0x00 for slow 1MHz regions (pull-downs)
    //   - Last bus value for fast 2MHz regions (capacitance)
    //
    // Use JsbeebCompat mode for differential testing against jsbeeb.
    void set_open_bus_mode(OpenBusMode mode) {
        memory_map_.set_open_bus_mode(mode);
    }

    // Get current open bus behavior mode
    OpenBusMode open_bus_mode() const {
        return memory_map_.open_bus_mode();
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
        // DFS lives at slot 11 by default (IC68 high half).
        user_sockets_.load_rom(11, data, size, "");
    }

    // =========================================================================
    // Unified ROM Loading API
    // =========================================================================

    // Load ROM data into one of the B+'s sideways slots, forcing the
    // destination to ROM type. IC71 BASIC pair is special - it's a
    // raw Rom<16384> aliased across both slots of the S13-selected
    // pair, so loading either slot of that pair fills the same buffer.
    void load_sideways_rom(uint8_t slot, const uint8_t* data, size_t len,
                          std::string_view image_name = "") {
        if (user_sockets_.load_rom(slot, data, len, image_name)) return;
        if (slot == 0 || slot == 1 || slot == 14 || slot == 15) {
            const size_t copy_len = std::min<size_t>(len, 16384);
            std::copy_n(data, copy_len, basic_rom.data());
            if (!image_name.empty()) {
                basic_rom_image_name_ = std::string(image_name);
            }
        }
        // Slots 12 and 13 are not wired on the 64K B+ - quietly ignored.
    }

    // Load data into a slot WITHOUT changing slot type. Used by the
    // server for RAM-typed slots: configure_slot_as_ram() is called
    // first (setting kind=Ram), then load_sideways_data writes the
    // preload image into the now-RAM slot's buffer.
    void load_sideways_data(uint8_t slot, const uint8_t* data, size_t len,
                           std::string_view image_name = "") {
        user_sockets_.load_data(slot, data, len, image_name);
        // No fall-through: IC71 slots (0/1/14/15) and 12/13 don't make
        // sense as RAM on a 64K B+.
    }

    // Check if a slot can have ROM loaded.
    // B+ has slots in pairs; slots 12 and 13 are not present (would require 32K ROM in IC71).
    static constexpr bool is_slot_loadable(uint8_t slot) {
        // Valid slots: 0,1,2,3,4,5,6,7,8,9,10,11,14,15
        return slot <= 11 || slot >= 14;
    }

    // Configure a user-socket slot (IC35..IC68 half) as sideways RAM.
    // Third-party SRAM modules in any of the user ROM sockets were
    // commonplace on real B+s; the emulator honours that by letting
    // these slots actually be RAM at runtime - writes from CPU code
    // (including *SRLOAD) persist into the slot's buffer.
    //
    // IC71's BASIC half (slots 14/15 with S13=South or 0/1 with North)
    // is the system ROM and cannot be reconfigured.
    void configure_slot_as_ram(uint8_t slot) {
        user_sockets_.configure_as_ram(slot);
        // IC71 pair and 12/13 don't accept RAM configuration; quietly ignored.
    }

    void configure_slot_as_empty(uint8_t slot) {
        user_sockets_.configure_as_empty(slot);
    }

    // Uniform per-slot status query (see SlotInfo in ConfigurableSlot.hpp).
    //
    // User sockets (slots 2..11) reflect their ConfigurableSlot directly.
    //
    // The IC71 pair holds BASIC. S13 selects which slot pair it responds
    // at; the inactive pair is electrically unwired and reads as open
    // bus, so we report it as Empty. The active pair always presents the
    // BASIC ROM (Rom, populated, image_name = basic_rom_image_name_).
    SlotInfo slot_info(uint8_t slot) const {
        if (user_sockets_.owns(slot)) {
            return user_sockets_.slot_info(slot);
        }
        const bool is_basic_pair =
            s13_is_south_ ? (slot == 14 || slot == 15)
                          : (slot == 0 || slot == 1);
        if (is_basic_pair) {
            return {SlotType::Rom, true, basic_rom_image_name_};
        }
        return {};
    }

    // Motherboard links on the BBC Model B+ that affect sideways slot
    // mapping. Today we model link S13, which selects which slot pair the
    // BASIC half of the IC71 system ROM responds at:
    //
    //   * South (default factory position): BASIC appears at slots 14 and
    //     15. The MOS scans from 15 downward for the language ROM, so this
    //     gives BASIC the highest priority.
    //
    //   * North: BASIC appears at slots 0 and 1. Used when slot 15 is
    //     needed for a different language ROM and BASIC is paged in at
    //     the lowest-priority pair.
    //
    // The directions match the Model B+ Service Manual (sec. 5.4.1, Fig 4).
    //
    // The other six links described in the service manual (S9, S11, S12,
    // S15, S18, S19) are device-size links, one per ROM socket. They select
    // whether the chip in that socket is operated as a 16K device (link
    // West, ROMSEL bit 0 not routed to A14, the 16K image is aliased at
    // both slots of the pair) or a 32K device (link East, ROMSEL bit 0 ->
    // A14, two distinct 16K halves at the pair):
    //
    //     S9  IC35   |   S15 IC62
    //     S11 IC44   |   S18 IC68
    //     S12 IC57   |   S19 IC71 (hardwired East for the 32K MOS+BASIC)
    //
    // Earlier comments here described S18/S19 as speed links - they aren't,
    // I'd confused them with the Model B's links of the same number. The
    // stock B+ ships every user socket in West (16K mode), which matches
    // our current behaviour (each socket aliased across both slots of the
    // pair). 32K-device mode isn't modelled today; if a user installed a
    // 32K dual-ROM chip in a user socket the emulator would alias the
    // first 16K to both slot numbers instead of presenting two distinct
    // halves. Rare enough that we leave it as future work.
    struct MotherboardLinks {
        // Compile-time flag: this machine has slot-mapping links the user
        // can configure with --motherboard-link.
        static constexpr bool has_slot_links = true;

        enum class S13Position { South, North };
        S13Position s13 = S13Position::South;

        std::optional<std::string> parse(std::string_view key,
                                         std::string_view value) {
            auto lower = [](std::string_view sv) {
                std::string out;
                out.reserve(sv.size());
                for (char c : sv) {
                    out.push_back(static_cast<char>(std::tolower(
                        static_cast<unsigned char>(c))));
                }
                return out;
            };
            const std::string lkey = lower(key);
            const std::string lvalue = lower(value);
            if (lkey == "s13") {
                if (lvalue == "south") { s13 = S13Position::South; return std::nullopt; }
                if (lvalue == "north") { s13 = S13Position::North; return std::nullopt; }
                std::ostringstream msg;
                msg << "Invalid value for motherboard link S13: '" << value
                    << "' (expected: south, north)";
                return msg.str();
            }
            std::ostringstream msg;
            msg << "Unknown motherboard link '" << key
                << "' on BBC Model B+ (expected: s13)";
            return msg.str();
        }

        // Report current state of every modelled link. The values returned
        // here are the same strings parse() accepts, so a client can round-
        // trip them.
        std::vector<MotherboardLinkInfo> describe() const {
            return {{
                "S13",
                s13 == S13Position::South ? "south" : "north",
                "IC71 (BASIC) slot pair: South=slots 14/15 (default), North=slots 0/1"
            }};
        }

        // Lines describing every available link, formatted for inclusion in
        // --help under "Available links". One line per supported KEY=VALUE
        // form; whitespace-only lines are not produced so callers can join
        // with newlines without worrying about trailing blanks.
        static std::vector<std::string> help_lines() {
            return {
                "s13=south|north  IC71 (BASIC) slot pair "
                "(south=slots 14/15, north=slots 0/1; default: south)"
            };
        }
    };

    // Topology of the B+ motherboard's ROM positions.
    //
    // IC71 (BASIC + MOS) is genuinely aliased on real hardware: the chip's
    // sideways half is wired so both slot numbers of the S13-selected pair
    // return the same BASIC content. We model that faithfully -- IC71
    // contributes a single topology entry covering both slots.
    //
    // The other five sockets (IC35, IC44, IC57, IC62, IC68) each hold a
    // 32K-capable chip wired to two adjacent slot numbers. On real
    // hardware the device-size links (S9, S11, S12, S15, S18) decide
    // whether the chip is operated as a 16K device (image aliased
    // across both slots) or a 32K device (two distinct halves at the
    // two slots). The emulator presents each slot as an independent
    // 16K bank -- equivalent to the 32K-mode interpretation -- giving
    // each user socket two distinct topology entries so callers can
    // configure / *SRLOAD different images at each slot. The 16K-mode
    // case is the special case where both halves happen to hold the
    // same image.
    //
    // No socket is wired to slots 12 or 13 on a stock B+; reads from
    // those return open bus.
    //
    // All B+ slots are ROM-only and not runtime-configurable: the
    // motherboard does not provide sideways RAM, and the chip contents
    // cannot change while the machine is running.
    static SlotTopology slot_topology() {
        return slot_topology(MotherboardLinks{});
    }

    static SlotTopology slot_topology(MotherboardLinks links) {
        SlotTopology topo;
        topo.has_aliasing = true;  // IC71 answers two slot numbers as one bank
        std::vector<int> ic71_slots;
        switch (links.s13) {
            case MotherboardLinks::S13Position::South:
                ic71_slots = {14, 15}; break;
            case MotherboardLinks::S13Position::North:
                ic71_slots = {0, 1}; break;
        }
        struct Entry { const char* label; std::vector<int> slots; };
        const std::vector<Entry> entries = {
            {"IC71",         ic71_slots}, // BASIC (link S13 selects pair, aliased)
            // User sockets: each 32K-capable chip presented as two
            // independent 16K slot rows.
            {"IC35 (slot 2)", {2}},
            {"IC35 (slot 3)", {3}},
            {"IC44 (slot 4)", {4}},
            {"IC44 (slot 5)", {5}},
            {"IC57 (slot 6)", {6}},
            {"IC57 (slot 7)", {7}},
            {"IC62 (slot 8)", {8}},
            {"IC62 (slot 9)", {9}},
            {"IC68 (slot 10)", {10}},
            {"IC68 (slot 11)", {11}}   // DFS by default
        };
        for (size_t i = 0; i < entries.size(); ++i) {
            SocketSpec spec;
            spec.socket_index = static_cast<int>(i);
            spec.label = entries[i].label;
            spec.slots = entries[i].slots;
            // IC71 (entry 0) is the soldered MOS+BASIC system ROM - it
            // can't be configured as RAM or emptied. The user sockets
            // (IC35..IC68, entries 1..10) accept ROM, third-party SRAM,
            // or no chip at all - the SocketSpec defaults already say so.
            if (i == 0) {
                spec.supports_rom = true;
                spec.supports_ram = false;
                spec.supports_empty = false;
                spec.runtime_configurable = false;
            }
            topo.sockets.push_back(std::move(spec));
        }
        return topo;
    }

    // Apply motherboard link state to the runtime memory wiring. The
    // BankedMemory template binds IC71 (BASIC) to all four candidate slots
    // (0, 1, 14, 15) at construction; this method drops the inactive pair
    // based on the configured S13 position. Real hardware leaves those
    // slot numbers electrically unconnected when S13 routes IC71 to the
    // other pair, so reads should return open-bus (0xFF).
    //
    // Must be called once at start-up, after the Hardware is constructed
    // but before the BBC is allowed to fetch ROMs. Calling more than once
    // is harmless but unnecessary.
    void apply_motherboard_links(const MotherboardLinks& links) {
        s13_is_south_ = (links.s13 == MotherboardLinks::S13Position::South);
        switch (links.s13) {
            case MotherboardLinks::S13Position::South:
                // IC71 lives at slots 14/15; slots 0/1 are unwired.
                sideways.unbind_bank(0);
                sideways.unbind_bank(1);
                break;
            case MotherboardLinks::S13Position::North:
                // IC71 lives at slots 0/1; slots 14/15 are unwired.
                sideways.unbind_bank(14);
                sideways.unbind_bank(15);
                break;
        }
    }

    // =========================================================================
    // Startup Options (keyboard links)
    // =========================================================================

    // Set raw startup options byte (all 8 keyboard links)
    void set_startup_options(uint8_t options) {
        system_via_peripheral.keyboard().set_startup_options(options);
    }

    // Get raw startup options byte
    uint8_t startup_options() const {
        return system_via_peripheral.keyboard().startup_options();
    }

    // Set boot screen mode (0-7) - modifies bits 0-2 of startup options
    // Mode 7 is the default if not set.
    void set_screen_mode(uint8_t mode) {
        system_via_peripheral.keyboard().set_screen_mode(mode);
    }

    // Get current screen mode from startup options
    uint8_t screen_mode() const {
        return system_via_peripheral.keyboard().screen_mode();
    }

    // Set auto-boot flag - modifies bit 3 of startup options
    // When enabled, reverses SHIFT-BREAK action
    void set_auto_boot(bool enabled) {
        system_via_peripheral.keyboard().set_auto_boot(enabled);
    }

    // Get auto-boot flag from startup options
    bool auto_boot() const {
        return system_via_peripheral.keyboard().auto_boot();
    }

    // =========================================================================
    // Disc Controller Configuration
    // =========================================================================

    // Enable/disable motor spin-up delay
    // @param enabled true to enable realistic delays, false to skip (fast)
    void set_spin_up_delay_enabled(bool enabled) {
        disc_controller.set_spin_up_delay_enabled(enabled);
    }

    // Check if spin-up delay is enabled
    bool spin_up_delay_enabled() const {
        return disc_controller.spin_up_delay_enabled();
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

    /// Find a region descriptor by name.
    /// @returns pointer to descriptor, or nullptr if not found.
    const MemoryRegionDescriptor* find_region(std::string_view name) const {
        if (name == REGION_MAIN_RAM) {
            static const MemoryRegionDescriptor desc{
                REGION_MAIN_RAM, 0x0000, 0x8000, RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
            };
            return &desc;
        }
        if (name == REGION_SHADOW_RAM) {
            static const MemoryRegionDescriptor desc{
                REGION_SHADOW_RAM, 0x3000, 0x5000, RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
            };
            return &desc;
        }
        if (name == REGION_ANDY_RAM) {
            static const MemoryRegionDescriptor desc{
                REGION_ANDY_RAM, 0x8000, 0x3000, RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
            };
            return &desc;
        }
        if (name == REGION_MOS_ROM) {
            static const MemoryRegionDescriptor desc{
                REGION_MOS_ROM, 0xC000, 0x4000, RegionFlags::Readable | RegionFlags::Populated
            };
            return &desc;
        }
        if (name.size() >= 6 && name.substr(0, 5) == "bank_") {
            uint8_t bank = parse_bank_number(name);
            if (bank < 16) {
                return &bank_descriptors_[bank];
            }
        }
        return nullptr;
    }

    /// Check if a region name is valid for this machine type.
    bool has_region(std::string_view name) const {
        return find_region(name) != nullptr;
    }

    /// Read from a named memory region without side effects.
    /// @throws std::invalid_argument for unknown region name or out-of-bounds address.
    uint8_t peek_region(std::string_view name, uint32_t address) const {
        const auto* region = find_region(name);
        if (!region) {
            throw std::invalid_argument("unknown region: '" + std::string(name) + "'");
        }
        validate_region_address(*region, address);
        return peek_region_unchecked(name, address);
    }

    /// Read from a named memory region (may have side effects).
    /// @throws std::invalid_argument for unknown region name or out-of-bounds address.
    uint8_t read_region(std::string_view name, uint32_t address) {
        const auto* region = find_region(name);
        if (!region) {
            throw std::invalid_argument("unknown region: '" + std::string(name) + "'");
        }
        validate_region_address(*region, address);
        return read_region_unchecked(name, address);
    }

    /// Write to a named memory region.
    /// @throws std::invalid_argument for unknown region name or out-of-bounds address.
    void write_region(std::string_view name, uint32_t address, uint8_t value) {
        const auto* region = find_region(name);
        if (!region) {
            throw std::invalid_argument("unknown region: '" + std::string(name) + "'");
        }
        validate_region_address(*region, address);
        write_region_unchecked(name, address, value);
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

    // Static descriptors for bank regions (all have same base/size)
    static constexpr auto bank_descriptors_ = make_bank_descriptors(bank_names_);

    // Unchecked accessors - caller must validate region and address first
    uint8_t peek_region_unchecked(std::string_view name, uint32_t address) const {
        if (name == REGION_MAIN_RAM) {
            return main_ram.read(static_cast<uint16_t>(address));
        }
        if (name == REGION_SHADOW_RAM) {
            return shadow_ram.read(static_cast<uint16_t>(address - 0x3000));
        }
        if (name == REGION_ANDY_RAM) {
            return andy_ram.read(static_cast<uint16_t>(address - 0x8000));
        }
        if (name == REGION_MOS_ROM) {
            return mos_rom.read(static_cast<uint16_t>(address - 0xC000));
        }
        uint8_t bank = parse_bank_number(name);
        return sideways.peek_bank(bank, static_cast<uint16_t>(address - 0x8000));
    }

    uint8_t read_region_unchecked(std::string_view name, uint32_t address) {
        if (name == REGION_MAIN_RAM) {
            return main_ram.read(static_cast<uint16_t>(address));
        }
        if (name == REGION_SHADOW_RAM) {
            return shadow_ram.read(static_cast<uint16_t>(address - 0x3000));
        }
        if (name == REGION_ANDY_RAM) {
            return andy_ram.read(static_cast<uint16_t>(address - 0x8000));
        }
        if (name == REGION_MOS_ROM) {
            return mos_rom.read(static_cast<uint16_t>(address - 0xC000));
        }
        uint8_t bank = parse_bank_number(name);
        return sideways.read_bank(bank, static_cast<uint16_t>(address - 0x8000));
    }

    void write_region_unchecked(std::string_view name, uint32_t address, uint8_t value) {
        if (name == REGION_MAIN_RAM) {
            main_ram.write(static_cast<uint16_t>(address), value);
            return;
        }
        if (name == REGION_SHADOW_RAM) {
            shadow_ram.write(static_cast<uint16_t>(address - 0x3000), value);
            return;
        }
        if (name == REGION_ANDY_RAM) {
            andy_ram.write(static_cast<uint16_t>(address - 0x8000), value);
            return;
        }
        if (name == REGION_MOS_ROM) {
            return;  // ROM is read-only, silently ignore
        }
        uint8_t bank = parse_bank_number(name);
        sideways.write_bank(bank, static_cast<uint16_t>(address - 0x8000), value);
    }

private:
    MemoryMapType memory_map_;
    IrqAggregatorType irq_aggregator_;

    IrqAggregatorType make_irq_aggregator() {
        return beebium::make_irq_aggregator(
            make_irq_binding<0>(system_via),
            make_irq_binding<1>(user_via),
            make_irq_binding<2>(tube_socket),
            make_irq_binding<3>(one_mhz_bus_),
            make_irq_binding<4>(serial_socket)
        );
    }

    MemoryMapType make_memory_map() {
        // Order matters: first match wins
        // I/O regions overlay MOS ROM at 0xFC00-0xFEFF
        return MemoryMap{
            make_region<0xFC00, 0xFDFF>(one_mhz_bus_),                              // FRED/JIM (overlays MOS ROM)
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(crtc),
            make_region<0xFE08, 0xFE0F, Mirror<0x01>>(serial_acia_region_),       // Serial ACIA (MC6850)
            make_region<0xFE10, 0xFE17, Mirror<0x07>>(serial_ula_region_),        // Serial ULA (SERPROC)
            make_region<0xFE18, 0xFE1F, Mirror<0x07>>(econet_station_id_region_), // Econet station ID + INTOFF
            make_region<0xFE20, 0xFE2F, Mirror<0x01>>(video_ula_with_inton_),     // Video ULA + INTON
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(system_via),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(user_via),
            make_region<0xFE30, 0xFE33, Mirror<0x03>>(romsel_reg),
            make_region<0xFE34, 0xFE37, Mirror<0x03>>(acccon_reg),
            make_region<0xFE80, 0xFE83, Mirror<0x03>>(disc_control_reg),
            make_region<0xFE84, 0xFE87, Mirror<0x03>>(disc_controller),
            make_region<0xFEA0, 0xFEBF, Mirror<0x03>>(econet_adlc_region_),       // Econet ADLC
            make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(tube_socket),               // Tube ULA
            make_region<0x0000, 0x7FFF>(main_ram),
            make_region<0x8000, 0xBFFF>(sideways),
            make_region<0xC000, 0xFFFF>(mos_rom)                                   // MOS ROM (occluded by I/O regions)
        };
    }
};

} // namespace beebium
