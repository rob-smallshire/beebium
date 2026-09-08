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
#include "ClockTypes.hpp"
#include "IrqAggregator.hpp"
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
#include "devices/AliasedBankedMemory.hpp"
#include "devices/Crtc6845.hpp"
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
#include "indicators/IndicatorFilter.hpp"
#include "indicators/Indicators.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace beebium {

// BBC Model B hardware configuration using AliasedBankedMemory for accurate
// ROM socket emulation.
//
// This struct holds all hardware devices and provides:
// - Memory-mapped access via the memory_map
// - Direct access to devices for clocking and configuration
// - Dependency injection for VIA peripherals
//
// Memory Map:
//   0x0000-0x7FFF: 32KB RAM
//   0x8000-0xBFFF: 16KB Paged ROM (4 sockets with 4-way aliasing via ROMSEL at 0xFE30)
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
// Sideways ROM Socket Configuration (4 physical sockets, 4-way aliasing):
//   The Model B has 4 physical ROM sockets with partial address decoding
//   (2 bits), so each socket appears at 4 slot numbers:
//
//   | Socket | IC    | Slots        | Default Content |
//   |--------|-------|--------------|-----------------|
//   |   0    | IC52  | 0, 4, 8, 12  | Empty           |
//   |   1    | IC88  | 1, 5, 9, 13  | DFS ROM         |
//   |   2    | IC100 | 2, 6, 10, 14 | Empty           |
//   |   3    | IC101 | 3, 7, 11, 15 | BASIC ROM       |
//
//   MOS searches for language ROM at slot 15, which due to aliasing accesses
//   socket 3 (IC101) where BASIC is installed.
//
class ModelBHardware {
public:
    // Machine identification and region names (compile-time constants)
    static constexpr std::string_view MACHINE_TYPE = "model-b";
    // Instance accessor so the debugger reaches the machine type through the
    // same call on the host memory map and on a coprocessor's region model.
    constexpr std::string_view machine_type() const { return MACHINE_TYPE; }
    static constexpr std::string_view MACHINE_DISPLAY_NAME = "BBC Model B";
    static constexpr std::string_view MACHINE_DESCRIPTION = "The original BBC Microcomputer with 32KB RAM";
    static constexpr std::string_view REGION_MAIN_RAM = "main_ram";
    static constexpr std::string_view REGION_MOS_ROM = "mos_rom";

    // Default ROM filenames for this machine
    // Note: Model B has no FDC by default, so no default DFS ROM.
    // DFS ROM should be loaded when an FDC is installed via --fdc.
    static constexpr std::string_view DEFAULT_MOS_ROM = "acorn-mos_1_20.rom";
    static constexpr std::string_view DEFAULT_LANGUAGE_ROM = "bbc-basic_2.rom";
    static constexpr uint8_t DEFAULT_LANGUAGE_SLOT = 15;  // Maps to socket 3 (IC101)

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

    // Sideways ROM sockets using AliasedBankedMemory
    // 4 physical sockets with 4-way aliasing (2-bit partial decode)
    using SidewaysType = AliasedBankedMemory;
    SidewaysType sideways;

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
    // Call enable_video_output() to activate
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

    // Disc subsystem - drives owned by hardware, persist across controller changes
    // On real Model B, these drives were external to the optional controller socket
    DiscDrive disc_drive_0{indicators, "floppy-0-activity-led", "FDD 0", "568nm"};
    DiscDrive disc_drive_1{indicators, "floppy-1-activity-led", "FDD 1", "568nm"};

    // Disc controller socket at 0xFE80-0xFE9F
    // On real hardware, this was a physical socket that could be empty or contain
    // various disc controller boards (8271, WD1770 upgrades, Opus, Watford, etc.)
    DiscControllerSocket disc_socket;

