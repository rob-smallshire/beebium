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

// Tests for the MC146818 / CDP6818 real-time clock (as fitted to the Integra-B).
//
// The calendar follows a host clock (injected here, so the tests are
// deterministic) plus an offset that guest writes establish. The periodic
// interrupt is driven by emulated 2 MHz cycles through a 32.768 kHz divider.

#include <beebium/devices/Mc146818Rtc.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>

using namespace beebium;
using std::chrono::microseconds;
using std::chrono::seconds;

namespace {

// A controllable host clock: local civil time in microseconds since
// 1970-01-01T00:00:00 (local).
struct FakeHostClock {
    int64_t us = 0;
    Mc146818Rtc::HostClock source() {
        return [this] { return microseconds(us); };
    }
    void set(int y, int mo, int d, int h, int mi, int s, int64_t frac_us = 0) {
        us = Mc146818Rtc::civil_to_seconds(y, mo, d, h, mi, s) * 1'000'000 + frac_us;
    }
    void advance_us(int64_t delta) { us += delta; }
};

constexpr uint8_t REG_SECONDS = 0x00;
constexpr uint8_t REG_SECONDS_ALARM = 0x01;
constexpr uint8_t REG_MINUTES = 0x02;
constexpr uint8_t REG_MINUTES_ALARM = 0x03;
constexpr uint8_t REG_HOURS = 0x04;
constexpr uint8_t REG_HOURS_ALARM = 0x05;
constexpr uint8_t REG_DAY_OF_WEEK = 0x06;
constexpr uint8_t REG_DATE = 0x07;
constexpr uint8_t REG_MONTH = 0x08;
constexpr uint8_t REG_YEAR = 0x09;
constexpr uint8_t REG_A = 0x0A;
constexpr uint8_t REG_B = 0x0B;
constexpr uint8_t REG_C = 0x0C;
constexpr uint8_t REG_D = 0x0D;

constexpr uint8_t B_SET = 0x80, B_PIE = 0x40, B_AIE = 0x20, B_UIE = 0x10;
constexpr uint8_t B_SQWE = 0x08, B_DM_BINARY = 0x04, B_24H = 0x02;
constexpr uint8_t C_IRQF = 0x80, C_PF = 0x40, C_AF = 0x20, C_UF = 0x10;
constexpr uint8_t A_UIP = 0x80, A_DV_32K = 0x20;

uint8_t rd(Mc146818Rtc& rtc, uint8_t reg) {
    rtc.write_address(reg);
    return rtc.read_data();
}

void wr(Mc146818Rtc& rtc, uint8_t reg, uint8_t value) {
    rtc.write_address(reg);
    rtc.write_data(value);
}

// Run the RTC for the given number of 2 MHz cycles.
void run_cycles(Mc146818Rtc& rtc, uint64_t cycles) {
    for (uint64_t i = 0; i < cycles; ++i) rtc.tick();
}

// A running clock: 32.768 kHz time base, BCD, 24-hour.
void start_running(Mc146818Rtc& rtc, uint8_t b = B_24H) {
    wr(rtc, REG_A, A_DV_32K);
    wr(rtc, REG_B, b);
}

}  // namespace

TEST_CASE("Mc146818Rtc civil time helpers round-trip", "[mc146818]") {
    auto s = Mc146818Rtc::civil_to_seconds(2026, 9, 23, 17, 45, 30);
    auto f = Mc146818Rtc::seconds_to_civil(s);
    CHECK(f.year == 2026);
    CHECK(f.month == 9);
    CHECK(f.date == 23);
    CHECK(f.hours == 17);
    CHECK(f.minutes == 45);
    CHECK(f.seconds == 30);
    CHECK(f.day_of_week == 4);  // Wednesday; 1 = Sunday
    CHECK(Mc146818Rtc::civil_to_seconds(1970, 1, 1, 0, 0, 0) == 0);
    CHECK(Mc146818Rtc::seconds_to_civil(0).day_of_week == 5);  // Thursday
}

