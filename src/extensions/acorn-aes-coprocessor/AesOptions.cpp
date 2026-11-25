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
// AesOptions.cpp, the table of switches, by name
// version 2.0.0, 20 November 2026.

// The table of switches, by name: sets and checks each AesOptions field (AesOptions.hpp) from a
// name and a value. Three are Beebium settings (manifest.json); the rest change by editing a
// default.
//
// Why a table: every switch is one row, so the plugin's settings
// (AesExtension.cpp), set_option and the tests all use one list, and a name or value not in it
// is refused rather than ignored. `choice` makes the row for a switch with named values,
// `number` the row for a number with limits, and `path` the row for the ROM; each row holds a
// function to set the field from text and one to read it back as text. FIELD(name), below, is
// shorthand for a small function that finds that field in an AesOptions.

#include "AesOptions.hpp"

#include <charconv>
#include <utility>

namespace {

// Short names for the two option structs, so that the table below stays readable.
using A = AesOptions;
using C = ArmOptions;

// A switch's choices: each name a user types, with the value it stands for.
template <class Field>
using Names = std::vector<std::pair<std::string, Field>>;

// A row for a switch with a fixed list of named values. `field` finds the switch in the options;
// `names` pairs each name a user types with the value it stands for. get turns the value back
// into its name, which the tests use to check that every default would itself be accepted.
template <class Field>
OptionRow choice(const std::string& key, Field& (*field)(A&), Names<Field> names) {
    std::vector<std::string> choices;
    for (const auto& [name, value] : names) choices.push_back(name);
    const auto get = [field, names](const A& options) {
        const Field now = field(const_cast<A&>(options));
        for (const auto& [name, value] : names) {
            if (value == now) return name;
        }
        return std::string();
    };
    const auto set = [field, names](A& options, const std::string& text) {
        for (const auto& [name, value] : names) {
            if (name == text) {
                field(options) = value;
                return true;
            }
        }
        return false;
    };
    return {key, choices, set, get};
}

// A row for a number, refused outside its limits. The limits stop values that would break the
// emulator: time.refresh_period must not be 0, or AesTiming's refresh count would never end.
OptionRow number(const std::string& key, int& (*field)(A&), int low, int high) {
    const auto get = [field](const A& options) { return std::to_string(field(const_cast<A&>(options))); };
    const auto set = [field, low, high](A& options, const std::string& text) {
        int value = 0;
        const char* end = text.data() + text.size();
        const auto [at, error] = std::from_chars(text.data(), end, value);
        if (error != std::errc() || at != end || value < low || value > high) return false;
        field(options) = value;
        return true;
    };
    return {key, {std::to_string(low), std::to_string(high)}, set, get};
}

// The row for the rom setting: a file path, taken as given. AesMemory checks the file when it
// loads it.
OptionRow path(const std::string& key) {
    const auto get = [](const A& options) { return options.rom; };
    const auto set = [](A& options, const std::string& text) {
        options.rom = text;
        return true;
    };
    return {key, {}, set, get};
}

// The choices of a true-or-false switch.
const Names<bool> kBool = {{"true", true}, {"false", false}};

}  // namespace

