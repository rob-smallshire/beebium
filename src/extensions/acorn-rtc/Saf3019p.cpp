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

#include "Saf3019p.hpp"

namespace beebium {

static constexpr uint8_t PB_DATA = 0x01;  // bit 0
static constexpr uint8_t PB_CLB  = 0x02;  // bit 1
static constexpr uint8_t PB_DLEN = 0x04;  // bit 2

Saf3019p::Saf3019p()
    : last_advance_time_(std::chrono::steady_clock::now()) {
    registers_.fill(0);
}

//////////////////////////////////////////////////////////////////////////////
// CBUS protocol
//////////////////////////////////////////////////////////////////////////////

void Saf3019p::write(uint8_t value) {
    // Detect rising edge on CLB (was low, now high) for read phase
    if (in_read_phase_ && (last_value_ & PB_CLB) == 0 && (value & PB_CLB) != 0) {
        // In read phase: shift out the next data bit on each CLB rising edge.
        // Per SAF3019P datasheet Fig. 3, DATA changes on CLB rising edge during TIME READ.
        data_output_bit_ = data_shift_register_ & 0x01;
        data_shift_register_ >>= 1;
    }

    // Detect falling edge on CLB (was high, now low) for address/write phases
    if ((last_value_ & PB_CLB) != 0 && (value & PB_CLB) == 0) {
        if (in_read_phase_) {
            // Read phase: falling edge does nothing (data shifted on rising edge)
        } else if ((value & PB_DLEN) != 0) {
            // DLEN high: shift DATA into command register
            command_shift_register_ = (command_shift_register_ >> 1)
                | ((value & PB_DATA) << 15);
            bit_count_++;
        } else {
            // DLEN low: end of transmission
            if (bit_count_ == 11) {
                // WRITE operation: 4 address bits + 7 data bits
                int cmd = command_shift_register_ >> 5;
                int reg = (cmd & 0x0F) >> 1;
                auto data = static_cast<uint8_t>(cmd >> 4);

                {
                    std::lock_guard lock(mutex_);
                    write_register(reg, data);
                }
                if (activity_callback_) {
                    activity_callback_(false, reg, data);
                }
            } else {
                // READ operation: extract address, load register into shift register
                int cmd = command_shift_register_ >> 12;
                int reg = (cmd & 0x0F) >> 1;

                uint8_t reg_value;
                {
                    std::lock_guard lock(mutex_);
                    reg_value = read_register(reg);
                    data_shift_register_ = reg_value;
                    // Enter read phase -- subsequent CLB edges shift out data bits
                    in_read_phase_ = true;
                    // First bit is immediately available
                    data_output_bit_ = data_shift_register_ & 0x01;
                    data_shift_register_ >>= 1;
                }
                if (activity_callback_) {
                    activity_callback_(true, reg, reg_value);
                }
            }
        }
    }
    last_value_ = value;
}

uint8_t Saf3019p::read_data_bit() const {
    return data_output_bit_;
}

void Saf3019p::reset_protocol() {
    bit_count_ = 0;
    command_shift_register_ = 0;
    in_read_phase_ = false;
    data_output_bit_ = 1;
}

//////////////////////////////////////////////////////////////////////////////
// Counter advancement
//////////////////////////////////////////////////////////////////////////////

void Saf3019p::advance_counters() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_advance_time_);

    if (elapsed.count() > 0) {
        last_advance_time_ = now;
        accumulated_seconds_ += static_cast<int>(elapsed.count());

        std::lock_guard lock(mutex_);
        while (accumulated_seconds_ >= 60) {
            accumulated_seconds_ -= 60;
            advance_one_minute();
        }
    }
}

void Saf3019p::advance_by_minutes(int minutes) {
    std::lock_guard lock(mutex_);
    for (int i = 0; i < minutes; i++) {
        advance_one_minute();
    }
}

void Saf3019p::advance_one_minute() {
    // Increment minutes (register 6)
    int min = from_bcd(registers_[6]);
    min++;
    if (min >= 60) {
        min = 0;
        // Carry to hours (register 4)
        int hour = from_bcd(registers_[4]);
        hour++;
        if (hour >= 24) {
            hour = 0;
            // Carry to date (register 2)
            int day = from_bcd(registers_[2]);
            day++;
            if (day > days_in_current_month()) {
                day = 1;
                // Carry to month (register 0)
                int month = from_bcd(registers_[0]);
                month++;
                if (month > 12) {
                    month = 1;
                    // No year carry -- the chip has no year counter
                }
                registers_[0] = to_bcd(month);
            }
            registers_[2] = to_bcd(day);
        }
        registers_[4] = to_bcd(hour);
    }
    registers_[6] = to_bcd(min);
}

