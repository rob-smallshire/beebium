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
// AesOptions.hpp, every switch for the board, with its default
// version 2.0.0, 20 November 2026.

// Every switch for the AES board: behaviours the emulator may need to change, each with its
// default. docs/arm1-and-aes.md, Part 2, explains them. Each field is the switch's name
// with _ for the dot: time_rom_wait is time.rom_wait. Beebium offers three as settings, rom, ram
// and abort, filled in through set_option when the machine is built. To change any other, edit
// its default here or in ArmOptions and rebuild; the test options_defaults_are_in_range checks
// the edit, because Beebium does not see these.

#pragma once

#include "ArmCore.hpp"

#include <functional>
#include <string>
#include <vector>

// The board's switches, one field each; the core's are in `cpu`. AesExtension fills one in from
// the plugin's settings, and every part of the board keeps a copy.
struct AesOptions {
    enum class MachineAbort { Off, Strict };
    enum class MachineCheck { Off, Log, Abort };
    enum class RamRepeat { AddressLines, FittedSize };
    enum class RamPowerOn { Zero, Random };
    enum class RamAbsentBank { AllOnes, LastBus, LowRam };
    enum class BootLatch { AnyWrite, RamWritesOnly };
    enum class Region10Read { Nothing, Abort, RamRepeat };
    enum class Region10Write { Discard, RamRepeat };
    enum class TubeAddrDecode { LowerHalf, WholeRegion };
    enum class TubeWidth { LowByte, Undefined };
    enum class BlockBreak { AfterLastWord, AtBlockStart, None };
    enum class TubeSync { None, NextHostEdge };
    enum class Links { Normal, SeqTied };
    enum class TubePrstLevel { Holds, Pulse };

    // What each field decides:
    //   cpu               the core's switches (ArmCore.hpp).
    //   ram               RAM fitted, in MB.
    //   abort             off: ABORT is never raised. strict: from the first user-mode access,
    //                     any access outside the plain map (fitted RAM, the Tube's 32 bytes, the
    //                     16 KB ROM) aborts. Lax again after each reset.
    //   rom               the path of the 16 KB Executive image, read at start.
    //   access_check      what a suspect access (a ROM write, a Tube word access) does: nothing,
    //                     counted, or counted and aborted (if abort is not off).
    //   ram_repeat        how RAM repeats through its region: by its address lines, or by size.
    //   ram_power_on      RAM at power-on: zeros, or random from a fixed seed.
    //   ram_absent_bank   what a read that nothing answers gives: all ones, the last bus value,
    //                     or, for an unfitted bank, low RAM again.
    //   boot_latch        what clears the boot latch (the ROM read at low addresses after reset):
    //                     any write, or a RAM write only.
    //   region10_read     a read of region 10, &2000000 up: nothing, an abort (if abort is
    //                     not off), or RAM repeated.
    //   region10_write    a write there: lost, or to RAM repeated.
    //   tube_addr_decode  whether the Tube answers in the lower half of its region or all of it.
    //   tube_width        what a word access to the Tube moves: the low byte, or nothing driven.
    //   tube_prst_level   whether the host's PRST holds the ARM while it lasts, or only restarts
    //                     it when it ends.
    //   time_block_break  which sequential RAM cycles pay the block break (AesTiming.cpp).
    //   time_refresh_period, time_refresh_cost   ticks between refresh requests; ticks a
    //                     refresh costs.
    //   time_rom_wait, time_tube_wait   the waits, in PAL clocks.
    //   time_tube_sync    whether a Tube cycle also waits for the host's next 2 MHz edge.
    //   time_links        normal, or SEQ tied so that no cycle is sequential.
    ArmOptions cpu;

    int ram = 4;  // RAM fitted, in MB: 2 or 4.
    MachineAbort abort = MachineAbort::Off;
    // The path of the Executive ROM image (16 KB), read when the machine starts. Required: no ROM
    // is bundled, and there is no default name, because Beebium always passes one and every test
    // names its own.
    std::string rom;
    MachineCheck access_check = MachineCheck::Off;

    RamRepeat ram_repeat = RamRepeat::AddressLines;
    RamPowerOn ram_power_on = RamPowerOn::Zero;
    RamAbsentBank ram_absent_bank = RamAbsentBank::AllOnes;
    BootLatch boot_latch = BootLatch::AnyWrite;
    Region10Read region10_read = Region10Read::Nothing;
    Region10Write region10_write = Region10Write::Discard;
    TubeAddrDecode tube_addr_decode = TubeAddrDecode::LowerHalf;
    TubeWidth tube_width = TubeWidth::LowByte;
    TubePrstLevel tube_prst_level = TubePrstLevel::Holds;

    BlockBreak time_block_break = BlockBreak::AfterLastWord;
    // Ticks between refresh requests: 64 PAL clocks of 3.
    int time_refresh_period = 192;
    // Ticks the ARM is stopped for each refresh.
    int time_refresh_cost = 9;
    // The ROM and Tube waits, in PAL clocks.
    int time_rom_wait = 8;
    int time_tube_wait = 2;
    TubeSync time_tube_sync = TubeSync::None;
    Links time_links = Links::Normal;
};

// One row per switch, named by its dotted name (time.rom_wait): how to set it from text and read it
// back. See AesOptions.cpp for why the switches are a table. Three are also in manifest.json.
struct OptionRow {
    std::string key;
    std::vector<std::string> choices;  // For a number, its least and greatest. Empty for a path.
    std::function<bool(AesOptions&, const std::string&)> set;
    std::function<std::string(const AesOptions&)> get;
};
// The table of every switch, built on first use.
const std::vector<OptionRow>& option_table();

// Sets one switch from its name and its value as text. False for an unknown name or value, so
// that nothing is quietly left at its default.
bool set_option(AesOptions& options, const std::string& key, const std::string& value);
