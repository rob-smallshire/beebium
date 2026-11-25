// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include "beebium/MachineModels.hpp"
#include "extension/OneMHzBusPort.hpp"
#include "extension/PrinterPort.hpp"
#include "extension/UserPort.hpp"
#include "extension/SerialPort.hpp"
#include "AddressableLatch.hpp"
#include "AudioBuffer.hpp"
#include "AudioFrameAssembler.hpp"
#include "BatteryBackedUnit.hpp"
#include "ClockTypes.hpp"
#include "IrqAggregator.hpp"
#include "Master128CmosSeed.hpp"
#include "MasterSystemViaPeripheral.hpp"
#include "PacingConfig.hpp"
#include "MemoryMap.hpp"
#include "DebugVariable.hpp"
#include "MemoryRegion.hpp"
#include "MotherboardLinks.hpp"
#include "OutputQueue.hpp"
#include "Saa5050.hpp"
#include "ScreenMemory.hpp"
#include "SlotTopology.hpp"
#include "UserViaPeripheral.hpp"
#include "Via6522.hpp"
#include "PixelBatch.hpp"
#include "devices/ConfigurableBankedMemory.hpp"
#include "devices/Crtc6845.hpp"
#include "devices/Mc146818BatteryUnit.hpp"
#include "devices/Mc146818Rtc.hpp"
#include "devices/Ram.hpp"
#include "devices/Rom.hpp"
#include "devices/Sn76489.hpp"
#include "devices/Upd7002.hpp"
#include "extension/AnaloguePort.hpp"
#include "extension/CartridgeSlot.hpp"
#include "ClockBinding.hpp"
#include "devices/VideoUla.hpp"
#include "disc/Acorn1770DiscController.hpp"
#include "disc/DiscDrive.hpp"
#include "econet/EconetSocket.hpp"
#include "cassette/CassetteDeck.hpp"
#include "serial/SerialSocket.hpp"
#include "tube/TubeSocket.hpp"
#include "indicators/Indicators.hpp"
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// A sideways bank a machine fills by default, with the ROM image it takes.
struct DefaultSidewaysRom {
    uint8_t slot;
    std::string_view image_filename;
};

// The Master's MOS ROM is one 128K chip: the MOS half and sideways banks 9 to
// 15, which travel together. A set is named by its MOS image's bare name;
// bank 12 is the language ROM, the other six banks are sideways_roms.
struct MosRomSet {
    std::string_view mos_rom_name;   // bare name of the MOS half, without ".rom"
    std::string_view language_rom;   // bank 12
    std::array<DefaultSidewaysRom, 6> sideways_roms;  // banks 9-11, 13-15
};

// The BBC Master 128 (docs/discussion/master-128-research.md; the plan is
// docs/plans/master-128-round-1.md).
//
// Memory (section 1.1):
//   &0000-&7FFF  32K main RAM; LYNNE, 20K, at &3000-&7FFF by ACCCON X and E
//   &8000-&BFFF  the sideways bank ROMSEL selects; ANDY, 4K, over
//                &8000-&8FFF while ROMSEL bit 7 is set
//   &C000-&FFFF  the MOS; HAZEL, 8K, over &C000-&DFFF while ACCCON Y is set
//   Sideways banks 4-7 are RAM; 9-15 are the 128K MOS ROM's other seven
//   parts, locked; 8 is the IC27 socket; 0-3 the two cartridge slots.
//
// The two paging registers are readable:
//   ROMSEL &FE30-&FE33  bits 0-3 bank, bit 7 ANDY
//   ACCCON &FE34-&FE37  IRR TST IFJ ITU Y X E D (bit 7 to bit 0)
//
// SHEILA (section 1.2): the ADC at &FE18, the Video ULA at &FE20-&FE23 only,
// the 1770's drive control latch at &FE24-&FE27 (write only: a read is the
// stale data bus) and its registers at &FE28-&FE2B, ROMSEL and ACCCON,
// Econet's INTOFF at &FE38 and INTON at &FE3C, the VIAs, the ADLC at
// &FEA0, the Tube at &FEE0. &FE2C-&FE2F (undecoded, as B-Em has it;
// unmeasured, research section 5 item 3), the modem connector (&FE80-&FE9F)
// and &FEC0-&FEDF read as the stale data bus until something is decoded
// there. The modem connector (PL12, the attachment point "modem-connector")
// is selected by nMODEM at &FE80-&FE9F, at 1 MHz; it carries A0-A3 only, so
// its 32 bytes are 16 registers mirrored once. Nothing is fitted to it yet.
//
// Two Tubes (section 1.6): the external Tube (PL6, the attachment point
// "tube") and the internal one (SK1/SK2, "internal-tube"). There is one
// register block at &FEE0-&FEFF: ACCCON ITU steers it, and the host-side Tube
// IRQ, to the internal Tube while set and the external one while clear. A
// coprocessor on the other one is not disabled: it runs on, unreached.
//
// Econet's station number is CMOS byte 0, not a register: the filing system
// reads it there as it starts, so --station sets that byte and the socket
// takes its number from it at every reset.
//
// The clock (section 1.4): an MC146818 on the System VIA (see
// MasterSystemViaPeripheral), its CMOS kept by its battery as the unit
// "master-128-cmos". Its IRQ output reaches the CPU only through link LK4,
// unmade as shipped, so it is not wired.
class Master128Hardware {
public:
    // Machine identification and region names (compile-time constants)
    static constexpr std::string_view MACHINE_TYPE = machine_models::kMaster128.id;
    constexpr std::string_view machine_type() const { return MACHINE_TYPE; }
    static constexpr std::string_view MACHINE_DISPLAY_NAME = machine_models::kMaster128.display_name;
    static constexpr std::string_view MACHINE_DESCRIPTION =
        "BBC Master 128: MOS 3.20, 128K RAM (LYNNE, HAZEL, ANDY, sideways RAM banks 4-7), "
        "real-time clock and CMOS RAM";
    static constexpr std::string_view REGION_MAIN_RAM = "main_ram";
    static constexpr std::string_view REGION_LYNNE = "lynne";
    static constexpr std::string_view REGION_HAZEL = "hazel";
    static constexpr std::string_view REGION_ANDY = "andy";
    static constexpr std::string_view REGION_MOS_ROM = "mos_rom";

    // The MOS ROM sets the server knows, each a 128K chip's sixteen-kilobyte
    // parts: the MOS half is the MOS image, the other seven banks 9 to 15. A
    // MOS that matches no set (a path, or a MOS half of another origin) gets
    // the MOS 3.20 set's banks, which is what a real board has when someone
    // fits a foreign MOS half.
    static constexpr std::array<MosRomSet, 2> MOS_ROM_SETS{{
        {"acorn-mos_3_20", "bbc-basic_4.rom", {{
            {9, "acorn-dfs_2_24.rom"},
            {10, "acornsoft-viewsheet_b1_0.rom"},
            {11, "acorn-edit_1_00.rom"},
            {13, "acorn-adfs_1_50.rom"},
            {14, "acornsoft-view_b3_0.rom"},
            {15, "acorn-terminal_1_20_mos_3_20.rom"},
        }}},
        {"acorn-mos_3_50", "bbc-basic_4r32.rom", {{
            {9, "acorn-dfs_2_45.rom"},
            {10, "acornsoft-viewsheet_b1_01.rom"},
            {11, "acorn-edit_1_50r.rom"},
            {13, "acorn-adfs_2_03.rom"},
            {14, "acornsoft-view_b3_3.rom"},
            {15, "acorn-terminal_1_20_mos_3_50.rom"},
        }}},
    }};
    static constexpr const MosRomSet& DEFAULT_MOS_ROM_SET = MOS_ROM_SETS[0];

