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

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <utility>

namespace beebium {

// Motorola MC146818 real-time clock plus RAM (and its second sources, such as
// the RCA/Harris CDP6818 fitted to the Computech Integra-B).
//
// Register file (64 bytes, addressed by the low six bits of the address latch):
//   0x00-0x09  seconds, seconds alarm, minutes, minutes alarm, hours, hours
//              alarm, day of week (1 = Sunday), date, month, year (00-99)
//   0x0A       register A: UIP (read-only) | DV2-0 time base | RS3-0 rate
//   0x0B       register B: SET PIE AIE UIE SQWE DM 24/12 DSE
//   0x0C       register C: IRQF PF AF UF (read-only; reading clears it)
//   0x0D       register D: VRT (read-only; always "valid")
//   0x0E-0x3F  50 bytes of general-purpose battery-backed RAM
//
// Time model. The calendar follows a host clock - local civil time - plus an
// offset, like the Acorn user-port RTC extension, so the clock keeps real time
// across host sleep and emulation pauses. The offset is re-derived whenever
// the guest writes a time register, or when the clock is released from being
// held (SET, or the divider chain stopped), so guest-set times run on from
// where they were set. While held, the time registers are frozen snapshots
// that the guest may rewrite freely. The day-of-week register counts
// independently of the date on the real chip, so a guest-set weekday is kept
// as an adjustment relative to the computed weekday.
//
// Update-ended and alarm events fire when the (offset) host second rolls over,
// detected every HOST_POLL_CYCLES emulated cycles. The periodic interrupt is a
// sub-second rate the host clock cannot pace, so it is driven by emulated 2 MHz
// cycles through a 32.768 kHz divider chain.
//
// Simplifications: changing the DM or 24/12 mode bits re-encodes the time
// registers (the real chip leaves stale encodings until the next set); the
// daylight-saving enable (DSE) is stored but not acted on - local time already
// follows the host's daylight-saving rules; the square-wave output is not
// modelled (it is unconnected on the Integra-B).
class Mc146818Rtc {
public:
    // Host clock source: local civil time as microseconds since
    // 1970-01-01T00:00:00 local. Injectable so tests are deterministic.
    using HostClock = std::function<std::chrono::microseconds()>;

    // Battery-backed register file image (all 64 registers). Registers 0x00,
    // 0x02, 0x04, 0x06-0x09 (the running time) and 0x0C/0x0D (status) are
    // ignored on load.
    using BatteryImage = std::array<uint8_t, 64>;

    static constexpr uint32_t CPU_HZ = 2'000'000;
    static constexpr uint32_t TIME_BASE_HZ = 32'768;
    // How often (in emulated 2 MHz cycles) the host clock is sampled for a
    // second rollover: 1 ms.
    static constexpr uint32_t HOST_POLL_CYCLES = 2'000;

    static constexpr uint8_t REG_SECONDS = 0x00;
    static constexpr uint8_t REG_SECONDS_ALARM = 0x01;
    static constexpr uint8_t REG_MINUTES = 0x02;
    static constexpr uint8_t REG_MINUTES_ALARM = 0x03;
    static constexpr uint8_t REG_HOURS = 0x04;
    static constexpr uint8_t REG_HOURS_ALARM = 0x05;
    static constexpr uint8_t REG_DAY_OF_WEEK = 0x06;
    static constexpr uint8_t REG_DATE = 0x07;
    static constexpr uint8_t REG_MONTH = 0x08;
    static constexpr uint8_t REG_YEAR = 0x09;
    static constexpr uint8_t REG_A = 0x0A;
    static constexpr uint8_t REG_B = 0x0B;
    static constexpr uint8_t REG_C = 0x0C;
    static constexpr uint8_t REG_D = 0x0D;
    static constexpr uint8_t FIRST_RAM_REGISTER = 0x0E;
    static constexpr uint8_t REGISTER_COUNT = 0x40;

