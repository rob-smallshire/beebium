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
#include "devices/ConfigurableBankedMemory.hpp"
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
#include "indicators/Indicators.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace beebium {

// BBC Model B with ROM/RAM expansion board providing 16 independent sideways slots.
//
// This configuration emulates a Model B fitted with a third-party ROM/RAM board
// (such as Watford Electronics or similar) that provides:
// - Full 4-bit ROMSEL decoding (16 independent slots, no aliasing)
// - Each slot can be configured as ROM or RAM at runtime
//
// All slots start as Empty (returning 0xFF). Slots are configured via:
// - CLI options: --sideways slot=0:type=ram, --sideways slot=15:type=rom:image=basic.rom
// - API: configure_slot(), load_rom_to_slot()
//
// Unlike stock Model B (which has 4-way aliasing), each slot here is independent.
//
class ModelBRomRamBoardHardware {
public:
    // Machine identification and region names (compile-time constants)
    static constexpr std::string_view MACHINE_TYPE = "model-b-romram";
    constexpr std::string_view machine_type() const { return MACHINE_TYPE; }
    static constexpr std::string_view MACHINE_DISPLAY_NAME = "BBC Model B with ROM/RAM Board";
    static constexpr std::string_view MACHINE_DESCRIPTION = "Model B with expansion board providing 16 independent sideways slots";
    static constexpr std::string_view REGION_MAIN_RAM = "main_ram";
    static constexpr std::string_view REGION_MOS_ROM = "mos_rom";

    // Default ROM filenames for this machine
    static constexpr std::string_view DEFAULT_MOS_ROM = "acorn-mos_1_20.rom";
    static constexpr std::string_view DEFAULT_LANGUAGE_ROM = "bbc-basic_2.rom";
    static constexpr uint8_t DEFAULT_LANGUAGE_SLOT = 15;
    // No DEFAULT_DFS_ROM: a ROM/RAM board is empty expansion sockets with no
    // filing system of its own. A DFS (or any other FS ROM) is provisioned by
    // a preset via its sideways_bank section, not auto-loaded by the machine.

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

    // Sideways ROM/RAM using ConfigurableBankedMemory (16 independent slots)
    // All slots start Empty - configure via CLI or API
    using SidewaysType = ConfigurableBankedMemory;
    SidewaysType sideways{};

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

    // ROMSEL register wrapper
    struct RomselRegister {
        SidewaysType& sideways;

        uint8_t read(uint16_t) { return 0xFF; }  // Write-only register
        void write(uint16_t, uint8_t value) {
            sideways.select_bank(value & 0x0F);
        }
    };

    RomselRegister romsel{sideways};

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
            make_region<0xFE30, 0xFE3F, Mirror<0x0F>>(std::declval<RomselRegister&>()),
            make_region<0xFE80, 0xFE9F, Mirror<0x1F>>(std::declval<DiscControllerSocket&>()),
            make_region<0xFEA0, 0xFEBF, Mirror<0x03>>(std::declval<EconetAdlcRegion&>()),        // Econet ADLC
            make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(std::declval<TubeSocket&>()),              // Tube ULA
            make_region<0x0000, 0x7FFF>(std::declval<Ram<32768>&>()),
            make_region<0x8000, 0xBFFF>(std::declval<SidewaysType&>()),
            make_region<0xC000, 0xFFFF>(std::declval<Rom<16384>&>())       // MOS ROM (occluded by I/O regions)
        }
    );

    // Default constructor
    ModelBRomRamBoardHardware()
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
    }

    // Constructor with custom peripherals (for testing)
    ModelBRomRamBoardHardware(ViaPeripheral& system_peripheral, ViaPeripheral& user_peripheral)
        : system_via(system_peripheral)
        , user_via(user_peripheral)
        , memory_map_(make_memory_map())
        , irq_aggregator_(make_irq_aggregator())
    {
        econet_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        serial_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        tube_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
    }

    ~ModelBRomRamBoardHardware() {
        indicators.stop();
    }

    // MemoryMappedDevice interface
    uint8_t read(uint16_t addr) {
        return memory_map_.read(addr);
    }

    void write(uint16_t addr, uint8_t value) {
        memory_map_.write(addr, value);
    }

    uint8_t peek(uint16_t addr) const {
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

    uint8_t peek_video(uint16_t addr) const {
        return main_ram.read(addr);
    }

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

    void soft_reset() {
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

    // Per-bank write-protect control, surfaced to SidewaysService.
    void set_slot_write_protected(uint8_t slot, bool protect) {
        sideways.set_slot_write_protected(slot, protect);
    }

    bool is_slot_write_protected(uint8_t slot) const {
        return sideways.is_slot_write_protected(slot);
    }

    // The ROM/RAM expansion board has full 4-bit ROMSEL decoding and no
    // motherboard links that affect slot mapping; every slot is independent
    // regardless of any link state.
    using MotherboardLinks = EmptyMotherboardLinks;

    // Topology of the 16 fully-decoded sideways slots provided by the
    // ROM/RAM expansion board.
    //
    // This board is not modelled on a specific real-world product: it is a
    // notional "sufficiently sophisticated" expansion board with full 4-bit
    // ROMSEL decoding (no aliasing) and runtime-configurable slot types.
    // We allow ROM/RAM/Empty changes through the gRPC ConfigureSlot RPC
    // because such a board could plausibly exist with that capability,
    // and it gives users a flexible test bed without forcing a server
    // restart. Future cartridge-slot machines (e.g. Master 128) will share
    // this runtime-configurable behaviour for their cartridge sockets.
    static SlotTopology slot_topology(MotherboardLinks /*links*/ = {}) {
        SlotTopology topo;
        topo.has_aliasing = false;
        for (int slot = 0; slot < 16; ++slot) {
            SocketSpec spec;
            spec.socket_index = slot;
            spec.label = "Slot " + std::to_string(slot);
            spec.slots = {slot};
            // ROM/RAM/empty come from the SocketSpec defaults; the ROM/RAM
            // board uniquely allows reconfiguring a slot's type at runtime.
            spec.runtime_configurable = true;
            // The notional board carries a per-slot write-protect switch on any
            // slot configured as RAM.
            spec.supports_write_protect = true;
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

    // Memory region discovery
    std::vector<MemoryRegionDescriptor> get_memory_regions() const {
        std::vector<MemoryRegionDescriptor> regions;

        regions.push_back({
            REGION_MAIN_RAM,
            0x0000,
            32768,
            RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
        });

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

            regions.push_back({
                bank_names_[bank],
                0x8000,
                16384,
                flags
            });
        }

        return regions;
    }

    /// Find a region descriptor by name.
    /// @returns pointer to descriptor, or nullptr if not found.
    const MemoryRegionDescriptor* find_region(std::string_view name) const {
        // Check static regions first
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
        // Check bank regions
        if (name.size() >= 6 && name.substr(0, 5) == "bank_") {
            uint8_t bank = parse_bank_number(name);
            if (bank < 16) {
                // Return pointer to the appropriate static bank descriptor
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

    // Unchecked accessors - caller must validate region and address first
    uint8_t peek_region_unchecked(std::string_view name, uint32_t address) const {
        if (name == REGION_MAIN_RAM) {
            return main_ram.read(static_cast<uint16_t>(address));
        }
        if (name == REGION_MOS_ROM) {
            return mos_rom.read(static_cast<uint16_t>(address - 0xC000));
        }
        // Must be a bank
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
