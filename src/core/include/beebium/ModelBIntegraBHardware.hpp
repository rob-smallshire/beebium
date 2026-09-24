// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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
#include "ClockTypes.hpp"
#include "IntegraBBatterySeed.hpp"
#include "IrqAggregator.hpp"
#include "PacingConfig.hpp"
#include "MemoryMap.hpp"
#include "MemoryRegion.hpp"
#include "MotherboardLinks.hpp"
#include "OutputQueue.hpp"
#include "Saa5050.hpp"
#include "SlotProtection.hpp"
#include "SlotTopology.hpp"
#include "SystemViaPeripheral.hpp"
#include "Via6522.hpp"
#include "PixelBatch.hpp"
#include "devices/ConfigurableBankedMemory.hpp"
#include "devices/Crtc6845.hpp"
#include "devices/Mc146818Rtc.hpp"
#include "devices/Ram.hpp"
#include "devices/Rom.hpp"
#include "devices/Sn76489.hpp"
#include "devices/VideoUla.hpp"
#include "disc/Acorn1770DiscController.hpp"
#include "disc/DiscControllerSocket.hpp"
#include "disc/DiscDrive.hpp"
#include "econet/EconetSocket.hpp"
#include "serial/SerialSocket.hpp"
#include "tube/TubeSocket.hpp"
#include "indicators/Indicators.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace beebium {

// BBC Model B fitted with the Computech Integra-B expansion board (1988-89).
//
// The board plugs into the 6502 socket (the CPU moves onto the board) and adds,
// under the control of its IBOS ROM:
//
// - Full 4-bit sideways decoding. Slots 0-3 are the four motherboard ROM
//   sockets; slots 4-7 are the board's four sideways RAM banks (two 32K static
//   RAMs, "RAM 4/5" and "RAM 6/7"); slots 8-15 are four pairs of 28-pin sockets
//   (8/9, 10/11, 12/13, 14/15). A pair holds two 8K/16K ROMs, or one 32K RAM or
//   ROM whose two halves decode as both slots of the pair.
//
// - 20K shadow RAM at &3000-&7FFF and 12K private RAM at &8000-&AFFF. They are
//   one 32K static RAM addressed by A0-A14: private RAM is chip &0000-&2FFF and
//   shadow RAM chip &3000-&7FFF. The video circuitry, on the motherboard, always
//   reads main memory: shadow RAM holds programs and data, never the screen.
//
// - Two write-only control latches, cleared by the 6502 reset line:
//     ROMSEL &FE30: bits 0-3 bank, bit 6 PRVEN (private RAM enable),
//                   bit 7 MEMSEL (select main memory while shadow is enabled)
//     RAMSEL &FE34: bit 4 PRVS8 (&9000-&AFFF), bit 5 PRVS4 (&8000-&8FFF),
//                   bit 6 PRVS1 (&8000-&83FF), bit 7 SHEN (shadow enable)
//   Only A2-A15 are decoded, so each latch is mirrored over four addresses.
//
// - A CDP6818 (MC146818) real-time clock with 50 bytes of CMOS RAM: the address
//   is written to &FE38 and data read/written at &FE3C. Its IRQ output drives
//   the CPU IRQ line and its RESET input is the 6502 reset line.
//
// - Battery backup for all board RAM and the clock. Battery-backed contents are
//   not persisted between launches; each launch starts from a board that has
//   been set up (IntegraBBatterySeed), not from a flat battery.
//
// - Per-chip write-protect switches (IBOS guide 9-6): a switch holds one 32K
//   RAM chip's write-enable high, protecting both of its 16K slots together.
//   Modelled as protection groups "slots-4-5", "slots-6-7" and, for socket
//   pairs fitted with RAM, "slots-8-9" ... "slots-14-15".
//
class ModelBIntegraBHardware {
public:
    // Machine identification and region names (compile-time constants)
    static constexpr std::string_view MACHINE_TYPE = "model-b-integra-b";
    constexpr std::string_view machine_type() const { return MACHINE_TYPE; }
    static constexpr std::string_view MACHINE_DISPLAY_NAME = "BBC Model B with Integra-B";
    static constexpr std::string_view MACHINE_DESCRIPTION = "Model B fitted with the Computech Integra-B board (IBOS, sideways RAM banks 4-7, eight ROM/RAM sockets 8-15, 20K shadow RAM, 12K private RAM, real-time clock)";
    static constexpr std::string_view REGION_MAIN_RAM = "main_ram";
    static constexpr std::string_view REGION_SHADOW_RAM = "shadow_ram";
    static constexpr std::string_view REGION_PRIVATE_RAM = "private_ram";
    static constexpr std::string_view REGION_MOS_ROM = "mos_rom";