TEST_CASE("Mc146818Rtc reads the host time in BCD 24-hour mode", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 17, 45, 30);
    Mc146818Rtc rtc(host.source());
    start_running(rtc);

    CHECK(rd(rtc, REG_SECONDS) == 0x30);
    CHECK(rd(rtc, REG_MINUTES) == 0x45);
    CHECK(rd(rtc, REG_HOURS) == 0x17);
    CHECK(rd(rtc, REG_DAY_OF_WEEK) == 0x04);
    CHECK(rd(rtc, REG_DATE) == 0x23);
    CHECK(rd(rtc, REG_MONTH) == 0x09);
    CHECK(rd(rtc, REG_YEAR) == 0x26);

    host.advance_us(61'000'000);
    CHECK(rd(rtc, REG_SECONDS) == 0x31);
    CHECK(rd(rtc, REG_MINUTES) == 0x46);
}

TEST_CASE("Mc146818Rtc binary data mode", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 12, 31, 23, 59, 58);
    Mc146818Rtc rtc(host.source());
    start_running(rtc, B_24H | B_DM_BINARY);

    CHECK(rd(rtc, REG_SECONDS) == 58);
    CHECK(rd(rtc, REG_HOURS) == 23);
    CHECK(rd(rtc, REG_DATE) == 31);
    CHECK(rd(rtc, REG_MONTH) == 12);
    CHECK(rd(rtc, REG_YEAR) == 26);
}

TEST_CASE("Mc146818Rtc 12-hour mode sets the PM flag in bit 7", "[mc146818]") {
    FakeHostClock host;
    Mc146818Rtc rtc(host.source());

    SECTION("afternoon, BCD") {
        host.set(2026, 1, 1, 13, 0, 0);
        start_running(rtc, 0);
        CHECK(rd(rtc, REG_HOURS) == (0x80 | 0x01));
    }
    SECTION("midnight reads as 12 AM") {
        host.set(2026, 1, 1, 0, 30, 0);
        start_running(rtc, 0);
        CHECK(rd(rtc, REG_HOURS) == 0x12);
    }
    SECTION("noon reads as 12 PM") {
        host.set(2026, 1, 1, 12, 30, 0);
        start_running(rtc, 0);
        CHECK(rd(rtc, REG_HOURS) == (0x80 | 0x12));
    }
    SECTION("evening, binary") {
        host.set(2026, 1, 1, 23, 0, 0);
        start_running(rtc, B_DM_BINARY);
        CHECK(rd(rtc, REG_HOURS) == (0x80 | 11));
    }
}

TEST_CASE("Mc146818Rtc guest-set time runs on from the host clock", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 17, 45, 30);
    Mc146818Rtc rtc(host.source());
    start_running(rtc);

    // Set the clock to 13:33:35 15 Sep 1988 as the IBOS guide does: SET, write,
    // release SET.
    wr(rtc, REG_B, B_SET | B_24H);
    wr(rtc, REG_SECONDS, 0x35);
    wr(rtc, REG_MINUTES, 0x33);
    wr(rtc, REG_HOURS, 0x13);
    wr(rtc, REG_DATE, 0x15);
    wr(rtc, REG_MONTH, 0x09);
    wr(rtc, REG_YEAR, 0x88);
    wr(rtc, REG_DAY_OF_WEEK, 0x05);  // Thursday

    // While SET is held the registers do not advance.
    host.advance_us(5'000'000);
    CHECK(rd(rtc, REG_SECONDS) == 0x35);

    wr(rtc, REG_B, B_24H);
    CHECK(rd(rtc, REG_SECONDS) == 0x35);
    CHECK(rd(rtc, REG_YEAR) == 0x88);

    host.advance_us(2'000'000);
    CHECK(rd(rtc, REG_SECONDS) == 0x37);
    CHECK(rd(rtc, REG_MINUTES) == 0x33);
    CHECK(rd(rtc, REG_DATE) == 0x15);
    CHECK(rd(rtc, REG_DAY_OF_WEEK) == 0x05);

    // Crossing midnight advances the guest's day of week from what it set.
    host.advance_us(int64_t{11} * 3600 * 1'000'000);
    CHECK(rd(rtc, REG_DATE) == 0x16);
    CHECK(rd(rtc, REG_DAY_OF_WEEK) == 0x06);
}