    static constexpr uint8_t A_UIP = 0x80;
    static constexpr uint8_t A_DV_MASK = 0x70;
    static constexpr uint8_t A_DV_32768HZ = 0x20;  // DV = 010
    static constexpr uint8_t A_RS_MASK = 0x0F;

    static constexpr uint8_t B_SET = 0x80;
    static constexpr uint8_t B_PIE = 0x40;
    static constexpr uint8_t B_AIE = 0x20;
    static constexpr uint8_t B_UIE = 0x10;
    static constexpr uint8_t B_SQWE = 0x08;
    static constexpr uint8_t B_DM_BINARY = 0x04;
    static constexpr uint8_t B_24_HOUR = 0x02;
    static constexpr uint8_t B_DSE = 0x01;

    static constexpr uint8_t C_IRQF = 0x80;
    static constexpr uint8_t C_PF = 0x40;
    static constexpr uint8_t C_AF = 0x20;
    static constexpr uint8_t C_UF = 0x10;

    static constexpr uint8_t D_VRT = 0x80;

    // UIP is high from 244 us before an update until the 1984 us update cycle
    // completes; the time registers may be read safely while it is low.
    static constexpr int64_t UIP_LEAD_US = 244;
    static constexpr int64_t UPDATE_CYCLE_US = 1984;

    // Broken-down civil time (binary, 24-hour; day_of_week 1 = Sunday).
    struct CivilTime {
        int year = 1970;
        int month = 1;
        int date = 1;
        int hours = 0;
        int minutes = 0;
        int seconds = 0;
        int day_of_week = 5;
    };

    explicit Mc146818Rtc(HostClock host_clock = host_local_civil_now)
        : host_clock_(std::move(host_clock)) {
        registers_[REG_A] = A_DV_32768HZ;
        registers_[REG_B] = B_24_HOUR;
        last_host_second_ = host_seconds();
    }

    // --- Bus interface ------------------------------------------------------

    // Address strobe: latch the register number (six bits decoded).
    void write_address(uint8_t value) { address_ = value & (REGISTER_COUNT - 1); }
    uint8_t address() const { return address_; }

    uint8_t read_data() {
        uint8_t value = peek_register(address_);
        if (address_ == REG_C) {
            registers_[REG_C] = 0;  // reading C clears all flags and IRQ
        }
        return value;
    }

    void write_data(uint8_t value) { write_register(address_, value); }

    // Read a register without side effects (register C is not cleared).
    uint8_t peek_register(uint8_t reg) const {
        reg &= REGISTER_COUNT - 1;
        switch (reg) {
            case REG_SECONDS:
            case REG_MINUTES:
            case REG_HOURS:
            case REG_DAY_OF_WEEK:
            case REG_DATE:
            case REG_MONTH:
            case REG_YEAR:
                return encode_time_register(reg, current_time());
            case REG_A:
                return static_cast<uint8_t>((registers_[REG_A] & 0x7F) |
                                            (update_in_progress() ? A_UIP : 0));
            case REG_C:
                return registers_[REG_C];
            case REG_D:
                return D_VRT;
            default:
                return registers_[reg];
        }
    }

    // --- Clocking -----------------------------------------------------------

    // Advance by one emulated 2 MHz CPU cycle.
    void tick() {
        if (divider_running()) {
            time_base_accumulator_ += TIME_BASE_HZ;
            if (time_base_accumulator_ >= CPU_HZ) {
                time_base_accumulator_ -= CPU_HZ;
                ++time_base_ticks_;
                uint32_t period = periodic_period_ticks(registers_[REG_A] & A_RS_MASK);
                if (period != 0 && (time_base_ticks_ % period) == 0) {
                    raise_flag(C_PF);
                }
            }
        }
        if (++host_poll_counter_ >= HOST_POLL_CYCLES) {
            host_poll_counter_ = 0;
            poll_host_clock();
        }
    }