int Saf3019p::days_in_current_month() const {
    // SAF3019P month-length table from datasheet Table 1.
    // February is ALWAYS 28 days -- the chip has no year awareness.
    int month = from_bcd(registers_[0]);
    switch (month) {
        case 2:
            return 28;  // Always 28 -- no leap year support in hardware
        case 4: case 6: case 9: case 11:
            return 30;
        default:
            return 31;
    }
}

//////////////////////////////////////////////////////////////////////////////
// Initialisation
//////////////////////////////////////////////////////////////////////////////

std::optional<RegisterLayout> register_layout_from_name(std::string_view name) {
    if (name == "4bit-year-in-r1") return RegisterLayout::FourBitYearInR1;
    if (name == "7bit-year-in-r7") return RegisterLayout::SevenBitYearInR7;
    if (name == "7bit-year-in-r1r5") return RegisterLayout::SevenBitYearInR1R5;
    return std::nullopt;
}

std::pair<int, int> Saf3019p::year_range(RegisterLayout layout) {
    // Ranges follow the year-storage width of each software convention:
    //   4-bit year offset in R1  -> 1981..1996
    //   7-bit 2-digit year in R7 -> 1981..2099
    //   7-bit year offset in R1R5-> 1981..2108
    switch (layout) {
        case RegisterLayout::FourBitYearInR1:
            return {1981, 1996};
        case RegisterLayout::SevenBitYearInR7:
            return {1981, 2099};
        case RegisterLayout::SevenBitYearInR1R5:
            return {1981, 2108};
    }
    return {1981, 1996};  // unreachable; keeps the compiler happy
}

bool Saf3019p::initialise(const DateTime& dt) {
    // Validate the year against the active layout's representable range.
    auto [min_year, max_year] = year_range(layout_);
    if (dt.year < min_year || dt.year > max_year) return false;

    std::lock_guard lock(mutex_);

    // Counter registers (same for all layouts)
    registers_[0] = to_bcd(dt.month);
    registers_[2] = to_bcd(dt.day);
    registers_[4] = to_bcd(dt.hour);
    registers_[6] = to_bcd(dt.minute);

    // Register 3: old month (low nibble) + leap flag (bit 4)
    // This mirrors what the FS's SETMFX routine computes.
    bool is_leap = (dt.year % 4 == 0);  // SAF3019P-era leap test: divisible by 4
    bool before_feb29 = (dt.month == 1) || (dt.month == 2 && dt.day < 29);
    uint8_t reg3 = to_bcd(dt.month);
    if (is_leap && before_feb29) {
        reg3 |= 0x10;  // Set leap flag in bit 4
    }
    registers_[3] = reg3;

    // Year and dongle registers depend on layout
    switch (layout_) {
        case RegisterLayout::FourBitYearInR1: {
            int year_offset = dt.year - 1981;
            registers_[1] = to_bcd(year_offset);  // BCD offset in reg 1
            registers_[5] = 0x00;                  // Unused
            registers_[7] = 0x00;                  // Dongle detection
            break;
        }
        case RegisterLayout::SevenBitYearInR7: {
            int two_digit_year = dt.year % 100;
            registers_[1] = 0x00;                  // Unused
            registers_[5] = 0x00;                  // Dongle detection
            registers_[7] = static_cast<uint8_t>(two_digit_year);
            break;
        }
        case RegisterLayout::SevenBitYearInR1R5: {
            // BeebMaster Y2KFIX: year = 1981 + reg1 + (reg5 * 16)
            int year_offset = dt.year - 1981;
            registers_[1] = to_bcd(year_offset & 0x0F);         // Low 4 bits
            registers_[5] = to_bcd((year_offset >> 4) & 0x07);  // High 3 bits
            registers_[7] = 0x00;                                 // Dongle detection
            break;
        }
    }

    // Reset prescaler
    accumulated_seconds_ = 0;
    last_advance_time_ = std::chrono::steady_clock::now();

    return true;
}

//////////////////////////////////////////////////////////////////////////////
// Register access
//////////////////////////////////////////////////////////////////////////////

uint8_t Saf3019p::read_register(int reg) const {
    if (reg < 0 || reg > 7) return 0;
    return registers_[static_cast<size_t>(reg)];
}