    // Default ROM filenames for this machine
    static constexpr std::string_view DEFAULT_MOS_ROM = "acorn-mos_1_20.rom";
    static constexpr std::string_view DEFAULT_LANGUAGE_ROM = "bbc-basic_2.rom";
    // BASIC stays on the motherboard, in its highest socket, as in the IBOS
    // guide's own *ROMS example (section 2-4.2).
    static constexpr uint8_t DEFAULT_LANGUAGE_SLOT = 3;
    // The IBOS ROM is supplied with the board and must be in the highest
    // priority socket (IBOS guide 9-4).
    static constexpr std::string_view DEFAULT_BOARD_ROM = "computech-ibos_1_26.rom";
    static constexpr uint8_t DEFAULT_BOARD_ROM_SLOT = 15;

    // Launch option for the board's real-time clock: --integra-rtc
    // clock=<host|emulated>[:time=...|:offset=...].
    static constexpr std::string_view BOARD_RTC_OPTION = "integra-rtc";

    // The board's own sideways RAM banks, always fitted.
    static constexpr uint8_t FIRST_BOARD_RAM_SLOT = 4;
    static constexpr uint8_t LAST_BOARD_RAM_SLOT = 7;
    static constexpr uint8_t FIRST_SOCKET_PAIR_SLOT = 8;

    // Latch bits
    static constexpr uint8_t ROMSEL_BANK_MASK = 0x0F;
    static constexpr uint8_t ROMSEL_PRVEN = 0x40;
    static constexpr uint8_t ROMSEL_MEMSEL = 0x80;
    static constexpr uint8_t ROMSEL_LATCHED = ROMSEL_MEMSEL | ROMSEL_PRVEN | ROMSEL_BANK_MASK;
    static constexpr uint8_t RAMSEL_PRVS8 = 0x10;
    static constexpr uint8_t RAMSEL_PRVS4 = 0x20;
    static constexpr uint8_t RAMSEL_PRVS1 = 0x40;
    static constexpr uint8_t RAMSEL_SHEN = 0x80;
    static constexpr uint8_t RAMSEL_LATCHED = RAMSEL_SHEN | RAMSEL_PRVS1 | RAMSEL_PRVS4 | RAMSEL_PRVS8;

    // Default pacing configuration for this machine
    static constexpr PacingConfig default_pacing_config() {
        return {
            .base_clock_hz = timing::CPU_HZ,  // 2 MHz
            .pacing_hz = 200,                  // 200 Hz sync rate
            .speed_multiplier = 1.0            // Real-time
        };
    }

    // Hardware devices (owned by this struct)
    Ram<32768> main_ram;
    Rom<16384> mos_rom;

    // Sideways ROM/RAM (16 independent slots, full 4-bit decoding).
    using SidewaysType = ConfigurableBankedMemory;
    SidewaysType sideways{};

    // The board's 32K static RAM holding private RAM (chip &0000-&2FFF, seen at
    // &8000-&AFFF) and shadow RAM (chip &3000-&7FFF, seen at &3000-&7FFF).
    Ram<32768> shadow_and_private_ram;

    // CDP6818 real-time clock with CMOS RAM.
    Mc146818Rtc rtc;

    Via6522 system_via;
    Via6522 user_via;

    // User Port handle (must be declared after user_via for init order)
    UserPort user_port_{user_via};

    // IRQ aggregator type - polls VIAs, Tube, and 1 MHz bus for IRQ status
    using IrqAggregatorType = IrqAggregator<
        IrqBinding<Via6522, 0>,        // System VIA -> bit 0
        IrqBinding<Via6522, 1>,        // User VIA -> bit 1
        IrqBinding<TubeSocket, 2>,     // Tube HIRQ -> bit 2
        IrqBinding<OneMHzBusPort, 3>,  // 1 MHz bus devices (e.g. SCSI) -> bit 3
        IrqBinding<SerialSocket, 4>,   // Serial ACIA (MC6850) -> bit 4
        IrqBinding<Mc146818Rtc, 5>     // Integra-B real-time clock -> bit 5
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

    // Hardware indicators (LEDs) - must be declared before SystemViaPeripheral
    Indicators indicators;