    // IRQ output (active while IRQF is set).
    bool irq_pending() const { return (registers_[REG_C] & C_IRQF) != 0; }

    // The RESET pin: clears the interrupt enables and flags (PIE, AIE, UIE,
    // SQWE; IRQF, PF, AF, UF). Time, calendar, the rest of registers A/B, and
    // RAM are unaffected.
    void reset() {
        registers_[REG_B] &= static_cast<uint8_t>(~(B_PIE | B_AIE | B_UIE | B_SQWE));
        registers_[REG_C] = 0;
    }

    // --- Host-clock offset --------------------------------------------------

    // The calendar is host local time plus this offset.
    std::chrono::seconds clock_offset() const { return std::chrono::seconds(offset_seconds_); }
    void set_clock_offset(std::chrono::seconds offset) {
        offset_seconds_ = offset.count();
        last_host_second_ = host_seconds();
    }

    // --- Battery-backed state -----------------------------------------------

    BatteryImage battery_image() const {
        BatteryImage image{};
        for (uint8_t reg = 0; reg < REGISTER_COUNT; ++reg) image[reg] = peek_register(reg);
        return image;
    }

    void load_battery_image(const BatteryImage& image) {
        registers_[REG_SECONDS_ALARM] = image[REG_SECONDS_ALARM];
        registers_[REG_MINUTES_ALARM] = image[REG_MINUTES_ALARM];
        registers_[REG_HOURS_ALARM] = image[REG_HOURS_ALARM];
        registers_[REG_A] = image[REG_A] & 0x7F;
        registers_[REG_B] = image[REG_B];
        for (uint8_t reg = FIRST_RAM_REGISTER; reg < REGISTER_COUNT; ++reg) {
            registers_[reg] = image[reg];
        }
        frozen_ = false;
        if (clock_held()) {
            frozen_time_ = running_time();
            frozen_ = true;
        }
        last_host_second_ = host_seconds();
    }

    // --- Civil time arithmetic (public for tests) ----------------------------

    // Periodic interrupt period, in 32.768 kHz time-base ticks, for rate
    // select RS (0 = no periodic interrupt). With a 32.768 kHz time base RS=1
    // and RS=2 repeat the RS=8 and RS=9 rates.
    static constexpr uint32_t periodic_period_ticks(uint8_t rs) {
        if (rs == 0) return 0;
        if (rs <= 2) return 1u << (rs + 6);
        return 1u << (rs - 1);
    }

    static constexpr int64_t days_from_civil(int y, int m, int d) {
        y -= m <= 2;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const int64_t yoe = y - era * 400;
        const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    static constexpr int64_t civil_to_seconds(int y, int mo, int d, int h, int mi, int s) {
        return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
    }

    static constexpr CivilTime seconds_to_civil(int64_t t) {
        int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
        int64_t secs = t - days * 86400;
        CivilTime c;
        c.hours = static_cast<int>(secs / 3600);
        c.minutes = static_cast<int>((secs % 3600) / 60);
        c.seconds = static_cast<int>(secs % 60);
        // 1970-01-01 was a Thursday (day_of_week 5 with 1 = Sunday).
        int64_t wd = (days + 4) % 7;
        if (wd < 0) wd += 7;
        c.day_of_week = static_cast<int>(wd) + 1;
        const int64_t z = days + 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const int64_t doe = z - era * 146097;
        const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const int64_t mp = (5 * doy + 2) / 153;
        c.date = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
        c.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
        c.year = static_cast<int>(yoe + era * 400 + (c.month <= 2));
        return c;
    }

    // Default host clock: the host's local civil time.
    static std::chrono::microseconds host_local_civil_now() {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto us_since_epoch = duration_cast<microseconds>(now.time_since_epoch()).count();
        std::time_t tt = system_clock::to_time_t(now);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &tt);
#else
        localtime_r(&tt, &local);
#endif
        int64_t local_seconds = civil_to_seconds(local.tm_year + 1900, local.tm_mon + 1,
                                                 local.tm_mday, local.tm_hour,
                                                 local.tm_min, local.tm_sec);
        int64_t sub_second_us = us_since_epoch - static_cast<int64_t>(tt) * 1'000'000;
        return microseconds(local_seconds * 1'000'000 + sub_second_us);
    }

private:
    HostClock host_clock_;
    std::array<uint8_t, REGISTER_COUNT> registers_{};
    uint8_t address_ = 0;