void Saf3019p::write_register(int reg, uint8_t bcd_value) {
    if (reg < 0 || reg > 7) return;

    switch (reg) {
        case 0:  // Month counter
        {
            // Strip the UC bit (bit 6) -- it controls the comparator mode,
            // not part of the stored counter value (SAF3019P datasheet Table 3).
            uint8_t data = bcd_value & 0x3F;
            if (data == 0) {
                // Prescaler reset: writing 0 to month resets seconds counter.
                // If accumulated seconds >= 30, increment minutes (coarse correction).
                if (accumulated_seconds_ >= 30) {
                    advance_one_minute();
                }
                accumulated_seconds_ = 0;
                return;  // Do not store 0 as month
            }
            registers_[0] = wrap_month_counter(data);
            break;
        }

        case 1:  // Month alarm (year storage)
            registers_[1] = wrap_month_alarm(bcd_value);
            break;

        case 2:  // Date counter
        case 3:  // Date alarm
        case 4:  // Hour counter
        case 5:  // Hour alarm
            registers_[static_cast<size_t>(reg)] = wrap_date_hour(bcd_value);
            break;

        case 6:  // Minute counter
        case 7:  // Minute alarm
            registers_[static_cast<size_t>(reg)] = wrap_minute(bcd_value);
            break;
    }
}

Saf3019p::DateTime Saf3019p::current_datetime() const {
    std::lock_guard lock(mutex_);

    int year = 0;
    switch (layout_) {
        case RegisterLayout::FourBitYearInR1:
            year = 1981 + from_bcd(registers_[1]);
            break;
        case RegisterLayout::SevenBitYearInR7: {
            int two_digit = registers_[7];
            year = (two_digit >= 81) ? (1900 + two_digit) : (2000 + two_digit);
            break;
        }
        case RegisterLayout::SevenBitYearInR1R5: {
            int year_offset = from_bcd(registers_[1]) + (from_bcd(registers_[5]) * 16);
            year = 1981 + year_offset;
            break;
        }
    }

    return {
        .year = year,
        .month = from_bcd(registers_[0]),
        .day = from_bcd(registers_[2]),
        .hour = from_bcd(registers_[4]),
        .minute = from_bcd(registers_[6])
    };
}

//////////////////////////////////////////////////////////////////////////////
// BCD wrapping (from BeebEm's empirical findings on real hardware)
//////////////////////////////////////////////////////////////////////////////

uint8_t Saf3019p::wrap_month_counter(uint8_t value) {
    // Register 0 (month counter): complex wrapping on raw byte values.
    // Empirically derived from real hardware (BeebEm).
    if (value == 0) return 0;  // prescaler reset (handled by caller)
    if (value >= 1 && value < 20) return value;
    if (value >= 20 && value < 60) return 19;
    if (value >= 60 && value < 80) return static_cast<uint8_t>(value - 60);
    if (value >= 80 && value < 100) return static_cast<uint8_t>(value - 80);
    if (value >= 100 && value < 120) return static_cast<uint8_t>(value - 100);
    if (value >= 120 && value < 160) return 19;
    if (value >= 160 && value < 180) return static_cast<uint8_t>(value - 160);
    if (value >= 180 && value < 200) return static_cast<uint8_t>(value - 180);
    if (value >= 200 && value < 220) return static_cast<uint8_t>(value - 200);
    return 19;  // >= 220
}

uint8_t Saf3019p::wrap_month_alarm(uint8_t value) {
    // Register 1 (month alarm / year): raw byte modulo 20.
    // Empirically derived from real hardware (BeebEm).
    return static_cast<uint8_t>(value % 20);
}

uint8_t Saf3019p::wrap_date_hour(uint8_t value) {
    // Registers 2-5 (date counter/alarm, hour counter/alarm):
    // raw byte wrapping with ranges. Empirically derived (BeebEm).
    if (value < 40) return value;
    if (value < 80) return static_cast<uint8_t>(value - 40);
    if (value < 100) return static_cast<uint8_t>(value - 80);
    if (value < 140) return static_cast<uint8_t>(value - 100);
    if (value < 180) return static_cast<uint8_t>(value - 140);
    if (value < 200) return static_cast<uint8_t>(value - 180);
    if (value < 240) return static_cast<uint8_t>(value - 200);
    return static_cast<uint8_t>(value - 240);
}

uint8_t Saf3019p::wrap_minute(uint8_t value) {
    // Registers 6-7 (minute counter/alarm): raw byte wrapping
    // with ranges. Empirically derived (BeebEm).
    if (value < 80) return value;
    if (value < 100) return static_cast<uint8_t>(value - 80);
    if (value < 180) return static_cast<uint8_t>(value - 100);
    if (value < 200) return static_cast<uint8_t>(value - 180);
    return static_cast<uint8_t>(value - 200);
}

//////////////////////////////////////////////////////////////////////////////
// BCD conversion
//////////////////////////////////////////////////////////////////////////////

uint8_t Saf3019p::to_bcd(int binary) {
    return static_cast<uint8_t>(((binary / 10) << 4) | (binary % 10));
}

int Saf3019p::from_bcd(uint8_t bcd) {
    return ((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F);
}

}  // namespace beebium
