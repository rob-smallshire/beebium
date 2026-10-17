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

#ifndef BEEBIUM_SAF3019P_HPP
#define BEEBIUM_SAF3019P_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

namespace beebium {

// Register layout describes how the BBC software uses the SAF3019P's alarm
// registers for year storage and dongle detection. The chip hardware is the
// same; only the software convention differs.
enum class RegisterLayout {
    // 4-bit year in register 1: BCD offset from 1981 (0-15).
    // Dongle detection on register 7. Register 5 unused.
    // Used by original Acorn FS (v0.90, v1.01-v1.07, v1.24).
    // Range: 1981-1996.
    FourBitYearInR1,

    // 7-bit year in register 7: binary 2-digit year (0-99).
    // Dongle detection on register 5. Register 1 unused.
    // Used by L3 FS v1.26 and similar revisions.
    // Range: 1981-2099.
    SevenBitYearInR7,

    // 7-bit year split across registers 1 and 5: offset from 1981,
    // low 4 bits in register 1, high 3 bits in register 5.
    // Reconstructed as: year = 1981 + reg1 + (reg5 * 16).
    // Dongle detection on register 7.
    // Used by BeebMaster Y2KFIX. Range: 1981-2108.
    SevenBitYearInR1R5,
};

// The layout a manifest `layout` value names ("4bit-year-in-r1",
// "7bit-year-in-r7", "7bit-year-in-r1r5"), or nullopt for any other value.
std::optional<RegisterLayout> register_layout_from_name(std::string_view name);

// Faithful emulation of the Signetics/Philips SAF3019P CMOS clock/timer IC.
//
// The chip provides:
//   - 4 time counter registers (month, date, hours, minutes) in BCD
//   - 4 alarm/storage registers (repurposed by Acorn for year, old month, etc.)
//   - CBUS serial protocol for register access
//   - Internal counter advancement with specific month-length rules
//     (February is always 28 days -- no year awareness)
//
// Counters advance at wall-clock rate via advance_counters(), or
// deterministically via advance_by_minutes() for testing.
class Saf3019p {
public:
    struct DateTime {
        int year;    // full year, e.g. 1985
        int month;   // 1-12
        int day;     // 1-31
        int hour;    // 0-23
        int minute;  // 0-59
    };

    Saf3019p();

    // --- CBUS protocol interface ---

    // Called on each VIA Port B write. Detects falling CLB edges,
    // shifts bits, and completes read/write operations on DLEN going low.
    void write(uint8_t value);

    // Returns the current DATA output bit (PB0 when BBC reads).
    // Does NOT shift -- shifting happens inside write() on CLB edges.
    uint8_t read_data_bit() const;

    // Reset the CBUS protocol state machine. Called when DDRB is
    // programmed with bits 0-2 as outputs (the start of a new command).
    void reset_protocol();

    // --- Counter advancement ---

    // Advance counters based on elapsed wall-clock time since last call.
    void advance_counters();

    // Advance counters by a specific number of minutes (for testing).
    void advance_by_minutes(int minutes);

    // --- Register layout ---

    void set_layout(RegisterLayout layout) { layout_ = layout; }
    RegisterLayout layout() const { return layout_; }

    // The inclusive [min_year, max_year] full-year range representable under a
    // given register layout. Single source of truth for both initialise()'s
    // validation and callers that report the valid range (e.g. SetTime).
    static std::pair<int, int> year_range(RegisterLayout layout);

    // --- Startup initialisation ---

    // Initialise all 8 registers from a DateTime using the current layout.
    // Returns false if the year cannot be represented.
    //   Acorn: year offset 0-19 in reg 1 (BCD), valid 1981-2000
    //   V126:  year offset 0-99 in reg 7 (binary), valid 1981-2080
    bool initialise(const DateTime& dt);

    // --- Register access (for gRPC / debugging) ---

    uint8_t read_register(int reg) const;
    void write_register(int reg, uint8_t bcd_value);

    // What the battery keeps: the eight registers, as they are, with none of
    // the side effects a write through the bus has.
    std::array<uint8_t, 8> battery_image() const {
        std::lock_guard lock(mutex_);
        return registers_;
    }
    void load_battery_image(const std::array<uint8_t, 8>& registers) {
        std::lock_guard lock(mutex_);
        registers_ = registers;
    }

    // Decode current register state into a DateTime.
    DateTime current_datetime() const;

    // --- Activity callback ---

    // Called when a CBUS register read or write completes.
    // is_read: true for read, false for write.
    // reg: register number (0-7).
    // value: BCD register value.
    using ActivityCallback = std::function<void(bool is_read, int reg, uint8_t value)>;

    void set_activity_callback(ActivityCallback cb) { activity_callback_ = std::move(cb); }

    // --- Prescaler access (for testing) ---

    int accumulated_seconds() const { return accumulated_seconds_; }
    void set_accumulated_seconds(int seconds) { accumulated_seconds_ = seconds; }

private:
    // CBUS protocol state machine
    uint8_t last_value_ = 0xFF;
    int bit_count_ = 0;
    int command_shift_register_ = 0;
    int data_shift_register_ = 0;
    uint8_t data_output_bit_ = 1;  // current DATA pin state (PB0 output)
    bool in_read_phase_ = false;   // true after TIME ADDRESS triggers a read

    // Register layout (software convention for year/dongle storage)
    RegisterLayout layout_ = RegisterLayout::FourBitYearInR1;

    // 8 BCD registers
    std::array<uint8_t, 8> registers_{};

    // Wall-clock tracking for counter advancement
    std::chrono::steady_clock::time_point last_advance_time_;
    int accumulated_seconds_ = 0;

    // Thread safety for register access (gRPC vs emulation thread)
    mutable std::mutex mutex_;

    // Activity callback (called outside mutex lock)
    ActivityCallback activity_callback_;

    // Internal counter advancement
    void advance_one_minute();
    int days_in_current_month() const;

    // BCD wrapping per register type (matching BeebEm's empirical findings)
    static uint8_t wrap_month_counter(uint8_t value);   // reg 0
    static uint8_t wrap_month_alarm(uint8_t value);     // reg 1
    static uint8_t wrap_date_hour(uint8_t value);       // regs 2-5
    static uint8_t wrap_minute(uint8_t value);           // regs 6-7

    static uint8_t to_bcd(int binary);
    static int from_bcd(uint8_t bcd);
};

}  // namespace beebium

#endif  // BEEBIUM_SAF3019P_HPP