    // The banks the MOS ROM fills: 9 to 15.
    static constexpr uint8_t FIRST_MOS_ROM_SLOT = 9;
    static constexpr uint8_t LAST_MOS_ROM_SLOT = 15;

    // The set whose MOS half is `mos_rom_name` (a bare name without ".rom"),
    // or nullptr for one the server does not know.
    static constexpr const MosRomSet* find_mos_rom_set(std::string_view mos_rom_name) {
        for (const auto& set : MOS_ROM_SETS) {
            if (set.mos_rom_name == mos_rom_name) return &set;
        }
        return nullptr;
    }

    // The set one of whose banks 9 to 15 is `image_filename`, or nullptr.
    static constexpr const MosRomSet* mos_rom_set_holding(std::string_view image_filename) {
        for (const auto& set : MOS_ROM_SETS) {
            if (set.language_rom == image_filename) return &set;
            for (const auto& rom : set.sideways_roms) {
                if (rom.image_filename == image_filename) return &set;
            }
        }
        return nullptr;
    }

    // Default ROMs: the MOS 3.20 set's.
    static constexpr std::string_view DEFAULT_MOS_ROM = "acorn-mos_3_20.rom";
    static constexpr std::string_view DEFAULT_LANGUAGE_ROM = DEFAULT_MOS_ROM_SET.language_rom;
    static constexpr uint8_t DEFAULT_LANGUAGE_SLOT = 12;
    static constexpr std::array<DefaultSidewaysRom, 6> DEFAULT_SIDEWAYS_ROMS =
        DEFAULT_MOS_ROM_SET.sideways_roms;

    // The modem connector (PL12) is an attachment point of this machine.
    static constexpr bool HAS_MODEM_CONNECTOR = true;

    // Launch option for the clock: --rtc clock=<host|emulated>[:time=...|:offset=...].
    static constexpr std::string_view BOARD_RTC_OPTION = "rtc";

    // Sideways banks by what fills them.
    static constexpr uint8_t FIRST_SIDEWAYS_RAM_SLOT = 4;
    static constexpr uint8_t LAST_SIDEWAYS_RAM_SLOT = 7;
    static constexpr uint8_t IC27_SLOT = 8;

    // ROMSEL bits
    static constexpr uint8_t ROMSEL_BANK_MASK = 0x0F;
    static constexpr uint8_t ROMSEL_ANDY = 0x80;

    // ACCCON bits
    static constexpr uint8_t ACCCON_IRR = 0x80;
    static constexpr uint8_t ACCCON_TST = 0x40;
    static constexpr uint8_t ACCCON_IFJ = 0x20;
    static constexpr uint8_t ACCCON_ITU = 0x10;
    static constexpr uint8_t ACCCON_Y = 0x08;
    static constexpr uint8_t ACCCON_X = 0x04;
    static constexpr uint8_t ACCCON_E = 0x02;
    static constexpr uint8_t ACCCON_D = 0x01;

    // Whether ACCCON Y suppresses E for code in &C000-&DFFF, as B2, beebjit
    // and jsbeeb have it ((Y and X) or (not Y and E)). Acorn describes E
    // without Y, and B-Em, BeebEm and MAME ignore Y; unmeasured (research
    // section 5 item 1). It matters only to code running out of HAZEL.
    static constexpr bool Y_SUPPRESSES_E = false;

    static constexpr PacingConfig default_pacing_config() {
        return {
            .base_clock_hz = timing::CPU_HZ,  // 2 MHz
            .pacing_hz = 200,
            .speed_multiplier = 1.0
        };
    }

    // Memory
    Ram<32768> main_ram;
    Ram<20480> lynne;      // &3000-&7FFF
    Ram<8192> hazel;       // &C000-&DFFF
    Ram<4096> andy;        // &8000-&8FFF
    Rom<16384> mos_rom;

    using SidewaysType = ConfigurableBankedMemory;
    SidewaysType sideways{};

    // The MC146818 real-time clock with its CMOS RAM.
    Mc146818Rtc rtc;

    Via6522 system_via;
    Via6522 user_via;

    // The host-side IRQ of the Tube ITU selects.
    struct SelectedTubeIrq {
        const Master128Hardware& hw;
        bool irq_pending() const { return hw.selected_tube().irq_pending(); }
    };

    using IrqAggregatorType = IrqAggregator<
        IrqBinding<Via6522, 0>,        // System VIA -> bit 0
        IrqBinding<Via6522, 1>,        // User VIA -> bit 1
        IrqBinding<SelectedTubeIrq, 2>,  // the Tube ITU selects: HIRQ -> bit 2
        IrqBinding<OneMHzBusPort, 3>,  // 1 MHz bus devices -> bit 3
        IrqBinding<SerialSocket, 4>    // Serial ACIA (MC6850) -> bit 4
    >;
    // ACCCON IRR's open-drain output, a level on nIRQ.
    static constexpr uint8_t IRR_IRQ_BIT = 0x20;

    // Video hardware
    Crtc6845 crtc;
    VideoUla video_ula;
    Saa5050 saa5050;
    std::optional<OutputQueue<PixelBatch>> video_output;