// The table itself. The keys are the switches' dotted names: time.rom_wait for time_rom_wait.
// Beebium offers three of these (manifest.json); the rest are changed by editing their defaults.
// A behaviour with no alternative in the code, such as ram.absent_write, has no row: there is
// nothing to choose. The table is built once, on first use. FIELD(name) makes the small function
// that finds that field in an AesOptions.
#define FIELD(name) [](A& o) -> auto& { return o.name; }
const std::vector<OptionRow>& option_table() {
    static const std::vector<OptionRow> table = {
        choice<int>("ram", FIELD(ram), {{"2", 2}, {"4", 4}}),
        path("rom"),
        choice<A::MachineAbort>("abort", FIELD(abort), {{"off", A::MachineAbort::Off}, {"strict", A::MachineAbort::Strict}}),
        choice<A::MachineCheck>("access.check", FIELD(access_check), {{"off", A::MachineCheck::Off}, {"log", A::MachineCheck::Log}, {"abort", A::MachineCheck::Abort}}),

        choice<C::RorRegCarry>("cpu.ror_reg_carry", FIELD(cpu.ror_reg_carry), {{"real-aes", C::RorRegCarry::RealAes}, {"zero-above-31", C::RorRegCarry::ZeroAbove31}, {"zero-at-multiples-of-32", C::RorRegCarry::ZeroAtMultiplesOf32}, {"arm2", C::RorRegCarry::Arm2}}),
        choice<C::EncABit7>("cpu.enc_a_bit7", FIELD(cpu.enc_a_bit7), {{"ignored", C::EncABit7::Ignored}, {"traps", C::EncABit7::Traps}}),
        choice<C::WordUnaligned>("cpu.word_unaligned", FIELD(cpu.word_unaligned), {{"ignore-low-bits", C::WordUnaligned::IgnoreLowBits}, {"rotate", C::WordUnaligned::Rotate}}),

        choice<C::ModeDelay>("cpu.mode_delay", FIELD(cpu.mode_delay), {{"reads-old-writes-new", C::ModeDelay::ReadsOldWritesNew}, {"old-bank-whole-instruction", C::ModeDelay::OldBankWholeInstruction}, {"none", C::ModeDelay::None}}),
        choice<bool>("cpu.ldm_user_bank", FIELD(cpu.ldm_user_bank), kBool),
        choice<C::LdmPcWb>("cpu.ldm_pc_wb", FIELD(cpu.ldm_pc_wb), {{"user-bank", C::LdmPcWb::UserBank}, {"current-bank", C::LdmPcWb::CurrentBank}}),
        choice<C::ResetR14>("cpu.reset_r14", FIELD(cpu.reset_r14), {{"r15-minus-4", C::ResetR14::R15Minus4}, {"r15", C::ResetR14::R15}}),

        choice<bool>("adx.store_to_load", FIELD(cpu.adx_store_to_load), kBool),
        choice<C::LdmFaultLoads>("ldm.fault_loads", FIELD(cpu.ldm_fault_loads), {{"complete", C::LdmFaultLoads::Complete}, {"stop-overwriting", C::LdmFaultLoads::StopOverwriting}}),

        choice<A::RamRepeat>("ram.repeat", FIELD(ram_repeat), {{"address-lines", A::RamRepeat::AddressLines}, {"fitted-size", A::RamRepeat::FittedSize}}),
        choice<A::RamPowerOn>("ram.power_on", FIELD(ram_power_on), {{"zero", A::RamPowerOn::Zero}, {"random", A::RamPowerOn::Random}}),
        choice<A::RamAbsentBank>("ram.absent_bank", FIELD(ram_absent_bank), {{"all-ones", A::RamAbsentBank::AllOnes}, {"last-bus", A::RamAbsentBank::LastBus}, {"low-ram", A::RamAbsentBank::LowRam}}),
        choice<A::BootLatch>("boot.latch", FIELD(boot_latch), {{"any-write", A::BootLatch::AnyWrite}, {"ram-writes-only", A::BootLatch::RamWritesOnly}}),
        choice<A::Region10Read>("region10.read", FIELD(region10_read), {{"nothing", A::Region10Read::Nothing}, {"abort", A::Region10Read::Abort}, {"ram-repeat", A::Region10Read::RamRepeat}}),
        choice<A::Region10Write>("region10.write", FIELD(region10_write), {{"discard", A::Region10Write::Discard}, {"ram-repeat", A::Region10Write::RamRepeat}}),
        choice<A::TubeAddrDecode>("tube.addr_decode", FIELD(tube_addr_decode), {{"lower-half", A::TubeAddrDecode::LowerHalf}, {"whole-region", A::TubeAddrDecode::WholeRegion}}),
        choice<A::TubeWidth>("tube.width", FIELD(tube_width), {{"low-byte", A::TubeWidth::LowByte}, {"undefined", A::TubeWidth::Undefined}}),
        choice<A::TubePrstLevel>("tube.prst_level", FIELD(tube_prst_level), {{"holds", A::TubePrstLevel::Holds}, {"pulse", A::TubePrstLevel::Pulse}}),

        choice<C::RegShiftNext>("time.reg_shift_next", FIELD(cpu.time_reg_shift_next), {{"n-fetch", C::RegShiftNext::NFetch}, {"two-internal", C::RegShiftNext::TwoInternal}}),
        choice<C::LdrRegShiftTime>("time.ldr_reg_shift", FIELD(cpu.time_ldr_reg_shift), {{"one-s", C::LdrRegShiftTime::OneS}, {"none", C::LdrRegShiftTime::None}}),
        choice<A::BlockBreak>("time.block_break", FIELD(time_block_break), {{"after-last-word", A::BlockBreak::AfterLastWord}, {"at-block-start", A::BlockBreak::AtBlockStart}, {"none", A::BlockBreak::None}}),
        number("time.refresh_period", FIELD(time_refresh_period), 1, 100000),
        number("time.refresh_cost", FIELD(time_refresh_cost), 0, 1000),
        number("time.rom_wait", FIELD(time_rom_wait), 0, 1000),
        number("time.tube_wait", FIELD(time_tube_wait), 0, 1000),
        choice<A::TubeSync>("time.tube_sync", FIELD(time_tube_sync), {{"none", A::TubeSync::None}, {"next-host-edge", A::TubeSync::NextHostEdge}}),
        choice<A::Links>("time.links", FIELD(time_links), {{"normal", A::Links::Normal}, {"seq-tied", A::Links::SeqTied}}),
    };
    return table;
}
#undef FIELD

// Sets one switch from its name and its value as text. False for an unknown name or a refused
// value, so that the caller can report it instead of quietly leaving the default.
bool set_option(AesOptions& options, const std::string& key, const std::string& value) {
    for (const OptionRow& row : option_table()) {
        if (row.key == key) return row.set(options, value);
    }
    return false;
}
