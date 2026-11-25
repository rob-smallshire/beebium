// Copyright 2026 James Sargent <acheton1984@letterboxes.org>
//
// This file is part of AESulator, a plugin for Beebium.
//
// AESulator is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. AESulator is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with AESulator.
// If not, see <https://www.gnu.org/licenses/>.

// AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
// AesMemory.hpp, the memory map's interface
// version 2.0.0, 20 November 2026.

// AesMemory, the board's memory and I/O as the ARM core sees it (see AesMemory.cpp for the map),
// and TubePort, its link to the Tube chip.

#pragma once

#include "AesOptions.hpp"
#include "AesTiming.hpp"
#include "ArmCore.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// The Tube chip as AesMemory sees it: eight registers, numbered 0-7 as the chip numbers them. It is
// an interface so that AesMemory needs nothing from Beebium: AesTube implements it by passing each
// access on to Beebium's own Tube chip, and the tests put a stand-in here.
class TubePort {
public:
    virtual ~TubePort() = default;
    // A read or write of one register, with the chip's side effects (a read takes the byte).
    virtual uint8_t read(int reg) = 0;
    virtual void write(int reg, uint8_t value) = 0;
    // The debugger's look at a register, which must change nothing: a read takes the register's
    // byte and moves its flags, a look does neither. A stand-in that cannot look shows &FF.
    virtual uint8_t peek(int reg) const {
        (void)reg;
        return 0xFF;
    }
};

// Whether an address is in the Tube's region, address bits A25 and A24 = 01. AesTube and the Tube
// log use it too.
inline bool in_tube(uint32_t address) { return ((address >> 24) & 3) == 1; }

// The board's memory and I/O as the ARM core sees it, through ArmBus (the core's interface to
// memory). The core calls it for every access; it decodes the address, costs the cycle with
// AesTiming (the board's clock) and raises the abort line when an access should abort. AesTube
// owns it; the debugger reads and writes through it too.
class AesMemory : public ArmBus {
public:
    static constexpr uint32_t kRomBytes = 0x4000;  // The ROM's size, 16 KB.
    static constexpr uint32_t kA24 = 0x01000000;  // Address bit A24.

    // Memory for the switches in `options`, costing its cycles with `timing` and reaching the Tube
    // chip through `tube`; it starts as power-on leaves it.
    AesMemory(const AesOptions& options, AesTiming& timing, TubePort& tube);

    // The ROM image, read from the file the rom setting names, or from `path`; false if missing
    // or not 16 KB. set_rom gives an image directly, for tests.
    bool load_rom();
    bool load_rom(const std::string& path);
    void set_rom(const std::vector<uint8_t>& image) { rom_ = image; }

    // Power-on fills RAM and resets; reset sets the boot latch again and abort strict back to lax.
    void power_on();
    void reset();

    // ArmBus: the core's reads, writes and internal cycles.
    uint32_t read_word(uint32_t addr, bool seq) override;
    uint32_t read_byte(uint32_t addr, bool seq) override;
    void write_word(uint32_t addr, uint32_t value, bool seq) override;
    void write_byte(uint32_t addr, uint8_t value, bool seq) override;
    void internal(uint32_t addr) override;

    // For the debugger, which must see memory with no time passing and nothing changed. peek is
    // what the core would read, boot latch included.
    uint8_t peek(uint32_t addr) const;
    // The debugger's ram region: the fitted size, and RAM itself, latch or not.
    uint32_t ram_bytes() const { return fitted_bytes_; }
    uint8_t peek_ram(uint32_t offset) const { return ram_[offset]; }
    void poke_ram(uint32_t offset, uint8_t value) { ram_[offset] = value; }
    // The debugger's write of a byte at an address the core could write; RAM only.
    bool poke(uint32_t addr, uint8_t value);
    // State the tests and AesTube read: suspect accesses counted, the boot latch, whether abort
    // strict's checking has started, and whether an address is outside abort strict's map.
    int suspect_accesses() const { return suspect_accesses_; }
    bool boot_latch_set() const { return boot_latch_; }
    bool strict_on() const { return strict_on_; }
    bool outside_strict_map(uint32_t addr) const;

private:
    // Where an access goes: the device, the offset within it, and whether it is fitted (an
    // unfitted target reads as undriven and keeps no write).
    enum class Kind { Ram, Rom, Tube, Region10, Empty };
    struct Target {
        Kind kind;
        uint32_t offset;
        bool fitted;
    };

    // Sends an address to its device, by the map.
    Target decode(uint32_t addr, bool for_write) const;
    // Where an address lands in RAM, and in the ROM.
    Target ram_target(uint32_t addr) const;
    static Target rom_target(uint32_t addr);
    // Which price AesTiming charges for a cycle.
    AesTiming::Place place_of(const Target& target, uint32_t addr) const;
    // Every access: costs the cycle and raises the abort line if it should abort.
    void note_cycle(const Target& target, uint32_t addr, bool seq, bool for_write);
    // The start of every read, and of every write.
    Target start_read(uint32_t addr, bool seq);
    Target start_write(uint32_t addr, bool seq);
    // The word a read sees from anything but the Tube.
    uint32_t read_lanes(const Target& target) const;
    // What a lane reads when nothing drives it.
    uint32_t undriven() const;
    // Reads a Tube register onto lane 0.
    uint32_t tube_lanes(uint32_t addr);
    // True when the abort setting is off, which means no abort at all.
    bool abort_tied() const { return opt_.abort == AesOptions::MachineAbort::Off; }
    // Notes a suspect access, by access.check.
    void suspect();
    // Notes a write's effect on the boot latch.
    void note_write(const Target& target);
    // Starts abort strict's checking at the first user-mode access.
    void watch_mode();

    const AesOptions opt_;
    AesTiming& timing_;
    TubePort& tube_;
    std::vector<uint8_t> ram_;  // Always 4 MB; see the constructor.
    std::vector<uint8_t> rom_;
    uint32_t fitted_bytes_;
    uint32_t last_bus_ = 0;  // The bus's last value, for ram.absent_bank's LastBus choice.
    bool boot_latch_ = true;  // While set, reads at low addresses get the ROM.
    bool strict_on_ = false;  // Whether abort strict's checking has started.
    int suspect_accesses_ = 0;
};