TEST_CASE("Mc146818Rtc writing a time register while running re-bases the clock", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 17, 45, 30);
    Mc146818Rtc rtc(host.source());
    start_running(rtc);

    wr(rtc, REG_MINUTES, 0x10);
    CHECK(rd(rtc, REG_MINUTES) == 0x10);
    CHECK(rd(rtc, REG_SECONDS) == 0x30);
    host.advance_us(60'000'000);
    CHECK(rd(rtc, REG_MINUTES) == 0x11);
}

TEST_CASE("Mc146818Rtc a stopped divider freezes the clock", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 17, 45, 30);
    Mc146818Rtc rtc(host.source());
    start_running(rtc);

    wr(rtc, REG_A, 0x70);  // DV = 111: divider chain held in reset
    host.advance_us(10'000'000);
    CHECK(rd(rtc, REG_SECONDS) == 0x30);

    wr(rtc, REG_A, A_DV_32K);
    host.advance_us(1'000'000);
    CHECK(rd(rtc, REG_SECONDS) == 0x31);
}

TEST_CASE("Mc146818Rtc clock offset shifts the calendar", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 17, 45, 30);
    Mc146818Rtc rtc(host.source());
    start_running(rtc);

    rtc.set_clock_offset(seconds(-3600));
    CHECK(rd(rtc, REG_HOURS) == 0x16);
    CHECK(rtc.clock_offset() == seconds(-3600));
}

