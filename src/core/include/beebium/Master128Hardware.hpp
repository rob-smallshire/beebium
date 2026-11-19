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
#include "ClockBinding.hpp"
#include "devices/VideoUla.hpp"
#include "econet/EconetSocket.hpp"
#include "cassette/CassetteDeck.hpp"
#include "cassette/CassetteMotorLed.hpp"
#include "serial/SerialSocket.hpp"
#include "tube/TubeSocket.hpp"
#include "indicators/Indicators.hpp"
#include <array>
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
// ROMSEL and ACCCON, the VIAs, the ADLC at &FEA0, the Tube at &FEE0. The
// 1770 (&FE24-&FE2B), INTOFF/INTON (&FE38/&FE3C), the modem connector
// (&FE80-&FE9F) and &FEC0-&FEDF read as the stale data bus until something
// is decoded there.
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

    // Default ROMs: the MOS 3.20 ROM's sixteen-kilobyte parts. The MOS half
    // is the MOS image; the other seven are banks 9 to 15.
    static constexpr std::string_view DEFAULT_MOS_ROM = "acorn-mos_3_20.rom";
    static constexpr std::string_view DEFAULT_LANGUAGE_ROM = "bbc-basic_4.rom";
    static constexpr uint8_t DEFAULT_LANGUAGE_SLOT = 12;
    static constexpr std::array<DefaultSidewaysRom, 6> DEFAULT_SIDEWAYS_ROMS{{
        {9, "acorn-dfs_2_24.rom"},
        {10, "acornsoft-viewsheet_b1_0.rom"},
        {11, "acorn-edit_1_00.rom"},
        {13, "acorn-adfs_1_50.rom"},
        {14, "acornsoft-view_b3_0.rom"},
        {15, "acorn-terminal_1_20_mos_3_20.rom"},
    }};

    // Launch option for the clock: --rtc clock=<host|emulated>[:time=...|:offset=...].
    static constexpr std::string_view BOARD_RTC_OPTION = "rtc";

    // Sideways banks by what fills them.
    static constexpr uint8_t FIRST_SIDEWAYS_RAM_SLOT = 4;
    static constexpr uint8_t LAST_SIDEWAYS_RAM_SLOT = 7;
    static constexpr uint8_t IC27_SLOT = 8;
    static constexpr uint8_t FIRST_MOS_ROM_SLOT = 9;

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

    using IrqAggregatorType = IrqAggregator<
        IrqBinding<Via6522, 0>,        // System VIA -> bit 0
        IrqBinding<Via6522, 1>,        // User VIA -> bit 1
        IrqBinding<TubeSocket, 2>,     // Tube HIRQ -> bit 2
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

    // Registered after the lock LEDs and before the drives' (CassetteMotorLed.hpp).
    CassetteMotorLed cassette_motor_led{indicators};

    EconetSocket econet_socket;
    SerialSocket serial_socket;

    static constexpr bool has_cassette = true;
    CassetteDeck cassette_deck;

    SerialPort serial_port_{serial_socket};

    // The external Tube.
    TubeSocket tube_socket;

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
    RomselRegister romsel_register{*this};
    AccconRegister acccon_register{*this};

    // The external 1 MHz bus (FRED/JIM, &FC00-&FDFF).
    OneMHzBusPort one_mhz_bus_;

    using MemoryMapType = decltype(
        MemoryMap{
            make_region<0xFC00, 0xFDFF>(std::declval<OneMHzBusPort&>()),
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(std::declval<Crtc6845&>()),
            make_region<0xFE08, 0xFE0F, Mirror<0x01>>(std::declval<SerialAciaRegion&>()),
            make_region<0xFE10, 0xFE17, Mirror<0x07>>(std::declval<SerialUlaRegion&>()),
            make_region<0xFE18, 0xFE1F, Mirror<0x03>>(std::declval<Upd7002&>()),
            make_region<0xFE20, 0xFE23, Mirror<0x01>>(std::declval<VideoUla&>()),
            make_region<0xFE24, 0xFE2F>(std::declval<StaleBusRegion&>()),
            make_region<0xFE30, 0xFE33, Mirror<0x03>>(std::declval<RomselRegister&>()),
            make_region<0xFE34, 0xFE37, Mirror<0x03>>(std::declval<AccconRegister&>()),
            make_region<0xFE38, 0xFE3F>(std::declval<StaleBusRegion&>()),
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE80, 0xFE9F>(std::declval<StaleBusRegion&>()),
            make_region<0xFEA0, 0xFEBF, Mirror<0x03>>(std::declval<EconetAdlcRegion&>()),
            make_region<0xFEC0, 0xFEDF>(std::declval<StaleBusRegion&>()),
            make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(std::declval<TubeSocket&>()),
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
        cassette_motor_led.attach(serial_socket.ula());
        tube_socket.set_last_bus_value_ptr(&stale_bus_);
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
        if (Y_SUPPRESSES_E && (acccon_ & ACCCON_Y)) return x();
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
        if (addr >= 0xFEE0 && addr <= 0xFEFF) return tube_socket.peek(addr & 0x07);
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
        one_mhz_bus_.reset(ResetCause::PowerOn);
        econet_socket.reset();
        serial_socket.reset();
        tube_socket.reset();
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
        one_mhz_bus_.reset(ResetCause::Break);
        econet_socket.reset();
        serial_socket.reset();
        tube_socket.reset();
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

    // No disc controller yet: ticks the 1 MHz bus and the ADC.
    uint8_t poll_nmi() {
        one_mhz_bus_.tick();
        make_clock_binding(analogue_converter_).tick_falling();
        return 0;
    }

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

    // The keyboard's start-up links, which the Master does not have; kept
    // for the server's start-up options until the keyboard round removes
    // them.
    void set_startup_options(uint8_t options) {
        system_via_peripheral.keyboard().set_startup_options(options);
    }
    uint8_t startup_options() const { return system_via_peripheral.keyboard().startup_options(); }
    void set_screen_mode(uint8_t mode) { system_via_peripheral.keyboard().set_screen_mode(mode); }
    uint8_t screen_mode() const { return system_via_peripheral.keyboard().screen_mode(); }
    void set_auto_boot(bool enabled) { system_via_peripheral.keyboard().set_auto_boot(enabled); }
    bool auto_boot() const { return system_via_peripheral.keyboard().auto_boot(); }

    // =========================================================================
    // Paging state
    // =========================================================================

    uint8_t romsel() const { return romsel_; }
    uint8_t acccon() const { return acccon_; }
    bool andy_paged() const { return (romsel_ & ROMSEL_ANDY) != 0; }
    bool x() const { return (acccon_ & ACCCON_X) != 0; }

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

    using MotherboardLinks = EmptyMotherboardLinks;

    // The sixteen sideways banks (research section 1.6, ROM sockets):
    // - 0-3: the cartridge slots, rear (0, 1) and front (2, 3); ROM or empty;
    // - 4-7: sideways RAM;
    // - 8: the IC27 socket, ROM or empty;
    // - 9-15: the MOS ROM's sideways parts, locked inside the chip.
    static SlotTopology slot_topology(MotherboardLinks /*links*/ = {}) {
        SlotTopology topo;
        topo.has_aliasing = false;
        for (int slot = 0; slot < 16; ++slot) {
            SocketSpec spec;
            spec.socket_index = slot;
            spec.slots = {slot};
            spec.runtime_configurable = false;
            spec.supports_write_protect = false;
            if (slot < FIRST_SIDEWAYS_RAM_SLOT) {
                spec.label = std::string(slot < 2 ? "Rear cartridge" : "Front cartridge") +
                             " (bank " + std::to_string(slot) + ")";
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

    std::vector<DebugVariable> debug_variables() const {
        auto variables = romsel_variables([this] { return uint64_t{romsel_}; });
        variables.push_back(
            {"shadow_access", "1 if the access went to LYNNE, 0 if to main RAM.",
             [this] { return uint64_t{last_access_shadow_}; }, /*watchpoint_only=*/true});
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
            make_irq_binding<2>(tube_socket),
            make_irq_binding<3>(one_mhz_bus_),
            make_irq_binding<4>(serial_socket)
        );
    }

    MemoryMapType make_memory_map() {
        // Order matters: first match wins. I/O regions overlay the MOS ROM.
        return MemoryMap{
            make_region<0xFC00, 0xFDFF>(one_mhz_bus_),
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(crtc),
            make_region<0xFE08, 0xFE0F, Mirror<0x01>>(serial_acia_region_),
            make_region<0xFE10, 0xFE17, Mirror<0x07>>(serial_ula_region_),
            make_region<0xFE18, 0xFE1F, Mirror<0x03>>(analogue_converter_),
            make_region<0xFE20, 0xFE23, Mirror<0x01>>(video_ula),
            make_region<0xFE24, 0xFE2F>(stale_bus_region_),
            make_region<0xFE30, 0xFE33, Mirror<0x03>>(romsel_register),
            make_region<0xFE34, 0xFE37, Mirror<0x03>>(acccon_register),
            make_region<0xFE38, 0xFE3F>(stale_bus_region_),
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(system_via),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(user_via),
            make_region<0xFE80, 0xFE9F>(stale_bus_region_),
            make_region<0xFEA0, 0xFEBF, Mirror<0x03>>(econet_adlc_region_),
            make_region<0xFEC0, 0xFEDF>(stale_bus_region_),
            make_region<0xFEE0, 0xFEFF, Mirror<0x07>>(tube_socket),
            make_region<0x0000, 0x7FFF>(main_ram),
            make_region<0x8000, 0xBFFF>(sideways),
            make_region<0xC000, 0xFFFF>(mos_rom)
        };
    }
};

}  // namespace beebium