    // System VIA peripherals
    AddressableLatch addressable_latch;
    SystemViaPeripheral system_via_peripheral{addressable_latch, indicators};

    // Disc subsystem
    DiscDrive disc_drive_0{indicators, "floppy-0-activity-led", "FDD 0", "568nm"};
    DiscDrive disc_drive_1{indicators, "floppy-1-activity-led", "FDD 1", "568nm"};

    // Disc controller socket at 0xFE80-0xFE9F
    DiscControllerSocket disc_socket;

    // Registry ID of installed controller
    std::string installed_controller_id_;

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

    // ROMSEL latch (&FE30-&FE33): bank select plus PRVEN and MEMSEL.
    struct RomselRegister {
        ModelBIntegraBHardware& hw;
        uint8_t read(uint16_t) { return 0xFF; }  // Write-only latch
        void write(uint16_t, uint8_t value) { hw.write_romsel(value); }
    };

    // RAMSEL latch (&FE34-&FE37): shadow enable and private RAM area selects.
    struct RamselRegister {
        ModelBIntegraBHardware& hw;
        uint8_t read(uint16_t) { return 0xFF; }  // Write-only latch
        void write(uint16_t, uint8_t value) { hw.ramsel_ = value & RAMSEL_LATCHED; }
    };

    // RTC address strobe (&FE38-&FE3B).
    struct RtcAddressRegister {
        Mc146818Rtc& rtc;
        uint8_t read(uint16_t) { return 0xFF; }
        void write(uint16_t, uint8_t value) { rtc.write_address(value); }
    };

    // RTC data (&FE3C-&FE3F).
    struct RtcDataRegister {
        Mc146818Rtc& rtc;
        uint8_t read(uint16_t) { return rtc.read_data(); }
        void write(uint16_t, uint8_t value) { rtc.write_data(value); }
    };

    RomselRegister romsel_register{*this};
    RamselRegister ramsel_register{*this};
    RtcAddressRegister rtc_address_register{rtc};
    RtcDataRegister rtc_data_register{rtc};

    // 1 MHz expansion bus (FRED/JIM, 0xFC00-0xFDFF).
    OneMHzBusPort one_mhz_bus_;