TEST_CASE("Mc146818Rtc register A update-in-progress flag", "[mc146818]") {
    FakeHostClock host;
    Mc146818Rtc rtc(host.source());
    start_running(rtc);

    host.set(2026, 1, 1, 0, 0, 0, 500'000);
    CHECK((rd(rtc, REG_A) & A_UIP) == 0);
    host.set(2026, 1, 1, 0, 0, 0, 999'900);  // within 244 us of the update
    CHECK((rd(rtc, REG_A) & A_UIP) != 0);
    host.set(2026, 1, 1, 0, 0, 1, 1'000);    // update cycle still running
    CHECK((rd(rtc, REG_A) & A_UIP) != 0);
    host.set(2026, 1, 1, 0, 0, 1, 3'000);
    CHECK((rd(rtc, REG_A) & A_UIP) == 0);

    // The writable bits of register A read back.
    wr(rtc, REG_A, A_DV_32K | 0x06);
    CHECK((rd(rtc, REG_A) & 0x7F) == (A_DV_32K | 0x06));
}

TEST_CASE("Mc146818Rtc register D reports valid RAM and time", "[mc146818]") {
    FakeHostClock host;
    Mc146818Rtc rtc(host.source());
    CHECK(rd(rtc, REG_D) == 0x80);
    wr(rtc, REG_D, 0x00);
    CHECK(rd(rtc, REG_D) == 0x80);
}

TEST_CASE("Mc146818Rtc has 50 bytes of general-purpose RAM", "[mc146818]") {
    FakeHostClock host;
    Mc146818Rtc rtc(host.source());
    for (uint8_t reg = 0x0E; reg < 0x40; ++reg) {
        wr(rtc, reg, static_cast<uint8_t>(reg ^ 0xA5));
    }
    for (uint8_t reg = 0x0E; reg < 0x40; ++reg) {
        CHECK(rd(rtc, reg) == static_cast<uint8_t>(reg ^ 0xA5));
    }
    // Only six address bits are decoded.
    wr(rtc, 0x0E, 0x12);
    CHECK(rd(rtc, 0x4E) == 0x12);
    CHECK(rd(rtc, 0xCE) == 0x12);
}

TEST_CASE("Mc146818Rtc update-ended interrupt", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 17, 45, 30, 100'000);
    Mc146818Rtc rtc(host.source());
    start_running(rtc, B_24H | B_UIE);
    CHECK_FALSE(rtc.irq_pending());

    host.advance_us(950'000);  // cross the second boundary
    run_cycles(rtc, Mc146818Rtc::HOST_POLL_CYCLES);
    CHECK(rtc.irq_pending());

    uint8_t c = rd(rtc, REG_C);
    CHECK((c & (C_IRQF | C_UF)) == (C_IRQF | C_UF));
    // Reading register C clears it and releases IRQ.
    CHECK(rd(rtc, REG_C) == 0);
    CHECK_FALSE(rtc.irq_pending());
}

TEST_CASE("Mc146818Rtc update flag is set without the enable but raises no IRQ", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 17, 45, 30, 100'000);
    Mc146818Rtc rtc(host.source());
    start_running(rtc);

    host.advance_us(950'000);
    run_cycles(rtc, Mc146818Rtc::HOST_POLL_CYCLES);
    CHECK_FALSE(rtc.irq_pending());
    CHECK(rd(rtc, REG_C) == C_UF);
}

TEST_CASE("Mc146818Rtc no update events while SET is held", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 17, 45, 30, 100'000);
    Mc146818Rtc rtc(host.source());
    start_running(rtc, B_SET | B_24H);

    host.advance_us(3'000'000);
    run_cycles(rtc, Mc146818Rtc::HOST_POLL_CYCLES);
    CHECK(rd(rtc, REG_C) == 0);
}

TEST_CASE("Mc146818Rtc setting SET clears the update interrupt enable", "[mc146818]") {
    FakeHostClock host;
    Mc146818Rtc rtc(host.source());
    start_running(rtc, B_24H | B_UIE);
    wr(rtc, REG_B, B_SET | B_24H | B_UIE);
    CHECK((rd(rtc, REG_B) & B_UIE) == 0);
}

TEST_CASE("Mc146818Rtc alarm interrupt", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 7, 29, 59, 100'000);
    Mc146818Rtc rtc(host.source());
    start_running(rtc, B_24H | B_AIE);

    wr(rtc, REG_HOURS_ALARM, 0x07);
    wr(rtc, REG_MINUTES_ALARM, 0x30);
    wr(rtc, REG_SECONDS_ALARM, 0x00);

    host.advance_us(1'000'000);  // 07:30:00
    run_cycles(rtc, Mc146818Rtc::HOST_POLL_CYCLES);
    CHECK(rtc.irq_pending());
    CHECK((rd(rtc, REG_C) & (C_IRQF | C_AF)) == (C_IRQF | C_AF));

    host.advance_us(1'000'000);  // 07:30:01 - no match
    run_cycles(rtc, Mc146818Rtc::HOST_POLL_CYCLES);
    CHECK_FALSE(rtc.irq_pending());
    CHECK((rd(rtc, REG_C) & C_AF) == 0);
}

TEST_CASE("Mc146818Rtc alarm don't-care codes match any value", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 7, 29, 59, 100'000);
    Mc146818Rtc rtc(host.source());
    start_running(rtc, B_24H | B_AIE);

    // Once a minute, at second 0.
    wr(rtc, REG_HOURS_ALARM, 0xC0);
    wr(rtc, REG_MINUTES_ALARM, 0xFF);
    wr(rtc, REG_SECONDS_ALARM, 0x00);

    host.advance_us(1'000'000);
    run_cycles(rtc, Mc146818Rtc::HOST_POLL_CYCLES);
    CHECK((rd(rtc, REG_C) & C_AF) != 0);

    host.advance_us(60'000'000);
    run_cycles(rtc, Mc146818Rtc::HOST_POLL_CYCLES);
    CHECK((rd(rtc, REG_C) & C_AF) != 0);
}

TEST_CASE("Mc146818Rtc periodic interrupt follows emulated cycles", "[mc146818]") {
    FakeHostClock host;
    Mc146818Rtc rtc(host.source());

    // RS = 1111: 500 ms period = 1,000,000 cycles at 2 MHz.
    wr(rtc, REG_A, A_DV_32K | 0x0F);
    wr(rtc, REG_B, B_24H | B_PIE);
    (void)rd(rtc, REG_C);

    run_cycles(rtc, 990'000);
    CHECK_FALSE(rtc.irq_pending());
    run_cycles(rtc, 20'000);
    CHECK(rtc.irq_pending());
    CHECK((rd(rtc, REG_C) & (C_IRQF | C_PF)) == (C_IRQF | C_PF));
    CHECK_FALSE(rtc.irq_pending());
}

TEST_CASE("Mc146818Rtc periodic rate select", "[mc146818]") {
    CHECK(Mc146818Rtc::periodic_period_ticks(0) == 0);    // none
    CHECK(Mc146818Rtc::periodic_period_ticks(1) == 128);  // 3.90625 ms
    CHECK(Mc146818Rtc::periodic_period_ticks(2) == 256);  // 7.8125 ms
    CHECK(Mc146818Rtc::periodic_period_ticks(3) == 4);    // 122.070 us
    CHECK(Mc146818Rtc::periodic_period_ticks(6) == 32);   // 976.562 us
    CHECK(Mc146818Rtc::periodic_period_ticks(15) == 16384);  // 500 ms
}

TEST_CASE("Mc146818Rtc no periodic flag with rate select zero or divider stopped", "[mc146818]") {
    FakeHostClock host;
    Mc146818Rtc rtc(host.source());

    SECTION("rate select zero") {
        wr(rtc, REG_A, A_DV_32K);
    }
    SECTION("divider held in reset") {
        wr(rtc, REG_A, 0x70 | 0x03);
    }
    wr(rtc, REG_B, B_24H | B_PIE);
    run_cycles(rtc, 100'000);
    CHECK_FALSE(rtc.irq_pending());
    CHECK((rd(rtc, REG_C) & C_PF) == 0);
}

TEST_CASE("Mc146818Rtc RESET clears interrupt enables and flags only", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 17, 45, 30);
    Mc146818Rtc rtc(host.source());
    wr(rtc, REG_A, A_DV_32K | 0x03);
    wr(rtc, REG_B, B_PIE | B_AIE | B_UIE | B_SQWE | B_DM_BINARY | B_24H);
    wr(rtc, 0x20, 0x5A);
    run_cycles(rtc, 1000);
    REQUIRE(rtc.irq_pending());

    rtc.reset();

    CHECK_FALSE(rtc.irq_pending());
    CHECK(rtc.peek_register(REG_B) == (B_DM_BINARY | B_24H));
    CHECK(rtc.peek_register(REG_C) == 0);
    CHECK(rd(rtc, 0x20) == 0x5A);
    CHECK((rd(rtc, REG_A) & 0x7F) == (A_DV_32K | 0x03));
    CHECK(rd(rtc, REG_HOURS) == 17);  // binary mode; time unaffected
}

TEST_CASE("Mc146818Rtc peek_register has no side effects", "[mc146818]") {
    FakeHostClock host;
    Mc146818Rtc rtc(host.source());
    wr(rtc, REG_A, A_DV_32K | 0x03);
    wr(rtc, REG_B, B_24H);
    run_cycles(rtc, 1000);
    uint8_t before = rtc.peek_register(REG_C);
    REQUIRE((before & C_PF) != 0);
    CHECK(rtc.peek_register(REG_C) == before);
}

TEST_CASE("Mc146818Rtc battery image loads configuration and RAM", "[mc146818]") {
    FakeHostClock host;
    host.set(2026, 9, 23, 17, 45, 30);
    Mc146818Rtc rtc(host.source());

    Mc146818Rtc::BatteryImage image{};
    image[REG_A] = A_DV_32K;
    image[REG_B] = B_24H | B_DM_BINARY;
    image[REG_HOURS_ALARM] = 0x07;
    image[0x0E] = 0x11;
    image[0x3F] = 0xEE;
    image[REG_SECONDS] = 0x99;  // time registers are ignored: time follows the host
    rtc.load_battery_image(image);

    CHECK(rd(rtc, REG_B) == (B_24H | B_DM_BINARY));
    CHECK(rd(rtc, REG_HOURS_ALARM) == 0x07);
    CHECK(rd(rtc, 0x0E) == 0x11);
    CHECK(rd(rtc, 0x3F) == 0xEE);
    CHECK(rd(rtc, REG_SECONDS) == 30);
    CHECK(rtc.battery_image()[0x3F] == 0xEE);
}