    int64_t offset_seconds_ = 0;
    int day_of_week_adjust_ = 0;  // guest weekday minus computed weekday, mod 7

    bool frozen_ = false;
    CivilTime frozen_time_{};

    uint32_t time_base_accumulator_ = 0;
    uint64_t time_base_ticks_ = 0;
    uint32_t host_poll_counter_ = 0;
    int64_t last_host_second_ = 0;

    int64_t host_microseconds() const { return host_clock_().count(); }

    static int64_t floor_div(int64_t a, int64_t b) {
        return a >= 0 ? a / b : -((-a + b - 1) / b);
    }

    int64_t host_seconds() const { return floor_div(host_microseconds(), 1'000'000); }

    bool divider_running() const {
        return (registers_[REG_A] & A_DV_MASK) == A_DV_32768HZ;
    }

    // The clock does not advance while SET is held or the divider is stopped.
    bool clock_held() const {
        return (registers_[REG_B] & B_SET) != 0 || !divider_running();
    }

    CivilTime running_time() const {
        CivilTime t = seconds_to_civil(host_seconds() + offset_seconds_);
        t.day_of_week = ((t.day_of_week - 1 + day_of_week_adjust_) % 7 + 7) % 7 + 1;
        return t;
    }

    CivilTime current_time() const { return frozen_ ? frozen_time_ : running_time(); }

    bool update_in_progress() const {
        if (clock_held()) return false;
        int64_t frac = host_microseconds() - host_seconds() * 1'000'000;
        return frac >= 1'000'000 - UIP_LEAD_US || frac < UPDATE_CYCLE_US;
    }

    // Re-base the offset so the running clock shows `t` now.
    void rebase_to(const CivilTime& t) {
        // Registers hold a two-digit year; the chip's leap-year rule (every
        // fourth year) matches the Gregorian calendar throughout 2000-2099.
        int year = 2000 + (t.year % 100);
        int64_t target = civil_to_seconds(year, t.month, t.date, t.hours, t.minutes, t.seconds);
        offset_seconds_ = target - host_seconds();
        CivilTime computed = seconds_to_civil(target);
        day_of_week_adjust_ = ((t.day_of_week - computed.day_of_week) % 7 + 7) % 7;
        last_host_second_ = host_seconds();
    }

    void write_register(uint8_t reg, uint8_t value) {
        reg &= REGISTER_COUNT - 1;
        switch (reg) {
            case REG_SECONDS:
            case REG_MINUTES:
            case REG_HOURS:
            case REG_DAY_OF_WEEK:
            case REG_DATE:
            case REG_MONTH:
            case REG_YEAR: {
                CivilTime t = current_time();
                decode_time_register(reg, value, t);
                if (frozen_) {
                    frozen_time_ = t;
                } else {
                    rebase_to(t);
                }
                return;
            }
            case REG_A:
            case REG_B: {
                bool was_held = clock_held();
                CivilTime before = current_time();
                if (reg == REG_A) {
                    registers_[REG_A] = value & 0x7F;
                } else {
                    // Setting SET clears UIE.
                    if (value & B_SET) value &= static_cast<uint8_t>(~B_UIE);
                    registers_[REG_B] = value;
                    update_irqf();
                }
                bool now_held = clock_held();
                if (!was_held && now_held) {
                    frozen_time_ = before;
                    frozen_ = true;
                } else if (was_held && !now_held) {
                    frozen_ = false;
                    rebase_to(frozen_time_);
                }
                return;
            }
            case REG_C:
            case REG_D:
                return;  // read-only
            default:
                registers_[reg] = value;  // alarms and RAM
                return;
        }
    }