    // Memory map type
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
            make_region<0xFE30, 0xFE33, Mirror<0x03>>(std::declval<RomselRegister&>()),
            make_region<0xFE34, 0xFE37, Mirror<0x03>>(std::declval<RamselRegister&>()),
            make_region<0xFE38, 0xFE3B, Mirror<0x03>>(std::declval<RtcAddressRegister&>()),
            make_region<0xFE3C, 0xFE3F, Mirror<0x03>>(std::declval<RtcDataRegister&>()),
            make_region<0xFE80, 0xFE9F, Mirror<0x1F>>(std::declval<DiscControllerSocket&>()),
            make_region<0xFEA0, 0xFEBF, Mirror<0x03>>(std::declval<EconetAdlcRegion&>()),        // Econet ADLC
            make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(std::declval<TubeSocket&>()),              // Tube ULA
            make_region<0x0000, 0x7FFF>(std::declval<Ram<32768>&>()),
            make_region<0x8000, 0xBFFF>(std::declval<SidewaysType&>()),
            make_region<0xC000, 0xFFFF>(std::declval<Rom<16384>&>())       // MOS ROM (occluded by I/O regions)
        }
    );

    // Default constructor
    ModelBIntegraBHardware()
        : system_via()
        , user_via()
        , memory_map_(make_memory_map())
        , irq_aggregator_(make_irq_aggregator())
    {
        system_via.set_peripheral(&system_via_peripheral);
        system_via_peripheral.set_sound_chip(&sound_chip);
        // Note: indicators.start() is deferred to the server bootstrap, after
        // extension init() runs. This keeps the registration window open for
        // extensions; closing it before extensions can register would violate
        // the Indicators register-before-start contract.
        econet_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        serial_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        tube_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        fit_board_ram();
    }

    // Constructor with custom peripherals (for testing)
    ModelBIntegraBHardware(ViaPeripheral& system_peripheral, ViaPeripheral& user_peripheral)
        : system_via(system_peripheral)
        , user_via(user_peripheral)
        , memory_map_(make_memory_map())
        , irq_aggregator_(make_irq_aggregator())
    {
        econet_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        serial_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        tube_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        fit_board_ram();
    }

    ~ModelBIntegraBHardware() {
        indicators.stop();
    }

    // MemoryMappedDevice interface
    uint8_t read(uint16_t addr) {
        if (shadow_selected_for(addr)) {
            return shadow_and_private_ram.read(addr);
        }
        if (private_ram_selected_for(addr)) {
            return shadow_and_private_ram.read(static_cast<uint16_t>(addr - 0x8000));
        }
        return memory_map_.read(addr);
    }

    void write(uint16_t addr, uint8_t value) {
        // Writes to shadow or private RAM do not reach the motherboard: the
        // board holds the motherboard R/W line high for them.
        if (shadow_selected_for(addr)) {
            shadow_and_private_ram.write(addr, value);
            return;
        }
        if (private_ram_selected_for(addr)) {
            shadow_and_private_ram.write(static_cast<uint16_t>(addr - 0x8000), value);
            return;
        }
        memory_map_.write(addr, value);
    }

    // Latch state
    uint8_t romsel() const { return romsel_; }
    uint8_t ramsel() const { return ramsel_; }

    // True when shadow RAM replaces main memory at &3000-&7FFF for the CPU:
    // SHEN set and MEMSEL clear (IBOS guide 8-2).
    bool shadow_selected() const {
        return (ramsel_ & RAMSEL_SHEN) != 0 && (romsel_ & ROMSEL_MEMSEL) == 0;
    }

    // True when private RAM overlays the sideways region at `addr` (IBOS guide
    // 8-2 truth table). PRVEN is the master enable; PRVS1 selects &8000-&83FF,
    // PRVS4 &8000-&8FFF, PRVS8 &9000-&AFFF.
    bool private_ram_selected_for(uint16_t addr) const {
        if ((romsel_ & ROMSEL_PRVEN) == 0 || addr < 0x8000 || addr >= 0xB000) return false;
        if (addr < 0x8400) return (ramsel_ & (RAMSEL_PRVS1 | RAMSEL_PRVS4)) != 0;
        if (addr < 0x9000) return (ramsel_ & RAMSEL_PRVS4) != 0;
        return (ramsel_ & RAMSEL_PRVS8) != 0;
    }

    // Direct shadow/private RAM access by CPU address (debugging and tests).
    uint8_t shadow_ram_peek(uint16_t addr) const {
        return (addr >= 0x3000 && addr < 0x8000) ? shadow_and_private_ram.read(addr) : 0xFF;
    }
    uint8_t private_ram_peek(uint16_t addr) const {
        return (addr >= 0x8000 && addr < 0xB000)
                   ? shadow_and_private_ram.read(static_cast<uint16_t>(addr - 0x8000))
                   : 0xFF;
    }

    // Clock expansion-board devices by one 2 MHz CPU cycle (called by Machine
    // on every cycle, including 1 MHz bus stretch cycles).
    void tick_expansion_devices() { rtc.tick(); }

    // Slot protection groups: one per 32K sideways RAM chip. Each chip's
    // optional write-protect switch protects both of its 16K slots together.
    std::vector<SlotProtectionGroup> protection_groups() const {
        std::vector<SlotProtectionGroup> groups;
        for (int first = FIRST_BOARD_RAM_SLOT; first < 16; first += 2) {
            const bool on_board = first <= LAST_BOARD_RAM_SLOT;
            const auto a = static_cast<uint8_t>(first);
            const auto b = static_cast<uint8_t>(first + 1);
            if (!on_board && !(sideways.bank_type(a) == SlotType::Ram &&
                               sideways.bank_type(b) == SlotType::Ram)) {
                continue;
            }
            SlotProtectionGroup g;
            g.id = ram_chip_group_id(first);
            g.label = "Write-protect slots " + std::to_string(first) + "/" +
                      std::to_string(first + 1);
            if (on_board) {
                // The board's own chips have marked write-protect links.
                g.label += " (WP " + std::to_string(first) + "/" + std::to_string(first + 1) + ")";
            }
            g.slots = {first, first + 1};
            g.supports_write_protect = true;
            g.write_protected = sideways.is_slot_write_protected(a);
            groups.push_back(std::move(g));
        }
        return groups;
    }

    bool set_protection(std::string_view group_id, ProtectionKind kind, bool engaged) {
        if (kind != ProtectionKind::WriteProtect) return false;
        for (const auto& g : protection_groups()) {
            if (g.id != group_id) continue;
            for (int slot : g.slots) {
                sideways.set_slot_write_protected(static_cast<uint8_t>(slot), engaged);
            }
            return true;
        }
        return false;
    }

    static std::string ram_chip_group_id(int first_slot) {
        return "slots-" + std::to_string(first_slot) + "-" + std::to_string(first_slot + 1);
    }

    uint8_t peek(uint16_t addr) const {
        if (shadow_selected_for(addr)) {
            return shadow_and_private_ram.read(addr);
        }
        if (private_ram_selected_for(addr)) {
            return shadow_and_private_ram.read(static_cast<uint16_t>(addr - 0x8000));
        }
        if (addr >= 0xFE3C && addr <= 0xFE3F) {
            return rtc.peek_register(rtc.address());  // no register C side effect
        }
        if (addr >= 0xFE40 && addr <= 0xFE5F) {
            return system_via.peek(addr & 0x0F);
        }
        if (addr >= 0xFE60 && addr <= 0xFE7F) {
            return user_via.peek(addr & 0x0F);
        }
        if (addr >= 0xFEE0 && addr <= 0xFEFF) {
            return tube_socket.peek(addr & 0x07);
        }
        return memory_map_.read(addr);
    }

    // The video circuitry is on the motherboard: it always reads main memory.
    uint8_t peek_video(uint16_t addr) const {
        return main_ram.read(addr);
    }

    // Power-on. Board RAM, the clock and the write-protect switches are battery
    // backed or physical, so they persist; the latches and the clock's
    // interrupt state follow the reset line.
    void reset() {
        main_ram.clear();
        system_via.reset();
        user_via.reset();
        crtc.reset();
        video_ula.reset();
        saa5050.reset();
        sound_chip.reset();
        addressable_latch.reset();
        reset_board_latches();
        disc_socket.reset();
        econet_socket.reset();
        serial_socket.reset();
        tube_socket.reset();
    }

    // Break. The board's latches and the clock's RESET pin are wired to the
    // 6502 reset line, so Break clears them too.
    void soft_reset() {
        user_via.reset();
        crtc.reset();
        video_ula.reset();
        saa5050.reset();
        sound_chip.reset();
        addressable_latch.reset();
        reset_board_latches();
        disc_socket.reset();
        econet_socket.reset();
        serial_socket.reset();
        tube_socket.reset();
    }

    void enable_video_output(size_t capacity = OutputQueue<PixelBatch>::DEFAULT_CAPACITY) {
        video_output.emplace(capacity);
    }

    void disable_video_output() {
        video_output.reset();
    }

    bool video_output_enabled() const {
        return video_output.has_value();
    }

    void enable_audio_output(size_t capacity = AudioBuffer::DEFAULT_CAPACITY) {
        if (!audio_buffer) {
            audio_buffer.emplace(capacity);
        }
    }

    void disable_audio_output() {
        audio_buffer.reset();
    }

    bool audio_output_enabled() const {
        return audio_buffer.has_value();
    }

    uint8_t poll_irq() {
        return irq_aggregator_.poll();
    }

    uint8_t poll_nmi() {
        uint8_t nmi = disc_socket.nmi_pending() ? 0x01 : 0x00;
        disc_socket.tick();
        one_mhz_bus_.tick();
        return nmi;
    }

    OneMHzBusPort& one_mhz_bus() { return one_mhz_bus_; }

    UserPort& user_port() { return user_port_; }
    SerialPort& serial_port() { return serial_port_; }

    // Startup Options
    void set_startup_options(uint8_t options) {
        system_via_peripheral.keyboard().set_startup_options(options);
    }

    uint8_t startup_options() const {
        return system_via_peripheral.keyboard().startup_options();
    }

    void set_screen_mode(uint8_t mode) {
        system_via_peripheral.keyboard().set_screen_mode(mode);
    }

    uint8_t screen_mode() const {
        return system_via_peripheral.keyboard().screen_mode();
    }

    void set_auto_boot(bool enabled) {
        system_via_peripheral.keyboard().set_auto_boot(enabled);
    }

    bool auto_boot() const {
        return system_via_peripheral.keyboard().auto_boot();
    }

    // Disc Controller Management
    void install_disc_controller(std::unique_ptr<DiscControllerInterface> controller,
                                 std::string_view controller_id = "") {
        disc_socket.install(std::move(controller));
        disc_socket.attach_drive(0, &disc_drive_0);
        disc_socket.attach_drive(1, &disc_drive_1);
        installed_controller_id_ = controller_id;
    }

    void install_acorn_1770() {
        install_disc_controller(std::make_unique<Acorn1770DiscController>(), "acorn-1770");
    }

    std::unique_ptr<DiscControllerInterface> remove_disc_controller() {
        installed_controller_id_.clear();
        return disc_socket.remove();
    }

    bool has_disc_controller() const {
        return disc_socket.has_controller();
    }

    std::string_view installed_controller_id() const {
        return installed_controller_id_;
    }

    void set_spin_up_delay_enabled(bool enabled) {
        if (auto* ctrl = disc_socket.controller()) {
            ctrl->set_spin_up_delay_enabled(enabled);
        }
    }

    bool spin_up_delay_enabled() const {
        if (auto* ctrl = disc_socket.controller()) {
            return ctrl->spin_up_delay_enabled();
        }
        return false;
    }

    // ROM Loading
    void load_mos(const uint8_t* data, size_t size) {
        mos_rom.load(data, size);
    }

    void load_basic(const uint8_t* data, size_t size) {
        sideways.configure_slot(DEFAULT_LANGUAGE_SLOT, SlotType::Rom);
        sideways.load_rom(DEFAULT_LANGUAGE_SLOT, data, size);
        sideways.set_slot_image_name(DEFAULT_LANGUAGE_SLOT, DEFAULT_LANGUAGE_ROM);
    }

    // Load ROM into a specific slot
    void load_rom_to_slot(uint8_t slot, const uint8_t* data, size_t size,
                         std::string_view image_name = "") {
        sideways.configure_slot(slot, SlotType::Rom);
        sideways.load_rom(slot, data, size);
        if (!image_name.empty()) {
            sideways.set_slot_image_name(slot, image_name);
        }
    }

    // Configure a slot's type
    void configure_slot(uint8_t slot, SlotType type) {
        sideways.configure_slot(slot, type);
    }

    // Load ROM by slot number
    void load_rom(uint8_t slot, const uint8_t* data, size_t size) {
        sideways.configure_slot(slot, SlotType::Rom);
        sideways.load_rom(slot, data, size);
    }

    // Load ROM data into a slot, automatically configuring as ROM type.
    void load_sideways_rom(uint8_t slot, const uint8_t* data, size_t len,
                          std::string_view image_name = "") {
        sideways.load_sideways_rom(slot, data, len, image_name);
    }

    // Load data into a slot WITHOUT changing slot type.
    void load_sideways_data(uint8_t slot, const uint8_t* data, size_t len,
                           std::string_view image_name = "") {
        sideways.load_sideways_data(slot, data, len, image_name);
    }

    // Check if a slot can have ROM loaded
    static constexpr bool is_slot_loadable(uint8_t slot) {
        return ConfigurableBankedMemory::is_slot_loadable(slot);
    }

    // Configure a slot as writable RAM
    void configure_slot_as_ram(uint8_t slot) {
        sideways.configure_slot_as_ram(slot);
    }

    // Configure a slot as empty
    void configure_slot_as_empty(uint8_t slot) {
        sideways.configure_slot_as_empty(slot);
    }

    // Uniform per-slot status query used by SidewaysService.
    SlotInfo slot_info(uint8_t slot) const {
        return sideways.slot_info(slot);
    }

    // Write protection is per RAM chip, exposed as protection groups (see
    // protection_groups / set_protection above). The board deliberately does
    // not implement the per-slot set_slot_write_protected surface, so the
    // per-slot launch --sideways ...:write-protect flag is rejected for it.

    // The Integra-B decodes all four ROMSEL bits (no aliasing); no motherboard
    // links affect slot mapping.
    using MotherboardLinks = EmptyMotherboardLinks;

    // Topology of the 16 sideways slots on a Model B fitted with the Integra-B:
    // - slots 0-3  : the four motherboard ROM sockets (ROM or empty),
    // - slots 4-7  : the board's sideways RAM (always fitted),
    // - slots 8-15 : four socket pairs; each socket may hold a ROM or be empty,
    //                or a pair may hold one 32K RAM spanning both of its slots.
    // Which chip is in which socket is fixed at launch (like fitting a chip).
    static SlotTopology slot_topology(MotherboardLinks /*links*/ = {}) {
        static constexpr const char* motherboard_ic[4] = {"IC52", "IC88", "IC100", "IC101"};
        SlotTopology topo;
        topo.has_aliasing = false;
        for (int slot = 0; slot < 16; ++slot) {
            SocketSpec spec;
            spec.socket_index = slot;
            spec.slots = {slot};
            spec.runtime_configurable = false;
            spec.supports_write_protect = false;  // per chip, via protection groups
            if (slot < FIRST_BOARD_RAM_SLOT) {
                spec.label = std::string("Motherboard ") + motherboard_ic[slot];
                spec.supports_rom = true;
                spec.supports_ram = false;
                spec.supports_empty = true;
            } else if (slot <= LAST_BOARD_RAM_SLOT) {
                spec.label = "Board RAM " + std::to_string(slot & ~1) + "/" +
                             std::to_string((slot & ~1) + 1);
                spec.supports_rom = false;
                spec.supports_ram = true;
                spec.supports_empty = false;
            } else {
                spec.label = "Board socket " + std::to_string(slot);
                spec.supports_rom = true;
                spec.supports_ram = true;
                spec.supports_empty = true;
            }
            topo.sockets.push_back(std::move(spec));
        }
        topo.ram_chips = {{8, 9}, {10, 11}, {12, 13}, {14, 15}};
        return topo;
    }

    // =========================================================================
    // Open Bus Configuration
    // =========================================================================

    void set_open_bus_mode(OpenBusMode mode) {
        memory_map_.set_open_bus_mode(mode);
    }

    OpenBusMode open_bus_mode() const {
        return memory_map_.open_bus_mode();
    }

    // Memory region discovery
    std::vector<MemoryRegionDescriptor> get_memory_regions() const {
        std::vector<MemoryRegionDescriptor> regions;

        regions.push_back({
            REGION_MAIN_RAM,
            0x0000,
            32768,
            RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
        });

        RegionFlags shadow_flags = RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated;
        if (shadow_selected()) shadow_flags = shadow_flags | RegionFlags::Active;
        regions.push_back({REGION_SHADOW_RAM, 0x3000, 0x5000, shadow_flags});

        RegionFlags private_flags = RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated;
        if (romsel_ & ROMSEL_PRVEN) private_flags = private_flags | RegionFlags::Active;
        regions.push_back({REGION_PRIVATE_RAM, 0x8000, 0x3000, private_flags});

        regions.push_back({
            REGION_MOS_ROM,
            0xC000,
            16384,
            RegionFlags::Readable | RegionFlags::Populated
        });

        for (uint8_t bank = 0; bank < 16; ++bank) {
            RegionFlags flags = RegionFlags::Readable;
            if (sideways.bank_type(bank) == SlotType::Ram) {
                flags = flags | RegionFlags::Writable;
            }
            if (sideways.is_bank_populated(bank)) {
                flags = flags | RegionFlags::Populated;
            }
            if (bank == sideways.selected_bank()) {
                flags = flags | RegionFlags::Active;
            }
            regions.push_back({bank_names_[bank], 0x8000, 16384, flags});
        }

        return regions;
    }

    /// Find a region descriptor by name.
    /// @returns pointer to descriptor, or nullptr if not found.
    const MemoryRegionDescriptor* find_region(std::string_view name) const {
        static const MemoryRegionDescriptor main_ram_desc{
            REGION_MAIN_RAM, 0x0000, 0x8000, RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
        };
        static const MemoryRegionDescriptor shadow_ram_desc{
            REGION_SHADOW_RAM, 0x3000, 0x5000, RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
        };
        static const MemoryRegionDescriptor private_ram_desc{
            REGION_PRIVATE_RAM, 0x8000, 0x3000, RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
        };
        static const MemoryRegionDescriptor mos_rom_desc{
            REGION_MOS_ROM, 0xC000, 0x4000, RegionFlags::Readable | RegionFlags::Populated
        };
        if (name == REGION_MAIN_RAM) return &main_ram_desc;
        if (name == REGION_SHADOW_RAM) return &shadow_ram_desc;
        if (name == REGION_PRIVATE_RAM) return &private_ram_desc;
        if (name == REGION_MOS_ROM) return &mos_rom_desc;
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
    /// Note: Writing to ROM regions is silently ignored (not an error).
    void write_region(std::string_view name, uint32_t address, uint8_t value) {
        const auto* region = find_region(name);
        if (!region) {
            throw std::invalid_argument("unknown region: '" + std::string(name) + "'");
        }
        validate_region_address(*region, address);
        write_region_unchecked(name, address, value);
    }

private:
    uint8_t romsel_ = 0;
    uint8_t ramsel_ = 0;

    static constexpr std::string_view bank_names_[16] = {
        "bank_0", "bank_1", "bank_2", "bank_3",
        "bank_4", "bank_5", "bank_6", "bank_7",
        "bank_8", "bank_9", "bank_10", "bank_11",
        "bank_12", "bank_13", "bank_14", "bank_15"
    };

    static uint8_t parse_bank_number(std::string_view name) {
        if (name.size() < 6) return 255;
        if (name.size() == 6) {
            char c = name[5];
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        } else if (name.size() == 7) {
            if (name[5] == '1') {
                char c = name[6];
                if (c >= '0' && c <= '5') return static_cast<uint8_t>(10 + (c - '0'));
            }
        }
        return 255;
    }

    // Static descriptors for bank regions (all have same base/size)
    static constexpr auto bank_descriptors_ = make_bank_descriptors(bank_names_);

    bool shadow_selected_for(uint16_t addr) const {
        return addr >= 0x3000 && addr < 0x8000 && shadow_selected();
    }

    void write_romsel(uint8_t value) {
        romsel_ = value & ROMSEL_LATCHED;
        sideways.select_bank(value & ROMSEL_BANK_MASK);
    }

    void reset_board_latches() {
        romsel_ = 0;
        ramsel_ = 0;
        sideways.select_bank(0);
        rtc.reset();
    }

    // Fit the board's own sideways RAM and give the battery-backed memory the
    // contents of a board that has been set up.
    void fit_board_ram() {
        for (uint8_t slot = FIRST_BOARD_RAM_SLOT; slot <= LAST_BOARD_RAM_SLOT; ++slot) {
            sideways.configure_slot_as_ram(slot);
        }
        integra_b_battery_seed::apply(shadow_and_private_ram, rtc);
    }

    // Unchecked accessors - caller must validate region and address first
    uint8_t peek_region_unchecked(std::string_view name, uint32_t address) const {
        if (name == REGION_MAIN_RAM) {
            return main_ram.read(static_cast<uint16_t>(address));
        }
        if (name == REGION_SHADOW_RAM || name == REGION_PRIVATE_RAM) {
            return shadow_and_private_ram.read(static_cast<uint16_t>(address & 0x7FFF));
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
        if (name == REGION_SHADOW_RAM || name == REGION_PRIVATE_RAM) {
            return shadow_and_private_ram.read(static_cast<uint16_t>(address & 0x7FFF));
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
        if (name == REGION_SHADOW_RAM || name == REGION_PRIVATE_RAM) {
            // Private RAM's CPU addresses &8000-&AFFF are chip &0000-&2FFF;
            // shadow RAM's &3000-&7FFF are the same chip addresses.
            shadow_and_private_ram.write(static_cast<uint16_t>(address & 0x7FFF), value);
            return;
        }
        if (name == REGION_MOS_ROM) {
            return;  // ROM is read-only, silently ignore
        }
        uint8_t bank = parse_bank_number(name);
        sideways.write_bank(bank, static_cast<uint16_t>(address - 0x8000), value);
    }

    MemoryMapType memory_map_;
    IrqAggregatorType irq_aggregator_;

    IrqAggregatorType make_irq_aggregator() {
        return beebium::make_irq_aggregator(
            make_irq_binding<0>(system_via),
            make_irq_binding<1>(user_via),
            make_irq_binding<2>(tube_socket),
            make_irq_binding<3>(one_mhz_bus_),
            make_irq_binding<4>(serial_socket),
            make_irq_binding<5>(rtc)
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
            make_region<0xFE30, 0xFE33, Mirror<0x03>>(romsel_register),
            make_region<0xFE34, 0xFE37, Mirror<0x03>>(ramsel_register),
            make_region<0xFE38, 0xFE3B, Mirror<0x03>>(rtc_address_register),
            make_region<0xFE3C, 0xFE3F, Mirror<0x03>>(rtc_data_register),
            make_region<0xFE80, 0xFE9F, Mirror<0x1F>>(disc_socket),
            make_region<0xFEA0, 0xFEBF, Mirror<0x03>>(econet_adlc_region_),       // Econet ADLC
            make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(tube_socket),               // Tube ULA
            make_region<0x0000, 0x7FFF>(main_ram),
            make_region<0x8000, 0xBFFF>(sideways),
            make_region<0xC000, 0xFFFF>(mos_rom)                                   // MOS ROM (occluded by I/O regions)
        };
    }
};

}  // namespace beebium