    // Registry ID of installed controller (empty if no controller or unknown)
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
    // On real hardware, the Econet NMI enable flip-flop (IC97) shares the same
    // address decode select line as the Video ULA.
    struct VideoUlaWithInton {
        VideoUla& video_ula;
        EconetSocket& econet_socket;
        uint8_t read(uint16_t offset) { econet_socket.on_inton(); return video_ula.read(offset); }
        void write(uint16_t offset, uint8_t value) { econet_socket.on_inton(); video_ula.write(offset, value); }
    };

    EconetStationIdRegion econet_station_id_region_{econet_socket};
    EconetAdlcRegion econet_adlc_region_{econet_socket};
    VideoUlaWithInton video_ula_with_inton_{video_ula, econet_socket};

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

    // ROMSEL register wrapper - handles bank switching and returns 0xFF on read
    struct RomselRegister {
        SidewaysType& sideways;

        uint8_t read(uint16_t) { return 0xFF; }  // Write-only register
        void write(uint16_t, uint8_t value) {
            sideways.select_bank(value & 0x0F);
        }
    };

    RomselRegister romsel{sideways};

    // 1 MHz expansion bus (FRED/JIM, 0xFC00-0xFDFF).
    // Dispatches reads/writes to devices that have claimed address ranges.
    // Unclaimed addresses return 0xFF (74LS245 transceiver behaviour).
    OneMHzBusPort one_mhz_bus_;