    // Sound hardware
    Sn76489 sound_chip{4'000'000, 48'000};
    std::optional<AudioBuffer> audio_buffer;
    AudioSourcePort audio_sources;
    AudioFrameAssembler audio_frames{audio_sources};

    // Hardware indicators - must be declared before the System VIA peripheral
    Indicators indicators;

    // System VIA peripherals: the keyboard, sound and latch, and the clock.
    AddressableLatch addressable_latch;
    MasterSystemViaPeripheral system_via_peripheral{addressable_latch, indicators, rtc};

    // NEC uPD7002 at &FE18-&FE1F. Its EOC pin drives System VIA CB1. It has
    // no reset pin, so Break leaves it alone.
    Upd7002 analogue_converter_;
    AnaloguePort analogue_port_{system_via_peripheral, analogue_converter_};

    UserViaPeripheral user_via_peripheral;
    UserPort user_port_{user_via_peripheral};
    PrinterPort printer_port_{user_via_peripheral};

    // No CassetteMotorLed: the Master's keyboard has no cassette motor LED
    // (#311). The Serial ULA still drives the motor relay.

    // The 1770 on the motherboard, with the Master's latch layout, and its
    // two drives. DS2, the latch's third select, has no drive.
    Acorn1770DiscController disc_controller{Acorn1770DiscController::LatchLayout::Master};
    DiscDrive disc_drive_0{indicators, "floppy-0-activity-led", "FDD 0", "568nm"};
    DiscDrive disc_drive_1{indicators, "floppy-1-activity-led", "FDD 1", "568nm"};

    EconetSocket econet_socket;
    SerialSocket serial_socket;

    static constexpr bool has_cassette = true;
    CassetteDeck cassette_deck;

    SerialPort serial_port_{serial_socket};

    // The external Tube (PL6) and the internal one (SK1/SK2).
    TubeSocket tube_socket;
    TubeSocket internal_tube_socket;

    struct EconetAdlcRegion {
        EconetSocket& econet_socket;
        uint8_t read(uint16_t offset) { return econet_socket.read_adlc(offset); }
        void write(uint16_t offset, uint8_t value) { econet_socket.write_adlc(offset, value); }
    };

    struct SerialAciaRegion {
        SerialSocket& serial_socket;
        uint8_t read(uint16_t offset) { return serial_socket.read_acia(offset); }
        uint8_t peek(uint16_t offset) const { return serial_socket.peek_acia(offset); }
        void write(uint16_t offset, uint8_t value) { serial_socket.write_acia(offset, value); }
    };

    struct SerialUlaRegion {
        SerialSocket& serial_socket;
        uint8_t read(uint16_t offset) { return serial_socket.read_ula(offset); }
        uint8_t peek(uint16_t offset) const { return serial_socket.peek_ula(offset); }
        void write(uint16_t offset, uint8_t value) { serial_socket.write_ula(offset, value); }
    };

    // An address nothing answers: a read returns the stale data bus, the byte
    // the video system last fetched (&20 in Mode 7, &00 in Mode 0 on a real
    // Master, research section 1.2); a write goes nowhere.
    struct StaleBusRegion {
        const uint8_t& stale;
        uint8_t read(uint16_t) const { return stale; }
        uint8_t peek(uint16_t) const { return stale; }
        void write(uint16_t, uint8_t) {}
    };

    // The 1770's drive control latch (&FE24-&FE27): write only, so a read
    // returns the stale data bus.
    struct DiscLatchRegion {
        Acorn1770DiscController& controller;
        const uint8_t& stale;
        uint8_t read(uint16_t) const { return stale; }
        uint8_t peek(uint16_t) const { return stale; }
        void write(uint16_t, uint8_t value) { controller.write_latch(value); }
    };

    // The WD1770's four registers (&FE28-&FE2B).
    struct DiscRegisterRegion {
        Acorn1770DiscController& controller;
        uint8_t read(uint16_t offset) { return controller.read_register(static_cast<uint8_t>(offset)); }
        uint8_t peek(uint16_t offset) const {
            return controller.peek_register(static_cast<uint8_t>(offset));
        }
        void write(uint16_t offset, uint8_t value) {
            controller.write_register(static_cast<uint8_t>(offset), value);
        }
    };

    // Econet's NMI mask in the I/O controller: any access to &FE38-&FE3B is
    // INTOFF, to &FE3C-&FE3F INTON. Neither is readable; a read is the stale
    // data bus.
    struct EconetIntoffRegion {
        EconetSocket& econet_socket;
        const uint8_t& stale;
        uint8_t read(uint16_t) { econet_socket.on_intoff(); return stale; }
        uint8_t peek(uint16_t) const { return stale; }
        void write(uint16_t, uint8_t) { econet_socket.on_intoff(); }
    };
    struct EconetIntonRegion {
        EconetSocket& econet_socket;
        const uint8_t& stale;
        uint8_t read(uint16_t) { econet_socket.on_inton(); return stale; }
        uint8_t peek(uint16_t) const { return stale; }
        void write(uint16_t, uint8_t) { econet_socket.on_inton(); }
    };

    // &FEE0-&FEFF: the registers of the Tube ITU selects.
    struct SelectedTubeRegion {
        Master128Hardware& hw;
        uint8_t read(uint16_t offset) { return hw.selected_tube().read(offset); }
        uint8_t peek(uint16_t offset) const { return hw.selected_tube().peek(offset); }
        void write(uint16_t offset, uint8_t value) { hw.selected_tube().write(offset, value); }
    };

    // A cartridge slot: two sideways banks, chosen between by ROMSEL bit 0
    // (ROMQA), which the cartridge fitted to it provides (CartridgeSlot.hpp).
    class MasterCartridgeSlot final : public CartridgeSlot {
    public:
        MasterCartridgeSlot(Master128Hardware& hw, uint8_t first_bank)
            : hw_(hw), first_bank_(first_bank) {}
        uint8_t first_bank() const override { return first_bank_; }
        void fit(SidewaysBankProvider& cartridge) override {
            hw_.sideways.attach_provider(first_bank_, &cartridge, 0);
            hw_.sideways.attach_provider(static_cast<uint8_t>(first_bank_ + 1), &cartridge, 1);
            fitted_ = true;
        }
        void remove() override {
            hw_.sideways.attach_provider(first_bank_, nullptr);
            hw_.sideways.attach_provider(static_cast<uint8_t>(first_bank_ + 1), nullptr);
            fitted_ = false;
        }
        bool fitted() const { return fitted_; }

    private:
        Master128Hardware& hw_;
        uint8_t first_bank_;
        bool fitted_ = false;
    };

    MasterCartridgeSlot cartridge_rear{*this, 0};
    MasterCartridgeSlot cartridge_front{*this, 2};

    // The slot that answers on `bank`, or nullptr for a bank outside 0-3.
    CartridgeSlot* cartridge_slot_for_bank(uint8_t bank) {
        if (bank < 2) return &cartridge_rear;
        if (bank < 4) return &cartridge_front;
        return nullptr;
    }

    // Why SidewaysService may not change `bank` at run time, or nullopt:
    // banks 0-3 are the cartridge slots', which a cartridge fills.
    std::optional<std::string> slot_change_refusal(uint8_t bank) const {
        if (bank >= 4) return std::nullopt;
        const bool rear = bank < 2;
        return "Bank " + std::to_string(bank) + " is the " +
               std::string(rear ? "rear" : "front") +
               " cartridge slot's, which a cartridge fills; fit one at the " +
               std::string(rear ? "Rear" : "Front") +
               " Cartridge Slot under Peripherals before the machine starts, not while it "
               "runs.";
    }

    // FRED and JIM (&FC00-&FDFF): the external 1 MHz bus while ACCCON IFJ is
    // clear; while it is set, the cartridge bus at 2 MHz, which has nothing
    // decoding FRED or JIM yet, so a read is the stale data bus and the
    // external bus is not reached.
    struct FredJimRegion {
        Master128Hardware& hw;
        uint8_t read(uint16_t offset) {
            if (hw.acccon_ & ACCCON_IFJ) return hw.stale_bus_;
            return hw.one_mhz_bus_.read(offset);
        }
        void write(uint16_t offset, uint8_t value) {
            if (hw.acccon_ & ACCCON_IFJ) return;
            hw.one_mhz_bus_.write(offset, value);
        }
    };

    // ROMSEL (&FE30-&FE33). Bits 4-6 read back as written: no hardware
    // measurement of ROMSEL readback exists (research section 5 item 2).
    struct RomselRegister {
        Master128Hardware& hw;
        uint8_t read(uint16_t) const { return hw.romsel_; }
        uint8_t peek(uint16_t) const { return hw.romsel_; }
        void write(uint16_t, uint8_t value) { hw.write_romsel(value); }
    };

    // ACCCON (&FE34-&FE37): all eight bits stored and readable.
    struct AccconRegister {
        Master128Hardware& hw;
        uint8_t read(uint16_t) const { return hw.acccon_; }
        uint8_t peek(uint16_t) const { return hw.acccon_; }
        void write(uint16_t, uint8_t value) { hw.acccon_ = value; }
    };

    SerialAciaRegion serial_acia_region_{serial_socket};
    SerialUlaRegion serial_ula_region_{serial_socket};
    EconetAdlcRegion econet_adlc_region_{econet_socket};
    StaleBusRegion stale_bus_region_{stale_bus_};
    DiscLatchRegion disc_latch_region_{disc_controller, stale_bus_};
    DiscRegisterRegion disc_register_region_{disc_controller};
    EconetIntoffRegion econet_intoff_region_{econet_socket, stale_bus_};
    EconetIntonRegion econet_inton_region_{econet_socket, stale_bus_};
    SelectedTubeRegion selected_tube_region_{*this};
    FredJimRegion fred_jim_region_{*this};
    SelectedTubeIrq selected_tube_irq_{*this};
    RomselRegister romsel_register{*this};
    AccconRegister acccon_register{*this};

    // The external 1 MHz bus (FRED/JIM, &FC00-&FDFF).
    OneMHzBusPort one_mhz_bus_;

    using MemoryMapType = decltype(
        MemoryMap{
            make_region<0xFC00, 0xFDFF>(std::declval<FredJimRegion&>()),
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(std::declval<Crtc6845&>()),
            make_region<0xFE08, 0xFE0F, Mirror<0x01>>(std::declval<SerialAciaRegion&>()),
            make_region<0xFE10, 0xFE17, Mirror<0x07>>(std::declval<SerialUlaRegion&>()),
            make_region<0xFE18, 0xFE1F, Mirror<0x03>>(std::declval<Upd7002&>()),
            make_region<0xFE20, 0xFE23, Mirror<0x01>>(std::declval<VideoUla&>()),
            make_region<0xFE24, 0xFE27>(std::declval<DiscLatchRegion&>()),
            make_region<0xFE28, 0xFE2B, Mirror<0x03>>(std::declval<DiscRegisterRegion&>()),
            make_region<0xFE2C, 0xFE2F>(std::declval<StaleBusRegion&>()),
            make_region<0xFE30, 0xFE33, Mirror<0x03>>(std::declval<RomselRegister&>()),
            make_region<0xFE34, 0xFE37, Mirror<0x03>>(std::declval<AccconRegister&>()),
            make_region<0xFE38, 0xFE3B>(std::declval<EconetIntoffRegion&>()),
            make_region<0xFE3C, 0xFE3F>(std::declval<EconetIntonRegion&>()),
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE80, 0xFE9F, Mirror<0x0F>>(std::declval<StaleBusRegion&>()),
            make_region<0xFEA0, 0xFEBF, Mirror<0x03>>(std::declval<EconetAdlcRegion&>()),
            make_region<0xFEC0, 0xFEDF>(std::declval<StaleBusRegion&>()),
            make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(std::declval<SelectedTubeRegion&>()),
            make_region<0x0000, 0x7FFF>(std::declval<Ram<32768>&>()),
            make_region<0x8000, 0xBFFF>(std::declval<SidewaysType&>()),
            make_region<0xC000, 0xFFFF>(std::declval<Rom<16384>&>())
        }
    );

    Master128Hardware()
        : system_via()
        , user_via()
        , memory_map_(make_memory_map())
        , irq_aggregator_(make_irq_aggregator())
    {
        system_via.set_peripheral(&system_via_peripheral);
        system_via_peripheral.set_analogue_converter(&analogue_converter_);
        user_via.set_peripheral(&user_via_peripheral);
        system_via_peripheral.set_sound_chip(&sound_chip);
        // indicators.start() is deferred to the server bootstrap, after
        // extension init() runs (the Indicators register-before-start contract).
        econet_socket.set_last_bus_value_ptr(&stale_bus_);
        serial_socket.set_last_bus_value_ptr(memory_map_.last_bus_value_ptr());
        serial_socket.ula().set_deck(&cassette_deck);
        tube_socket.set_last_bus_value_ptr(&stale_bus_);
        internal_tube_socket.set_last_bus_value_ptr(&stale_bus_);
        disc_controller.attach_drive(0, &disc_drive_0);
        disc_controller.attach_drive(1, &disc_drive_1);
        fit_sideways();
    }

    ~Master128Hardware() {
        indicators.stop();
    }

    // =========================================================================
    // CPU and debugger accesses
    // =========================================================================

    // An access made without regard to the instruction making it (the
    // debugger's, a client's): &3000-&7FFF is LYNNE only while X is set.
    uint8_t read(uint16_t addr) {
        if (addr >= 0x3000 && addr < 0x8000) {
            last_access_shadow_ = x();
            return x() ? lynne.read(addr - 0x3000) : main_ram.read(addr);
        }
        last_access_shadow_ = false;
        if (addr >= 0x8000 && addr < 0xC000) {
            if (andy_paged_for(addr)) return andy.read(addr - 0x8000);
            if (sideways.bank_type(sideways.selected_bank()) == SlotType::Empty) return stale_bus_;
        } else if (addr >= 0xC000) {
            if (hazel_paged_for(addr)) return hazel.read(addr - 0xC000);
            // ACCCON TST: reads of &FC00-&FFFF return the MOS ROM, not the
            // I/O. The emulators' consensus; Acorn says only that TST must
            // be zero, and no one has measured it (research section 5 item 10).
            if (addr >= 0xFC00 && (acccon_ & ACCCON_TST)) return mos_rom.read(addr - 0xC000);
        }
        return memory_map_.read(addr);
    }

    void write(uint16_t addr, uint8_t value) {
        if (addr >= 0x3000 && addr < 0x8000) {
            last_access_shadow_ = x();
            if (x()) {
                lynne.write(addr - 0x3000, value);
            } else {
                main_ram.write(addr, value);
            }
            return;
        }
        last_access_shadow_ = false;
        if (andy_paged_for(addr)) {
            andy.write(addr - 0x8000, value);
            return;
        }
        if (hazel_paged_for(addr)) {
            hazel.write(addr - 0xC000, value);
            return;
        }
        memory_map_.write(addr, value);
    }

    // The I/O controller's view of SYNC (research section 1.1, Reference
    // Manual F.2): with X clear and E set, the accesses an instruction makes
    // after its opcode fetch go to LYNNE when that opcode was fetched from
    // &C000-&DFFF. The flag is latched at each opcode fetch and holds for the
    // instruction's operand and data accesses (and an interrupt entry, which
    // fetches no opcode); an opcode fetch is never redirected, since its own
    // address is outside &3000-&7FFF whenever the flag it sets is true.
    // CpuBinding reports each opcode fetch here before making it.
    void note_opcode_fetch(uint16_t addr) { vdu_latch_ = addr >= 0xC000 && addr < 0xE000; }

    // The flag as last latched: the instruction executing was fetched from
    // &C000-&DFFF.
    bool vdu() const { return vdu_latch_; }

    uint8_t cpu_read(uint16_t addr) { return routed_read(addr, vdu_latch_); }
    void cpu_write(uint16_t addr, uint8_t value) { routed_write(addr, value, vdu_latch_); }

    // An access as code at `pc` would make it, for the debugger.
    uint8_t read_with_pc(uint16_t addr, uint16_t pc) {
        return routed_read(addr, pc >= 0xC000 && pc < 0xE000);
    }
    void write_with_pc(uint16_t addr, uint8_t value, uint16_t pc) {
        routed_write(addr, value, pc >= 0xC000 && pc < 0xE000);
    }

    // Whether an access to &3000-&7FFF by an instruction fetched from
    // &C000-&DFFF (`vdu`) or elsewhere goes to LYNNE. X wins whatever the
    // instruction: with X set and E clear the VDU driver's own writes land in
    // LYNNE too (the Planet Nubium corner).
    bool lynne_selected(bool vdu) const {
        if (!vdu) return x();
        if constexpr (Y_SUPPRESSES_E) {
            if (acccon_ & ACCCON_Y) return x();
        }
        return x() || (acccon_ & ACCCON_E) != 0;
    }

    uint8_t routed_read(uint16_t addr, bool vdu) {
        if (addr >= 0x3000 && addr < 0x8000) {
            last_access_shadow_ = lynne_selected(vdu);
            return last_access_shadow_ ? lynne.read(addr - 0x3000) : main_ram.read(addr);
        }
        return read(addr);
    }

    void routed_write(uint16_t addr, uint8_t value, bool vdu) {
        if (addr >= 0x3000 && addr < 0x8000) {
            last_access_shadow_ = lynne_selected(vdu);
            if (last_access_shadow_) {
                lynne.write(addr - 0x3000, value);
            } else {
                main_ram.write(addr, value);
            }
            return;
        }
        write(addr, value);
    }

    // Side-effect-free read for inspection, as read() routes it.
    uint8_t peek(uint16_t addr) const {
        if (addr >= 0x3000 && addr < 0x8000) {
            return x() ? lynne.read(addr - 0x3000) : main_ram.read(addr);
        }
        if (addr >= 0x8000 && addr < 0xC000) {
            if (andy_paged_for(addr)) return andy.read(addr - 0x8000);
            if (sideways.bank_type(sideways.selected_bank()) == SlotType::Empty) return stale_bus_;
        } else if (addr >= 0xC000) {
            if (hazel_paged_for(addr)) return hazel.read(addr - 0xC000);
            if (addr >= 0xFC00 && (acccon_ & ACCCON_TST)) return mos_rom.read(addr - 0xC000);
        }
        if (addr >= 0xFE40 && addr <= 0xFE5F) return system_via.peek(addr & 0x0F);
        if (addr >= 0xFE60 && addr <= 0xFE7F) return user_via.peek(addr & 0x0F);
        if (addr >= 0xFEE0 && addr <= 0xFEFF) return selected_tube().peek(addr & 0x07);
        return memory_map_.peek(addr);
    }

    // Which accesses wait for the 1 MHz clock (the I/O controller holds RDY):
    // &FE00-&FE1F, &FE28-&FE2B and &FE40-&FE9F, as beebjit and B2 have it,
    // and FRED and JIM while IFJ leaves them on the external 1 MHz bus.
    bool is_1mhz_access(uint16_t addr) const {
        if (addr < 0xFC00 || addr >= 0xFF00) return false;
        if (addr < 0xFE00) return (acccon_ & ACCCON_IFJ) == 0;
        const uint8_t low = static_cast<uint8_t>(addr);
        return low < 0x20 || (low >= 0x28 && low < 0x2C) || (low >= 0x40 && low < 0xA0);
    }

    // =========================================================================
    // Video
    // =========================================================================

    // What the CRTC displays. With D clear, main RAM. With D set the CRTC
    // addresses the second 64K bank throughout: LYNNE at &3000-&7FFF, and
    // below &3000 ANDY (&0000-&0FFF) and HAZEL (&1000-&2FFF), as beebjit has
    // it against a hardware-verified test (research section 1.1).
    uint8_t peek_video(uint16_t addr) const {
        if ((acccon_ & ACCCON_D) == 0) return main_ram.read(addr & 0x7FFF);
        addr &= 0x7FFF;
        if (addr >= 0x3000) return lynne.read(addr - 0x3000);
        if (addr >= 0x1000) return hazel.read(addr - 0x1000);
        return andy.read(addr);
    }

    std::string_view video_region() const {
        return (acccon_ & ACCCON_D) ? REGION_LYNNE : REGION_MAIN_RAM;
    }

    // Every character the video system fetches, rendered or not, is the
    // byte left on the data bus.
    template <typename CrtcOutput>
    void note_video_fetch(const CrtcOutput& output) {
        stale_bus_ = peek_video(
            screen_address(output.address, output.raster, addressable_latch.screen_base()));
    }

    // The byte the video system last fetched: what an unmapped read returns.
    uint8_t stale_bus() const { return stale_bus_; }

    // =========================================================================
    // Reset
    // =========================================================================

    // Power-on. The clock and its CMOS are battery backed; the chip's RESET
    // pin is on the system reset.
    void reset() {
        analogue_converter_.power_on();
        main_ram.clear();
        lynne.clear();
        hazel.clear();
        andy.clear();
        system_via.reset();
        user_via.reset();
        crtc.reset();
        video_ula.reset();
        saa5050.reset();
        sound_chip.reset();
        addressable_latch.reset();
        reset_paging();
        disc_controller.reset(ResetCause::PowerOn);
        one_mhz_bus_.reset(ResetCause::PowerOn);
        econet_socket.reset();
        adopt_cmos_station();
        serial_socket.reset();
        tube_socket.reset();
        internal_tube_socket.reset();
    }

    // Break: the System VIA is preserved, so the MOS sees a warm reset. The
    // paging registers and the clock's RESET pin are on the reset line.
    void soft_reset() {
        user_via.reset();
        crtc.reset();
        video_ula.reset();
        saa5050.reset();
        sound_chip.reset();
        addressable_latch.reset();
        reset_paging();
        disc_controller.reset(ResetCause::Break);
        one_mhz_bus_.reset(ResetCause::Break);
        econet_socket.reset();
        adopt_cmos_station();
        serial_socket.reset();
        tube_socket.reset();
        internal_tube_socket.reset();
    }

    // Econet's station number: CMOS byte 0. Setting it here is the Master's
    // --station; the socket takes it at the next reset.
    void set_cmos_station(uint8_t station) {
        rtc.poke_ram(Mc146818Rtc::FIRST_RAM_REGISTER + master_128_cmos_seed::CMOS_STATION, station);
    }
    uint8_t cmos_station() const {
        return rtc.peek_register(Mc146818Rtc::FIRST_RAM_REGISTER + master_128_cmos_seed::CMOS_STATION);
    }

    // The clock, by one 2 MHz cycle (Machine calls this every cycle,
    // including stretch cycles).
    void tick_expansion_devices() { rtc.tick(); }

    // =========================================================================
    // Output
    // =========================================================================

    void enable_video_output(size_t capacity = OutputQueue<PixelBatch>::DEFAULT_CAPACITY) {
        video_output.emplace(capacity);
    }
    void disable_video_output() { video_output.reset(); }
    bool video_output_enabled() const { return video_output.has_value(); }

    void enable_audio_output(size_t capacity = AudioBuffer::DEFAULT_CAPACITY) {
        if (!audio_buffer) audio_buffer.emplace(capacity);
        audio_frames.attach(&*audio_buffer);
    }
    void disable_audio_output() {
        audio_frames.attach(nullptr);
        audio_buffer.reset();
    }
    bool audio_output_enabled() const { return audio_buffer.has_value(); }

    // =========================================================================
    // Interrupts
    // =========================================================================

    // The devices' IRQs, and ACCCON IRR's level.
    uint8_t poll_irq() {
        const uint8_t devices = irq_aggregator_.poll();
        return (acccon_ & ACCCON_IRR) ? static_cast<uint8_t>(devices | IRR_IRQ_BIT) : devices;
    }

    // The 1770's NMI, then a 1 MHz tick of the 1770, the 1 MHz bus and the
    // ADC. The NMI is sampled before the tick, so that a DRQ the CPU has just
    // cleared makes an edge (see ModelBPlusHardware::poll_nmi).
    uint8_t poll_nmi() {
        const uint8_t nmi = disc_controller.nmi_pending() ? 0x01 : 0x00;
        disc_controller.tick();
        one_mhz_bus_.tick();
        make_clock_binding(analogue_converter_).tick_falling();
        return nmi;
    }

    void set_spin_up_delay_enabled(bool enabled) { disc_controller.set_spin_up_delay_enabled(enabled); }
    bool spin_up_delay_enabled() const { return disc_controller.spin_up_delay_enabled(); }

    // =========================================================================
    // Ports
    // =========================================================================

    OneMHzBusPort& one_mhz_bus() { return one_mhz_bus_; }
    AudioSourcePort& audio_source_port() { return audio_sources; }
    UserPort& user_port() { return user_port_; }
    PrinterPort& printer_port() { return printer_port_; }
    Upd7002& analogue_converter() { return analogue_converter_; }
    const Upd7002& analogue_converter() const { return analogue_converter_; }
    AnaloguePort& analogue_port() { return analogue_port_; }
    uint8_t light_pen_strobe() { return system_via_peripheral.light_pen_strobe(); }
    SerialPort& serial_port() { return serial_port_; }

    // =========================================================================
    // Paging state
    // =========================================================================

    uint8_t romsel() const { return romsel_; }
    uint8_t acccon() const { return acccon_; }
    bool andy_paged() const { return (romsel_ & ROMSEL_ANDY) != 0; }
    bool x() const { return (acccon_ & ACCCON_X) != 0; }

    // The Tube ACCCON ITU selects: the internal one while set.
    TubeSocket& selected_tube() {
        return (acccon_ & ACCCON_ITU) ? internal_tube_socket : tube_socket;
    }
    const TubeSocket& selected_tube() const {
        return (acccon_ & ACCCON_ITU) ? internal_tube_socket : tube_socket;
    }

    // =========================================================================
    // ROMs and sideways slots
    // =========================================================================

    void load_mos(const uint8_t* data, size_t size) { mos_rom.load(data, size); }

    void load_basic(const uint8_t* data, size_t size) {
        sideways.configure_slot(DEFAULT_LANGUAGE_SLOT, SlotType::Rom);
        sideways.load_rom(DEFAULT_LANGUAGE_SLOT, data, size);
        sideways.set_slot_image_name(DEFAULT_LANGUAGE_SLOT, DEFAULT_LANGUAGE_ROM);
    }

    void load_sideways_rom(uint8_t slot, const uint8_t* data, size_t len,
                           std::string_view image_name = "") {
        sideways.load_sideways_rom(slot, data, len, image_name);
    }

    void load_sideways_data(uint8_t slot, const uint8_t* data, size_t len,
                            std::string_view image_name = "") {
        sideways.load_sideways_data(slot, data, len, image_name);
    }

    static constexpr bool is_slot_loadable(uint8_t slot) {
        return ConfigurableBankedMemory::is_slot_loadable(slot);
    }

    void configure_slot_as_ram(uint8_t slot) { sideways.configure_slot_as_ram(slot); }
    void configure_slot_as_empty(uint8_t slot) { sideways.configure_slot_as_empty(slot); }

    SlotInfo slot_info(uint8_t slot) const { return sideways.slot_info(slot); }

    // Links LK18 and LK19 (research section 1.6, ROM sockets): West, as
    // shipped, gives sideways RAM in banks 4/5 (LK18) and 6/7 (LK19); East
    // gives the IC41 (4/5) and IC37 (6/7) sockets instead, each taking a 32K
    // ROM whose low half is the even bank and high half the odd one. The
    // Service Manual and Advanced Reference Manual swap the two sockets; the
    // Reference Manual, the Hardware Specification and the board agree on
    // IC41 = 4/5.
    struct MotherboardLinks {
        static constexpr bool has_slot_links = true;

        enum class Position { West, East };
        Position lk18 = Position::West;
        Position lk19 = Position::West;

        std::optional<std::string> parse(std::string_view key, std::string_view value) {
            auto lower = [](std::string_view sv) {
                std::string out;
                for (char c : sv) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                return out;
            };
            const std::string k = lower(key);
            const std::string v = lower(value);
            Position* link = k == "lk18" ? &lk18 : k == "lk19" ? &lk19 : nullptr;
            if (!link) {
                return "Unknown motherboard link '" + std::string(key) +
                       "' on the BBC Master 128 (expected: lk18, lk19)";
            }
            if (v == "west") { *link = Position::West; return std::nullopt; }
            if (v == "east") { *link = Position::East; return std::nullopt; }
            return "Invalid value for motherboard link " + std::string(key) + ": '" +
                   std::string(value) + "' (expected: west, east)";
        }

        std::vector<MotherboardLinkInfo> describe() const {
            auto name = [](Position p) { return p == Position::West ? "west" : "east"; };
            return {
                {"LK18", name(lk18),
                 "Banks 4/5: West = sideways RAM (default), East = a 32K ROM in IC41"},
                {"LK19", name(lk19),
                 "Banks 6/7: West = sideways RAM (default), East = a 32K ROM in IC37"},
            };
        }

        static std::vector<std::string> help_lines() {
            return {
                "lk18=west|east  banks 4/5 (west=sideways RAM, east=IC41 32K ROM; default: west)",
                "lk19=west|east  banks 6/7 (west=sideways RAM, east=IC37 32K ROM; default: west)",
            };
        }

        // Whether `slot` is a ROM socket's half under these links.
        bool rom_socket(int slot) const {
            if (slot == 4 || slot == 5) return lk18 == Position::East;
            if (slot == 6 || slot == 7) return lk19 == Position::East;
            return false;
        }
    };

    // Make the links so: banks whose link is East become the empty halves of
    // their ROM socket, for --sideways to fill. Called once at start-up,
    // before ROMs are loaded.
    void apply_motherboard_links(const MotherboardLinks& links) {
        links_ = links;
        for (uint8_t slot = FIRST_SIDEWAYS_RAM_SLOT; slot <= LAST_SIDEWAYS_RAM_SLOT; ++slot) {
            if (links.rom_socket(slot)) {
                sideways.configure_slot_as_empty(slot);
            } else {
                sideways.configure_slot_as_ram(slot);
            }
        }
    }

    const MotherboardLinks& motherboard_links() const { return links_; }

    // The sixteen sideways banks (research section 1.6, ROM sockets):
    // - 0-3: the cartridge slots, rear (0, 1) and front (2, 3), empty unless a
//   cartridge (an extension at cartridge-rear or cartridge-front) fills them;
    // - 4-7: sideways RAM, or with LK18/LK19 East the halves of IC41/IC37;
    // - 8: the IC27 socket, ROM or empty;
    // - 9-15: the MOS ROM's sideways parts, locked inside the chip.
    static SlotTopology slot_topology() { return slot_topology(MotherboardLinks{}); }
    static SlotTopology slot_topology(MotherboardLinks links) {
        SlotTopology topo;
        topo.has_aliasing = false;
        for (int slot = 0; slot < 16; ++slot) {
            SocketSpec spec;
            spec.socket_index = slot;
            spec.slots = {slot};
            spec.runtime_configurable = false;
            spec.supports_write_protect = false;
            if (slot < FIRST_SIDEWAYS_RAM_SLOT) {
                // Filled by a cartridge fitted to the slot (an extension at
                // cartridge-rear or cartridge-front), not by --sideways.
                spec.label = std::string(slot < 2 ? "Rear cartridge" : "Front cartridge") +
                             " (bank " + std::to_string(slot) + ")";
                spec.supports_rom = false;
                spec.supports_ram = false;
                spec.supports_empty = true;
                spec.provided_by_point = slot < 2 ? "cartridge-rear" : "cartridge-front";
            } else if (slot <= LAST_SIDEWAYS_RAM_SLOT && links.rom_socket(slot)) {
                spec.label = std::string(slot < 6 ? "IC41" : "IC37") + " (bank " +
                             std::to_string(slot) + ")";
                spec.supports_rom = true;
                spec.supports_ram = false;
                spec.supports_empty = true;
            } else if (slot <= LAST_SIDEWAYS_RAM_SLOT) {
                spec.label = "Sideways RAM (bank " + std::to_string(slot) + ")";
                spec.supports_rom = false;
                spec.supports_ram = true;
                spec.supports_empty = false;
            } else if (slot == IC27_SLOT) {
                spec.label = "IC27 (bank 8)";
                spec.supports_rom = true;
                spec.supports_ram = false;
                spec.supports_empty = true;
            } else {
                spec.label = "MOS ROM (bank " + std::to_string(slot) + ")";
                spec.supports_rom = true;
                spec.supports_ram = false;
                spec.supports_empty = false;
            }
            topo.sockets.push_back(std::move(spec));
        }
        return topo;
    }

    void set_open_bus_mode(OpenBusMode mode) { memory_map_.set_open_bus_mode(mode); }
    OpenBusMode open_bus_mode() const { return memory_map_.open_bus_mode(); }

    // =========================================================================
    // Debugger
    // =========================================================================

    std::vector<MemoryRegionDescriptor> get_memory_regions() const {
        const auto ram = RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated;
        auto active_if = [ram](bool active) { return active ? (ram | RegionFlags::Active) : ram; };
        std::vector<MemoryRegionDescriptor> regions{
            {REGION_MAIN_RAM, 0x0000, 32768, ram},
            {REGION_LYNNE, 0x3000, 0x5000, active_if(x())},
            {REGION_HAZEL, 0xC000, 0x2000, active_if((acccon_ & ACCCON_Y) != 0)},
            {REGION_ANDY, 0x8000, 0x1000, active_if(andy_paged())},
            {REGION_MOS_ROM, 0xC000, 16384, RegionFlags::Readable | RegionFlags::Populated},
        };
        regions.reserve(regions.size() + 16);
        for (uint8_t bank = 0; bank < 16; ++bank) {
            regions.push_back(sideways_bank_region(bank_names_[bank], slot_info(bank),
                                                   bank == sideways.selected_bank()));
        }
        return regions;
    }

    // The values a debugger condition can name on this machine (DebugVariable):
    // ROMSEL and its bank, ANDY, ACCCON's bits by their hardware names, and
    // whether the access a watchpoint judges went to LYNNE.
    std::vector<DebugVariable> debug_variables() const {
        auto variables = romsel_variables([this] { return uint64_t{romsel_}; });
        auto acccon_bit = [this](uint8_t mask) {
            return [this, mask] { return uint64_t{(acccon_ & mask) != 0}; };
        };
        variables.insert(variables.end(), {
            {"acccon", "The ACCCON register (&FE34) as last written: IRR TST IFJ ITU Y X E D.",
             [this] { return uint64_t{acccon_}; }},
            {"andy", "1 while ROMSEL bit 7 pages ANDY's RAM in at &8000-&8FFF.",
             [this] { return uint64_t{andy_paged()}; }},
            {"accd", "ACCCON bit 0 (D): the display shows LYNNE.", acccon_bit(ACCCON_D)},
            {"acce", "ACCCON bit 1 (E): an instruction fetched from &C000-&DFFF reaches LYNNE "
                     "at &3000-&7FFF.",
             acccon_bit(ACCCON_E)},
            {"accx", "ACCCON bit 2 (X): every access to &3000-&7FFF reaches LYNNE.",
             acccon_bit(ACCCON_X)},
            {"accy", "ACCCON bit 3 (Y): HAZEL is paged in at &C000-&DFFF.", acccon_bit(ACCCON_Y)},
            {"accitu", "ACCCON bit 4 (ITU): &FEE0 reaches the internal Tube.",
             acccon_bit(ACCCON_ITU)},
            {"accifj", "ACCCON bit 5 (IFJ): FRED and JIM are the cartridge bus's.",
             acccon_bit(ACCCON_IFJ)},
            {"acctst", "ACCCON bit 6 (TST): reads of &FC00-&FFFF return the MOS ROM.",
             acccon_bit(ACCCON_TST)},
            {"accirr", "ACCCON bit 7 (IRR): the IRQ line is held low.", acccon_bit(ACCCON_IRR)},
            {"shadow_access", "1 if the access went to LYNNE, 0 if to main RAM: decided by "
                              "X, and by E for an instruction fetched from &C000-&DFFF.",
             [this] { return uint64_t{last_access_shadow_}; }, /*watchpoint_only=*/true},
        });
        return variables;
    }

    std::optional<RegionExtent> region_extent(std::string_view name) const {
        if (name == REGION_MAIN_RAM) return RegionExtent{REGION_MAIN_RAM, 0x0000, 0x8000};
        if (name == REGION_LYNNE) return RegionExtent{REGION_LYNNE, 0x3000, 0x5000};
        if (name == REGION_HAZEL) return RegionExtent{REGION_HAZEL, 0xC000, 0x2000};
        if (name == REGION_ANDY) return RegionExtent{REGION_ANDY, 0x8000, 0x1000};
        if (name == REGION_MOS_ROM) return RegionExtent{REGION_MOS_ROM, 0xC000, 0x4000};
        if (name.size() >= 6 && name.substr(0, 5) == "bank_") {
            uint8_t bank = parse_bank_number(name);
            if (bank < 16) return sideways_bank_extent(bank_names_[bank]);
        }
        return std::nullopt;
    }

    bool has_region(std::string_view name) const { return region_extent(name).has_value(); }

    uint8_t peek_region(std::string_view name, uint32_t address) const {
        checked_region(name, address);
        return region_byte(name, address, [this](uint8_t bank, uint16_t offset) {
            return sideways.peek_bank(bank, offset);
        });
    }

    uint8_t read_region(std::string_view name, uint32_t address) {
        checked_region(name, address);
        if (const auto bank = parse_bank_number(name); bank < 16) {
            return sideways.read_bank(bank, static_cast<uint16_t>(address - 0x8000));
        }
        return peek_region(name, address);
    }

    void write_region(std::string_view name, uint32_t address, uint8_t value) {
        checked_region(name, address);
        const auto a = static_cast<uint16_t>(address);
        if (name == REGION_MAIN_RAM) { main_ram.write(a, value); return; }
        if (name == REGION_LYNNE) { lynne.write(a - 0x3000, value); return; }
        if (name == REGION_HAZEL) { hazel.write(a - 0xC000, value); return; }
        if (name == REGION_ANDY) { andy.write(a - 0x8000, value); return; }
        if (name == REGION_MOS_ROM) return;  // read-only
        sideways.write_bank(parse_bank_number(name), static_cast<uint16_t>(a - 0x8000), value);
    }

private:
    uint8_t romsel_ = 0;
    uint8_t acccon_ = 0;
    bool vdu_latch_ = false;           // see note_opcode_fetch()
    bool last_access_shadow_ = false;  // where the last access went (shadow_access)
    uint8_t stale_bus_ = 0;            // see stale_bus()
    MotherboardLinks links_{};

    static constexpr std::string_view bank_names_[16] = {
        "bank_0", "bank_1", "bank_2", "bank_3",
        "bank_4", "bank_5", "bank_6", "bank_7",
        "bank_8", "bank_9", "bank_10", "bank_11",
        "bank_12", "bank_13", "bank_14", "bank_15"
    };

    static uint8_t parse_bank_number(std::string_view name) {
        if (name.size() == 6) {
            char c = name[5];
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        } else if (name.size() == 7 && name[5] == '1') {
            char c = name[6];
            if (c >= '0' && c <= '5') return static_cast<uint8_t>(10 + (c - '0'));
        }
        return 255;
    }

    void checked_region(std::string_view name, uint32_t address) const {
        const auto region = region_extent(name);
        if (!region) {
            throw std::invalid_argument("unknown region: '" + std::string(name) + "'");
        }
        validate_region_address(*region, address);
    }

    template <typename BankByte>
    uint8_t region_byte(std::string_view name, uint32_t address, BankByte bank_byte) const {
        const auto a = static_cast<uint16_t>(address);
        if (name == REGION_MAIN_RAM) return main_ram.read(a);
        if (name == REGION_LYNNE) return lynne.read(a - 0x3000);
        if (name == REGION_HAZEL) return hazel.read(a - 0xC000);
        if (name == REGION_ANDY) return andy.read(a - 0x8000);
        if (name == REGION_MOS_ROM) return mos_rom.read(a - 0xC000);
        return bank_byte(parse_bank_number(name), static_cast<uint16_t>(a - 0x8000));
    }

    bool andy_paged_for(uint16_t addr) const {
        return addr >= 0x8000 && addr < 0x9000 && andy_paged();
    }

    bool hazel_paged_for(uint16_t addr) const {
        return addr >= 0xC000 && addr < 0xE000 && (acccon_ & ACCCON_Y) != 0;
    }

    void write_romsel(uint8_t value) {
        romsel_ = value;
        sideways.select_bank(value & ROMSEL_BANK_MASK);
    }

    // The filing system takes its station number from CMOS byte 0 as the
    // machine restarts.
    void adopt_cmos_station() { econet_socket.adopt_station(cmos_station()); }

    void reset_paging() {
        romsel_ = 0;
        acccon_ = 0;
        vdu_latch_ = false;
        sideways.select_bank(0);
        rtc.reset();
    }

    // Banks 4-7 are RAM; the clock takes its seed until a saved machine's
    // CMOS is restored over it.
    void fit_sideways() {
        for (uint8_t slot = FIRST_SIDEWAYS_RAM_SLOT; slot <= LAST_SIDEWAYS_RAM_SLOT; ++slot) {
            sideways.configure_slot_as_ram(slot);
        }
        clock_unit_.seed();
    }

    // The clock and its CMOS RAM: one chip, one battery-backed unit.
    Mc146818BatteryUnit clock_unit_{
        rtc, "master-128-cmos", "Master 128: MC146818",
        [](Mc146818Rtc& clock) { master_128_cmos_seed::apply_to_clock(clock); }};

public:
    std::vector<BatteryBackedUnit*> battery_units() { return {&clock_unit_}; }

private:
    MemoryMapType memory_map_;
    IrqAggregatorType irq_aggregator_;

    IrqAggregatorType make_irq_aggregator() {
        return beebium::make_irq_aggregator(
            make_irq_binding<0>(system_via),
            make_irq_binding<1>(user_via),
            make_irq_binding<2>(selected_tube_irq_),
            make_irq_binding<3>(one_mhz_bus_),
            make_irq_binding<4>(serial_socket)
        );
    }

    MemoryMapType make_memory_map() {
        // Order matters: first match wins. I/O regions overlay the MOS ROM.
        return MemoryMap{
            make_region<0xFC00, 0xFDFF>(fred_jim_region_),
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(crtc),
            make_region<0xFE08, 0xFE0F, Mirror<0x01>>(serial_acia_region_),
            make_region<0xFE10, 0xFE17, Mirror<0x07>>(serial_ula_region_),
            make_region<0xFE18, 0xFE1F, Mirror<0x03>>(analogue_converter_),
            make_region<0xFE20, 0xFE23, Mirror<0x01>>(video_ula),
            make_region<0xFE24, 0xFE27>(disc_latch_region_),
            make_region<0xFE28, 0xFE2B, Mirror<0x03>>(disc_register_region_),
            make_region<0xFE2C, 0xFE2F>(stale_bus_region_),
            make_region<0xFE30, 0xFE33, Mirror<0x03>>(romsel_register),
            make_region<0xFE34, 0xFE37, Mirror<0x03>>(acccon_register),
            make_region<0xFE38, 0xFE3B>(econet_intoff_region_),
            make_region<0xFE3C, 0xFE3F>(econet_inton_region_),
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(system_via),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(user_via),
            make_region<0xFE80, 0xFE9F, Mirror<0x0F>>(stale_bus_region_),  // nMODEM: PL12, unfitted
            make_region<0xFEA0, 0xFEBF, Mirror<0x03>>(econet_adlc_region_),
            make_region<0xFEC0, 0xFEDF>(stale_bus_region_),
            make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(selected_tube_region_),
            make_region<0x0000, 0x7FFF>(main_ram),
            make_region<0x8000, 0xBFFF>(sideways),
            make_region<0xC000, 0xFFFF>(mos_rom)
        };
    }
};

}  // namespace beebium