    // --- Register encoding ----------------------------------------------------

    bool binary_mode() const { return (registers_[REG_B] & B_DM_BINARY) != 0; }
    bool twenty_four_hour() const { return (registers_[REG_B] & B_24_HOUR) != 0; }

    uint8_t encode_value(int v) const {
        return binary_mode() ? static_cast<uint8_t>(v)
                             : static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
    }

    int decode_value(uint8_t v) const {
        return binary_mode() ? v : ((v >> 4) & 0x0F) * 10 + (v & 0x0F);
    }

    uint8_t encode_hours(int hours) const {
        if (twenty_four_hour()) return encode_value(hours);
        bool pm = hours >= 12;
        int h12 = hours % 12;
        if (h12 == 0) h12 = 12;
        return static_cast<uint8_t>(encode_value(h12) | (pm ? 0x80 : 0));
    }

    int decode_hours(uint8_t value) const {
        if (twenty_four_hour()) return decode_value(value);
        bool pm = (value & 0x80) != 0;
        int h12 = decode_value(value & 0x7F);
        return (h12 % 12) + (pm ? 12 : 0);
    }

    uint8_t encode_time_register(uint8_t reg, const CivilTime& t) const {
        switch (reg) {
            case REG_SECONDS: return encode_value(t.seconds);
            case REG_MINUTES: return encode_value(t.minutes);
            case REG_HOURS: return encode_hours(t.hours);
            case REG_DAY_OF_WEEK: return encode_value(t.day_of_week);
            case REG_DATE: return encode_value(t.date);
            case REG_MONTH: return encode_value(t.month);
            case REG_YEAR: return encode_value(t.year % 100);
            default: return 0;
        }
    }

    void decode_time_register(uint8_t reg, uint8_t value, CivilTime& t) const {
        switch (reg) {
            case REG_SECONDS: t.seconds = decode_value(value); break;
            case REG_MINUTES: t.minutes = decode_value(value); break;
            case REG_HOURS: t.hours = decode_hours(value); break;
            case REG_DAY_OF_WEEK: t.day_of_week = decode_value(value); break;
            case REG_DATE: t.date = decode_value(value); break;
            case REG_MONTH: t.month = decode_value(value); break;
            case REG_YEAR: t.year = 2000 + decode_value(value) % 100; break;
            default: break;
        }
    }

    // --- Interrupts -------------------------------------------------------------

    void raise_flag(uint8_t flag) {
        registers_[REG_C] |= flag;
        update_irqf();
    }

    void update_irqf() {
        uint8_t c = registers_[REG_C];
        uint8_t b = registers_[REG_B];
        bool irq = ((c & C_PF) && (b & B_PIE)) || ((c & C_AF) && (b & B_AIE)) ||
                   ((c & C_UF) && (b & B_UIE));
        registers_[REG_C] = static_cast<uint8_t>(irq ? (c | C_IRQF) : (c & ~C_IRQF));
    }

    static bool alarm_matches(uint8_t alarm, uint8_t current) {
        return (alarm & 0xC0) == 0xC0 || alarm == current;
    }

    void poll_host_clock() {
        int64_t now = host_seconds();
        if (now == last_host_second_) return;
        last_host_second_ = now;
        if (clock_held()) return;
        CivilTime t = running_time();
        if (alarm_matches(registers_[REG_SECONDS_ALARM], encode_value(t.seconds)) &&
            alarm_matches(registers_[REG_MINUTES_ALARM], encode_value(t.minutes)) &&
            alarm_matches(registers_[REG_HOURS_ALARM], encode_hours(t.hours))) {
            registers_[REG_C] |= C_AF;
        }
        raise_flag(C_UF);
    }
};

}  // namespace beebium