    // Memory map type (deduced from make_memory_map)
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
            make_region<0xFE30, 0xFE3F, Mirror<0x0F>>(std::declval<RomselRegister&>()),
            make_region<0xFE80, 0xFE9F, Mirror<0x1F>>(std::declval<DiscControllerSocket&>()),
            make_region<0xFEA0, 0xFEBF, Mirror<0x03>>(std::declval<EconetAdlcRegion&>()),        // Econet ADLC
            make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(std::declval<TubeSocket&>()),              // Tube ULA
            make_region<0x0000, 0x7FFF>(std::declval<Ram<32768>&>()),
            make_region<0x8000, 0xBFFF>(std::declval<SidewaysType&>()),
            make_region<0xC000, 0xFFFF>(std::declval<Rom<16384>&>())       // MOS ROM (occluded by I/O regions)
        }
    );

    // Default constructor - uses internal system_via_peripheral
    ModelBHardware()
        : system_via()
        , user_via()
        , memory_map_(make_memory_map())
        , irq_aggregator_(make_irq_aggregator())
    {
        // Connect internal peripheral to system VIA
        system_via.set_peripheral(&system_via_peripheral);
        // Connect sound chip to system VIA peripheral
        system_via_peripheral.set_sound_chip(&sound_chip);
        // Note: indicators.start() is deferred to the server bootstrap, after
        // extension init() runs. This keeps the registration window open for
        // extensions; closing it before extensions can register would violate
        // the Indicators register-before-start contract.

        // Configure default socket types (stock Model B: all ROM sockets)
        // Socket 0 (IC52): Empty by default
        // Socket 1 (IC88): ROM (DFS)
        // Socket 2 (IC100): Empty by default
        // Socket 3 (IC101): ROM (BASIC)
        sideways.configure_socket(AliasedBankedMemory::SOCKET_IC88, SlotType::Rom);
        sideways.configure_socket(AliasedBankedMemory::SOCKET_IC101, SlotType::Rom);

        // Wire 2MHz open bus regions to memory map's last bus value for open bus emulation
        econet_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        serial_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        tube_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
    }

    // Constructor with custom peripherals (for testing or alternative configurations)
    ModelBHardware(ViaPeripheral& system_peripheral, ViaPeripheral& user_peripheral)
        : system_via(system_peripheral)
        , user_via(user_peripheral)
        , memory_map_(make_memory_map())
        , irq_aggregator_(make_irq_aggregator())
    {
        // Configure default socket types
        sideways.configure_socket(AliasedBankedMemory::SOCKET_IC88, SlotType::Rom);
        sideways.configure_socket(AliasedBankedMemory::SOCKET_IC101, SlotType::Rom);

        // Wire 2MHz open bus regions to memory map's last bus value for open bus emulation
        econet_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        serial_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        tube_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
    }

    // Destructor - stops indicator consumer thread
    ~ModelBHardware() {
        indicators.stop();
    }

    // MemoryMappedDevice interface (delegates to memory_map)
    uint8_t read(uint16_t addr) {
        return memory_map_.read(addr);
    }

    void write(uint16_t addr, uint8_t value) {
        memory_map_.write(addr, value);
    }

    // Side-effect-free read for debugger inspection.
    // Uses peek() for VIAs and Tube to avoid clearing interrupt flags,
    // dequeuing FIFOs, or other side effects.
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
        // All other regions have no read side effects
        return memory_map_.read(addr);
    }

    // Video memory access - reads from currently configured video RAM.
    // For Model B, this is always main RAM (no shadow RAM).
    // Used by VideoBinding for screen rendering.
    uint8_t peek_video(uint16_t addr) const {
        return main_ram.read(addr);
    }

    // Power-on reset: clear RAM and reset all devices including System VIA
    // This is the original reset() behavior, equivalent to powering off and on.
    void reset() {
        main_ram.clear();
        system_via.reset();
        user_via.reset();
        crtc.reset();
        video_ula.reset();
        saa5050.reset();
        sound_chip.reset();
        addressable_latch.reset();
        sideways.select_bank(0);
        disc_socket.reset();
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
        disc_socket.reset();
        econet_socket.reset();
        serial_socket.reset();
        tube_socket.reset();
        // Do NOT clear RAM
        // Do NOT reset sideways bank selection
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
    // Returns IRQ mask (bit 0 = system VIA, bit 1 = user VIA)
    uint8_t poll_irq() {
        return irq_aggregator_.poll();
    }

    // Poll NMI status from disc controller (called from Machine::step after clock tick)
    // Returns non-zero if NMI is pending from disc controller.
    // Model B has an optional disc controller add-on via the disc socket.
    uint8_t poll_nmi() {
        // Sample NMI state BEFORE ticking the disc controller
        // This is critical for edge detection to prevent NMI stacking
        uint8_t nmi = disc_socket.nmi_pending() ? 0x01 : 0x00;

        // Tick the disc controller (1MHz peripheral clock)
        disc_socket.tick();

        // Tick 1 MHz bus devices
        one_mhz_bus_.tick();

        return nmi;
    }

    OneMHzBusPort& one_mhz_bus() { return one_mhz_bus_; }

    UserPort& user_port() { return user_port_; }
    SerialPort& serial_port() { return serial_port_; }

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
    // Call before reset() to boot into specified mode
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
    // Disc Controller Management
    // =========================================================================

    // Install a disc controller into the socket
    // After installation, drives are automatically attached
    // @param controller Controller to install (takes ownership)
    // @param controller_id Registry ID of the controller (for querying later)
    void install_disc_controller(std::unique_ptr<DiscControllerInterface> controller,
                                 std::string_view controller_id = "") {
        disc_socket.install(std::move(controller));
        disc_socket.attach_drive(0, &disc_drive_0);
        disc_socket.attach_drive(1, &disc_drive_1);
        installed_controller_id_ = controller_id;
    }

    // Convenience method: install Acorn 1770 DFS controller
    // This is the most common disc controller upgrade for Model B
    void install_acorn_1770() {
        install_disc_controller(std::make_unique<Acorn1770DiscController>(), "acorn-1770");
    }

    // Remove installed disc controller
    // @return Previously installed controller, or nullptr if socket was empty
    std::unique_ptr<DiscControllerInterface> remove_disc_controller() {
        installed_controller_id_.clear();
        return disc_socket.remove();
    }

    // Check if a disc controller is installed
    bool has_disc_controller() const {
        return disc_socket.has_controller();
    }

    // Get registry ID of installed controller
    // @return Controller ID (e.g., "acorn-1770"), or empty if none installed
    std::string_view installed_controller_id() const {
        return installed_controller_id_;
    }

    // =========================================================================
    // Disc Controller Configuration (for DiscService compatibility)
    // =========================================================================

    // Enable/disable motor spin-up delay
    // @param enabled true to enable realistic delays, false to skip (fast)
    void set_spin_up_delay_enabled(bool enabled) {
        if (auto* ctrl = disc_socket.controller()) {
            ctrl->set_spin_up_delay_enabled(enabled);
        }
    }

    // Check if spin-up delay is enabled
    // @return true if enabled, false if disabled or no controller
    bool spin_up_delay_enabled() const {
        if (auto* ctrl = disc_socket.controller()) {
            return ctrl->spin_up_delay_enabled();
        }
        return false;
    }

    // =========================================================================
    // ROM Loading (socket-based for Model B)
    // =========================================================================

    // Load MOS ROM
    void load_mos(const uint8_t* data, size_t size) {
        mos_rom.load(data, size);
    }

    // Load BASIC ROM into socket 3 (IC101, visible at slots 3/7/11/15)
    void load_basic(const uint8_t* data, size_t size) {
        sideways.configure_socket(AliasedBankedMemory::SOCKET_IC101, SlotType::Rom);
        sideways.load_rom_to_socket(AliasedBankedMemory::SOCKET_IC101, data, size);
        sideways.set_socket_image_name(AliasedBankedMemory::SOCKET_IC101, DEFAULT_LANGUAGE_ROM);
    }

    // Load DFS ROM into socket 1 (IC88, visible at slots 1/5/9/13)
    void load_dfs(const uint8_t* data, size_t size, std::string_view image_name = "") {
        sideways.configure_socket(AliasedBankedMemory::SOCKET_IC88, SlotType::Rom);
        sideways.load_rom_to_socket(AliasedBankedMemory::SOCKET_IC88, data, size);
        if (!image_name.empty()) {
            sideways.set_socket_image_name(AliasedBankedMemory::SOCKET_IC88, image_name);
        }
    }

    // Load ROM into a specific socket (0-3)
    // Socket mapping: 0=IC52, 1=IC88, 2=IC100, 3=IC101
    void load_rom_to_socket(uint8_t socket_idx, const uint8_t* data, size_t size,
                           std::string_view image_name = "") {
        sideways.configure_socket(socket_idx, SlotType::Rom);
        sideways.load_rom_to_socket(socket_idx, data, size);
        if (!image_name.empty()) {
            sideways.set_socket_image_name(socket_idx, image_name);
        }
    }

    // Configure a socket's type (Empty, Rom, or Ram)
    // Note: This uses socket index (0-3), not slot number (0-15)
    void configure_socket(uint8_t socket_idx, SlotType type) {
        sideways.configure_socket(socket_idx, type);
    }

    // Configure a socket by slot number (aliasing handled automatically)
    // Multiple slots map to the same socket due to partial address decoding:
    //   Slots 0,4,8,12 -> Socket 0 (IC52)
    //   Slots 1,5,9,13 -> Socket 1 (IC88)
    //   Slots 2,6,10,14 -> Socket 2 (IC100)
    //   Slots 3,7,11,15 -> Socket 3 (IC101)
    void configure_slot(uint8_t slot, SlotType type) {
        sideways.configure_slot(slot, type);
    }

    // Load ROM by slot number (aliasing handled automatically)
    void load_rom(uint8_t slot, const uint8_t* data, size_t size) {
        uint8_t socket_idx = AliasedBankedMemory::slot_to_socket(slot);
        sideways.configure_socket(socket_idx, SlotType::Rom);
        sideways.load_rom(slot, data, size);
    }

    // =========================================================================
    // Unified ROM Loading API (forwards to sideways member)
    // =========================================================================

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
        return AliasedBankedMemory::is_slot_loadable(slot);
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

    // The Model B has no motherboard links that affect sideways slot mapping;
    // the four sockets are wired the same way on every machine.
    using MotherboardLinks = EmptyMotherboardLinks;

    // Topology of the four sideways ROM sockets, including the 4-way aliasing
    // produced by partial ROMSEL decoding. Used by configuration validation
    // and the gRPC SidewaysService to describe the hardware layout to
    // clients.
    //
    // Model B sockets are not runtime-reconfigurable: on real hardware,
    // changing what is in a socket means powering off, removing the EPROM,
    // and inserting a different chip (or sideways RAM module). Initial
    // socket type and ROM image are configured at startup via --sideways;
    // the gRPC ConfigureSlot RPC will reject runtime change requests on
    // these sockets. The fantasy ROM/RAM expansion board variant
    // (model-b-romram) and future cartridge-slot machines (e.g. Master 128)
    // are where runtime reconfiguration belongs.
    static SlotTopology slot_topology(MotherboardLinks /*links*/ = {}) {
        SlotTopology topo;
        topo.has_aliasing = true;
        for (uint8_t socket_idx = 0; socket_idx < AliasedBankedMemory::num_sockets;
             ++socket_idx) {
            SocketSpec spec;
            spec.socket_index = socket_idx;
            spec.label = std::string(AliasedBankedMemory::socket_names[socket_idx]);
            for (uint8_t slot : AliasedBankedMemory::socket_aliased_slots(socket_idx)) {
                spec.slots.push_back(slot);
            }
            // ROM/RAM/empty come from the SocketSpec defaults; the Model B has
            // no runtime slot reconfiguration (changing a socket is power-off).
            topo.sockets.push_back(std::move(spec));
        }
        return topo;
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

    // =========================================================================
    // Memory Region Discovery (for debugger)
    // =========================================================================

    std::vector<MemoryRegionDescriptor> get_memory_regions() const {
        std::vector<MemoryRegionDescriptor> regions;

        // Main RAM (0x0000-0x7FFF)
        regions.push_back({
            REGION_MAIN_RAM,
            0x0000,  // base_address
            32768,
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
        // Note: Due to aliasing, banks 0/4/8/12, 1/5/9/13, 2/6/10/14, 3/7/11/15
        // each share the same physical socket.
        for (uint8_t bank = 0; bank < 16; ++bank) {
            RegionFlags flags = RegionFlags::Readable;

            // Check if slot is RAM (writable)
            if (sideways.bank_type(bank) == SlotType::Ram) {
                flags = flags | RegionFlags::Writable;
            }

            if (sideways.is_bank_populated(bank)) {
                flags = flags | RegionFlags::Populated;
            }
            if (bank == sideways.selected_bank()) {
                flags = flags | RegionFlags::Active;
            }
            // Bank names are generated as "bank_0", "bank_1", etc.
            // We use a static array for the string_views
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
            static const MemoryRegionDescriptor main_ram_desc{
                REGION_MAIN_RAM, 0x0000, 0x8000, RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
            };
            return &main_ram_desc;
        }
        if (name == REGION_MOS_ROM) {
            static const MemoryRegionDescriptor mos_rom_desc{
                REGION_MOS_ROM, 0xC000, 0x4000, RegionFlags::Readable | RegionFlags::Populated
            };
            return &mos_rom_desc;
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
            make_region<0xFE30, 0xFE3F, Mirror<0x0F>>(romsel),
            make_region<0xFE80, 0xFE9F, Mirror<0x1F>>(disc_socket),
            make_region<0xFEA0, 0xFEBF, Mirror<0x03>>(econet_adlc_region_),       // Econet ADLC
            make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(tube_socket),               // Tube ULA
            make_region<0x0000, 0x7FFF>(main_ram),
            make_region<0x8000, 0xBFFF>(sideways),
            make_region<0xC000, 0xFFFF>(mos_rom)                                   // MOS ROM (occluded by I/O regions)
        };
    }
};

} // namespace beebium
