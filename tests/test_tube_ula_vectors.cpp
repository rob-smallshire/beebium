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

// Period Tube ULA test vectors, replayed against TubeUla.
//
// The vectors come from a BBC BASIC program found on a DFS
// disc titled TUBE-TEST. Its origin is not documented; it appears to be a
// production or development diagnostic for the Tube ULA. The program drives
// a Tube ULA in a 40-pin test jig through three 16-bit ports on the 1MHz bus,
// using a tiny scripting language:
//
//     PROCDIRECTION(OUT%, "HD7..HD0,PIRQ")   declare which pins the ULA drives
//     PROCDIRECTION(INP%, "HA2..HA0,HPHI")   declare which pins the tester drives
//     PROCSET("HA2..HA0=4,PA0=1")            drive tester pins
//     PROCCLK("HPHI")                        pulse a pin low then high
//     PROCNCLK("HPHI", 25)                   pulse a pin N times
//     PROCCHECK("HD7..HD0=&55,PIRQ=0")       sample ULA pins and compare
//
// This file is a line-for-line transliteration of that script. The Jig class
// below reproduces the BASIC's pin primitives, including its range expansion
// (a range "X7..X0=v" assigns bit 0 of v to X0 upwards) and its refusal to
// drive a ULA output or sample a tester-driven input. Each call carries the
// BASIC line number it came from, so a failing CHECK names the vector.
//
// Mapping pin activity onto TubeUla's register interface:
//
//   Host bus cycle    HCS low. HPHI rising edge with HRW high performs a host
//                     read of register HA; the falling edge with HRW low
//                     performs a host write of the value the tester drives on
//                     HD. While HPHI is high during a read, a status register
//                     (even HA) is sampled live and a data register (odd HA)
//                     shows the byte latched on the rising edge.
//   Parasite cycle    PCS low. PNRD falling edge performs a parasite read of
//                     register PA (status live while PNRD is low, data latched
//                     on the edge). PNWD rising edge performs a parasite write.
//   HRST              Low calls reset(). PRST is low while HRST is low or the P
//                     control flag is set.
//   PIRQ HIRQ PNMI    Active low on the pins, so they are the inverse of
//                     TubeUla's active-high outputs.
//   DRQ               Not modelled by TubeUla. Application Note 004 ("DMA
//                     Operation") defines DRQ as the register 3 action-required
//                     condition N, ungated by M. The parasite R3 status
//                     register reads N in bit 7, so DRQ is derived from there.
//   DACK              Not modelled by TubeUla. Application Note 004 states that
//                     DACK selects register 3 independently of PA0-2 and PCS
//                     and forces a read cycle when PNWD is active or a write
//                     cycle when PNRD is active. Modelled as forced R3 accesses.
//
// Application Note 004's register organisation table gives the parasite R3
// status as "N F3 x x x x x x" with every x bit reading as 1 (note 11). The
// vectors agree: line 3880 expects &3F with N and F3 both clear.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using beebium::TubeUla;

namespace {

// DATA statements at BASIC lines 1840-1970: the 40-pin Tube ULA assignment.
constexpr std::array<std::pair<std::string_view, int>, 40> PIN_TABLE = {{
    {"GND1", 1},  {"VCC1", 2},  {"VCC2", 3},  {"VCC3", 4},  {"GND2", 5},
    {"HD0", 6},   {"HD1", 7},   {"HD2", 8},   {"HD3", 9},   {"HD4", 10},
    {"HD5", 11},  {"HD6", 12},  {"HD7", 13},  {"HA0", 14},  {"HA1", 15},
    {"HA2", 16},  {"HRW", 17},  {"HCS", 18},  {"HPHI", 19}, {"HRST", 20},
    {"PCS", 21},  {"PNRD", 22}, {"PNWD", 23}, {"DACK", 24}, {"PA2", 25},
    {"PA1", 26},  {"PA0", 27},  {"PD7", 28},  {"PD6", 29},  {"PD5", 30},
    {"PD4", 31},  {"PD3", 32},  {"PD2", 33},  {"PD1", 34},  {"PD0", 35},
    {"PIRQ", 36}, {"PRST", 37}, {"DRQ", 38},  {"HIRQ", 39}, {"PNMI", 40},
}};

constexpr int MAXPIN = 40;

enum Pin : int {
    GND1 = 1, VCC1, VCC2, VCC3, GND2,
    HD0, HD1, HD2, HD3, HD4, HD5, HD6, HD7,
    HA0, HA1, HA2, HRW, HCS, HPHI, HRST,
    PCS, PNRD, PNWD, DACK,
    PA2, PA1, PA0,
    PD7, PD6, PD5, PD4, PD3, PD2, PD1, PD0,
    PIRQ, PRST, DRQ, HIRQ, PNMI,
};

// Pin direction from the ULA's point of view, as the BASIC uses the terms:
// OUTPUT pins are driven by the ULA and sampled by the tester, INPUT pins
// are driven by the tester into the ULA. NC (not connected) is the initial
// state; the BASIC errors on any access to an NC pin.
enum class Dir { NC, Output, Input };

const char* dir_name(Dir d)
{
    switch (d) {
    case Dir::NC: return "O/C";
    case Dir::Output: return "OUTPUT";
    case Dir::Input: return "INPUT";
    }
    return "?";
}

int find_pin(std::string_view name)
{
    for (const auto& [n, number] : PIN_TABLE)
        if (n == name)
            return number;
    FAIL("PIN " << name << " NOT FOUND");
    return 0;
}

std::string_view pin_name(int pin)
{
    return PIN_TABLE[static_cast<size_t>(pin - 1)].first;
}

// One item of a vector string: a pin or an inclusive range, with an
// optional value. Mirrors PROCRANGE / PROCACCEPT(EQUALS%) / PROCACCEPT(NUMBER%).
struct Item {
    std::string first;   // e.g. "HD7"
    std::string last;    // e.g. "HD0"; empty if not a range
    bool has_value = false;
    int value = 0;
};

// Parser for the vector mini-language. Equivalent to PROCGETSYMBOL and its
// callers: identifiers are letters/digits, "&" introduces hex, ".." is a
// range, "," separates items, spaces are skipped.
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::vector<Item> parse(bool expect_values)
    {
        std::vector<Item> items;
        for (;;) {
            Item item;
            item.first = identifier();
            skip_spaces();
            if (starts_with("..")) {
                pos_ += 2;
                item.last = identifier();
            }
            skip_spaces();
            if (expect_values) {
                expect('=');
                item.value = number();
                item.has_value = true;
            }
            items.push_back(std::move(item));
            skip_spaces();
            if (pos_ >= text_.size())
                break;
            expect(',');
        }
        return items;
    }

private:
    void skip_spaces()
    {
        while (pos_ < text_.size() && text_[pos_] == ' ')
            ++pos_;
    }

    bool starts_with(std::string_view s) const
    {
        return text_.substr(pos_, s.size()) == s;
    }

    void expect(char c)
    {
        skip_spaces();
        if (pos_ >= text_.size() || text_[pos_] != c)
            FAIL("'" << c << "' EXPECTED in \"" << text_ << "\" at " << pos_);
        ++pos_;
    }

    std::string identifier()
    {
        skip_spaces();
        size_t start = pos_;
        while (pos_ < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[pos_]))))
            ++pos_;
        if (pos_ == start)
            FAIL("IDENTIFIER EXPECTED in \"" << text_ << "\" at " << pos_);
        return std::string(text_.substr(start, pos_ - start));
    }

    int number()
    {
        skip_spaces();
        if (pos_ >= text_.size())
            FAIL("NUMBER EXPECTED in \"" << text_ << "\"");
        int base = 10;
        if (text_[pos_] == '&') {
            base = 16;
            ++pos_;
        }
        size_t start = pos_;
        while (pos_ < text_.size() && std::isxdigit(static_cast<unsigned char>(text_[pos_])))
            ++pos_;
        if (pos_ == start)
            FAIL("NUMBER EXPECTED in \"" << text_ << "\" at " << pos_);
        return std::stoi(std::string(text_.substr(start, pos_ - start)), nullptr, base);
    }

    std::string_view text_;
    size_t pos_ = 0;
};

// PROCEXTRACTNO: split "HD7" into label "HD" and trailing number 7.
std::pair<std::string, int> split_label(const std::string& name)
{
    size_t i = name.size();
    if (i == 0 || !std::isdigit(static_cast<unsigned char>(name[i - 1])))
        FAIL("RANGE IDENTIFIER MUST HAVE TAILING NUMBER: " << name);
    while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1])))
        --i;
    return {name.substr(0, i), std::stoi(name.substr(i))};
}

// Expand an item into (pin number, bit value) pairs in the order the BASIC
// visits them. For a range the BASIC starts at the LAST name and counts up to
// the FIRST, halving the value each step, so "HD7..HD0=&55" yields
// HD0=1, HD1=0, HD2=1, ... HD7=0. It requires FIRST >= LAST and matching
// labels. A lone pin takes bit 0 of the value.
std::vector<std::pair<int, int>> expand(const Item& item)
{
    std::vector<std::pair<int, int>> pins;
    if (item.last.empty()) {
        pins.emplace_back(find_pin(item.first), item.value & 1);
        return pins;
    }
    auto [first_label, first_no] = split_label(item.first);
    auto [last_label, last_no] = split_label(item.last);
    if (first_no < last_no)
        FAIL("RANGE WRONG WAY ROUND: " << item.first << ".." << item.last);
    if (first_label != last_label)
        FAIL("RANGE NAMES NOT CONSISTENT: " << item.first << ".." << item.last);
    int value = item.value;
    for (int no = last_no; no <= first_no; ++no) {
        pins.emplace_back(find_pin(first_label + std::to_string(no)), value & 1);
        value /= 2;
    }
    return pins;
}

// The test jig: pin levels, pin directions, and the mapping from pin edges
// onto TubeUla register operations.
class Jig {
public:
    explicit Jig(TubeUla& ula) : ula_(ula) {}

    // PROCDIRECTION
    void direction(int line, Dir dir, std::string_view spec)
    {
        INFO("BASIC line " << line << ": " << dir_name(dir) << "S : " << spec);
        for (const auto& item : Parser(spec).parse(false))
            for (auto [pin, unused] : expand(item)) {
                (void)unused;
                dir_[static_cast<size_t>(pin)] = dir;
            }
    }

    // PROCSET
    void set(int line, std::string_view spec)
    {
        INFO("BASIC line " << line << ": SET " << spec);
        for (const auto& item : Parser(spec).parse(true))
            for (auto [pin, value] : expand(item))
                drive(pin, value);
    }

    // PROCCLK: PROCSETBIT(pin, 0) then PROCSETBIT(pin, 1)
    void clk(int line, std::string_view spec)
    {
        INFO("BASIC line " << line << ": CLOCK " << spec);
        for (const auto& item : Parser(spec).parse(false))
            for (auto [pin, unused] : expand(item)) {
                (void)unused;
                drive(pin, 0);
                drive(pin, 1);
            }
    }

    // PROCNCLK
    void nclk(int line, std::string_view name, int times)
    {
        INFO("BASIC line " << line << ": CLOCK " << name << " " << times << " TIMES");
        int pin = find_pin(name);
        drive(pin, 0);
        for (int i = 0; i < times; ++i) {
            drive(pin, 0);
            drive(pin, 1);
        }
    }

    // PROCCHECK. Non-fatal so the whole vector set runs, like the BASIC
    // which counts errors and reports at the end.
    void check(int line, std::string_view spec)
    {
        for (const auto& item : Parser(spec).parse(true)) {
            // Report the whole multi-bit value once, then each bit.
            std::vector<std::pair<int, int>> pins = expand(item);
            int expected = 0;
            int actual = 0;
            for (size_t i = pins.size(); i-- > 0;) {
                auto [pin, bit] = pins[i];
                expected = (expected << 1) | bit;
                actual = (actual << 1) | sample(pin);
            }
            std::ostringstream where;
            where << "BASIC line " << line << ": CHECK " << spec << " -- "
                  << item.first;
            if (!item.last.empty())
                where << ".." << item.last;
            where << " expected &" << std::hex << std::uppercase << expected
                  << " actual &" << actual;
            INFO(where.str());
            CHECK(actual == expected);
        }
    }

private:
    Dir dir(int pin) const { return dir_[static_cast<size_t>(pin)]; }
    int level(int pin) const { return level_[static_cast<size_t>(pin)]; }

    // PROCSETBIT: the tester drives a pin. Refuses ULA outputs, as the BASIC does.
    void drive(int pin, int value)
    {
        if (dir(pin) != Dir::Input)
            FAIL(pin_name(pin) << " PIN " << pin << " IS AN " << dir_name(dir(pin)));
        int old = level(pin);
        level_[static_cast<size_t>(pin)] = value & 1;
        switch (pin) {
        case HPHI:
            if (!old && value) host_phi_rising();
            else if (old && !value) host_phi_falling();
            break;
        case PNRD:
            if (old && !value) pnrd_falling();
            break;
        case PNWD:
            if (!old && value) pnwd_rising();
            else if (old && !value) pnwd_falling();
            break;
        case HRST:
            if (!value) ula_.reset();
            break;
        default:
            break;
        }
    }

    // PROCCHECKBIT: sample a ULA-driven pin. Refuses tester inputs, as the BASIC does.
    int sample(int pin)
    {
        if (dir(pin) != Dir::Output)
            FAIL(pin_name(pin) << " PIN " << pin << " IS AN " << dir_name(dir(pin)));
        switch (pin) {
        case GND1: case GND2:
            return 0;
        case VCC1: case VCC2: case VCC3:
            return 1;
        case HD0: case HD1: case HD2: case HD3: case HD4: case HD5: case HD6: case HD7:
            return (host_bus() >> (pin - HD0)) & 1;
        case PD0: case PD1: case PD2: case PD3: case PD4: case PD5: case PD6: case PD7:
            return (parasite_bus() >> (PD0 - pin)) & 1;
        case PIRQ:
            return ula_.pirq() ? 0 : 1;
        case HIRQ:
            return ula_.hirq() ? 0 : 1;
        case PNMI:
            return ula_.pnmi_level() ? 0 : 1;
        case PRST:
            return (level(HRST) == 0 || ula_.parasite_reset_active()) ? 0 : 1;
        case DRQ:
            // App Note 004: DRQ is the R3 action-required condition N, ungated by M.
            return (ula_.parasite_peek(4) & TubeUla::DATA_AVAILABLE) ? 1 : 0;
        default:
            FAIL(pin_name(pin) << " is not a ULA output");
            return 0;
        }
    }

    uint8_t ha() const { return static_cast<uint8_t>(level(HA0) | (level(HA1) << 1) | (level(HA2) << 2)); }
    uint8_t pa() const { return static_cast<uint8_t>(level(PA0) | (level(PA1) << 1) | (level(PA2) << 2)); }

    uint8_t hd_driven()
    {
        if (dir(HD0) != Dir::Input)
            FAIL("host write while HD is not driven by the tester");
        uint8_t v = 0;
        for (int pin = HD7; pin >= HD0; --pin)
            v = static_cast<uint8_t>((v << 1) | level(pin));
        return v;
    }

    uint8_t pd_driven()
    {
        if (dir(PD0) != Dir::Input)
            FAIL("parasite write while PD is not driven by the tester");
        uint8_t v = 0;
        for (int pin = PD7; pin <= PD0; ++pin)
            v = static_cast<uint8_t>((v << 1) | level(pin));
        return v;
    }

    // Value the ULA drives on HD, sampled by the tester.
    uint8_t host_bus()
    {
        if (level(HCS) == 0 && level(HRW) == 1 && level(HPHI) == 1 && (ha() & 1) == 0)
            return ula_.host_peek(ha());
        return hd_latched_;
    }

    // Value the ULA drives on PD, sampled by the tester.
    uint8_t parasite_bus()
    {
        if (level(DACK) == 0)
            return pd_latched_;
        if (level(PCS) == 0 && level(PNRD) == 0 && (pa() & 1) == 0)
            return ula_.parasite_peek(pa());
        return pd_latched_;
    }

    void host_phi_rising()
    {
        if (level(HRST) == 0 || level(HCS) != 0)
            return;
        if (level(HRW) == 1 && (ha() & 1))
            hd_latched_ = ula_.host_read(ha());
    }

    void host_phi_falling()
    {
        if (level(HRST) == 0 || level(HCS) != 0)
            return;
        if (level(HRW) == 0) {
            ula_.host_write(ha(), hd_driven());
            if (ula_.stretched())
                FAIL("host write to register " << int(ha()) << " was bus-stretched");
        }
    }

    void pnrd_falling()
    {
        if (level(DACK) == 0) {
            // App Note 004: with DACK active, PNRD forces a write cycle to R3.
            ula_.parasite_write(5, pd_driven());
            return;
        }
        if (level(PCS) == 0 && (pa() & 1))
            pd_latched_ = ula_.parasite_read(pa());
    }

    void pnwd_rising()
    {
        if (level(DACK) == 0)
            return;
        if (level(PCS) == 0)
            ula_.parasite_write(pa(), pd_driven());
    }

    void pnwd_falling()
    {
        if (level(DACK) == 0) {
            // App Note 004: with DACK active, PNWD forces a read cycle from R3.
            pd_latched_ = ula_.parasite_read(5);
        }
    }

    TubeUla& ula_;
    std::array<int, MAXPIN + 1> level_{};
    std::array<Dir, MAXPIN + 1> dir_{};
    uint8_t hd_latched_ = 0;
    uint8_t pd_latched_ = 0;
};

}  // namespace

// The vector script, BASIC lines 1980-5260. Line numbers are carried into
// every call. Comments in the margin restate what the vector establishes
// where it is not obvious from the values alone.
TEST_CASE("Tube ULA test vectors (TUBE-TEST)", "[tube][vectors]")
{
    TubeUla ula;
    Jig jig(ula);

    jig.direction(2000, Dir::Output, "GND1,GND2,VCC3..VCC1,HD7..HD0,PD7..PD0,PIRQ,PRST,DRQ,HIRQ,PNMI");
    jig.direction(2010, Dir::Input, "HA2..HA0,HRW,HCS,HPHI,HRST,PCS,PNRD,PNWD,DACK,PA2..PA0");

    // *****  CHECK RESETS  *****
    jig.set(2030, "HCS=1,PCS=1,PNRD=1,PNWD=1,HRW=1,DACK=1");
    jig.set(2035, "HA2..HA0=0,PA2..PA0=0");
    jig.clk(2040, "HRST");
    jig.check(2050, "PRST=1");
    jig.set(2060, "HRST=0");
    jig.check(2070, "PRST=0");            // PRST follows HRST
    jig.nclk(2080, "HPHI", 25);
    jig.set(2090, "HRST=1");
    jig.check(2100, "PRST=1");

    // *****  CONTROL LATCHES *****
    jig.set(2130, "HCS=0,PCS=0,PNRD=0");
    jig.check(2140, "HD7..HD0=&40");      // after reset: F1 set, no flags
    jig.check(2150, "PD7..PD0=&40");
    jig.set(2160, "HPHI=0");
    jig.set(2165, "HRW=0");
    jig.direction(2170, Dir::Input, "HD7..HD0");
    jig.set(2180, "HD7..HD0=&95");        // S=1: set Q, J, V
    jig.set(2190, "HPHI=1");
    jig.set(2200, "HPHI=0");
    jig.check(2210, "PD7..PD0=&55");      // flags visible in bits 0-5 on the parasite side
    jig.set(2220, "HD7..HD0=&15");        // S=0: clear Q, J, V
    jig.set(2230, "HPHI=1");
    jig.set(2240, "HPHI=0");
    jig.set(2250, "HD7..HD0=&AA");        // S=1: set I, M, P
    jig.set(2260, "HPHI=1");
    jig.set(2270, "HPHI=0");
    jig.check(2280, "PD7..PD0=&6A");
    jig.set(2290, "HD7..HD0=&95");        // S=1: set Q, J, V again -> all six flags set
    jig.set(2300, "HPHI=1");
    jig.set(2310, "HPHI=0");
    jig.set(2320, "HRW=1");
    jig.direction(2330, Dir::Output, "HD7..HD0");
    jig.set(2340, "HPHI=1");
    jig.check(2350, "PD7..PD0=&7F");
    jig.check(2360, "HD7..HD0=&7F");
    jig.check(2370, "PRST=0");            // P flag asserts PRST

    // *****  REG 1A  *****  parasite to host, 24-byte FIFO
    jig.set(2390, "PNRD=1");
    jig.direction(2400, Dir::Input, "PD7..PD0");
    jig.set(2410, "PA0=1");
    jig.set(2420, "PD7..PD0=1");
    jig.clk(2430, "PNWD");
    jig.check(2440, "HD7..HD0=&FF");      // host status live: A1 set after one byte
    jig.set(2450, "PD7..PD0=2");
    jig.clk(2460, "PNWD");
    jig.set(2470, "PD7..PD0=4");
    jig.clk(2480, "PNWD");
    jig.set(2490, "PD7..PD0=8");
    jig.clk(2500, "PNWD");
    jig.set(2510, "PD7..PD0=&10");
    jig.clk(2520, "PNWD");
    jig.set(2530, "PD7..PD0=&20");
    jig.clk(2540, "PNWD");
    jig.set(2550, "PD7..PD0=&40");
    jig.clk(2560, "PNWD");
    jig.set(2570, "PD7..PD0=&80");
    jig.clk(2580, "PNWD");
    for (int i = 1; i <= 8; ++i) {        // 2585-2615: sixteen more bytes, filling the FIFO to 24
        jig.set(2590, "PD7..PD0=&55");
        jig.clk(2600, "PNWD");
        jig.set(2610, "PD7..PD0=&AA");
        jig.clk(2611, "PNWD");
    }
    jig.set(2630, "PD7..PD0=&55");
    jig.set(2650, "PA0=0");
    jig.clk(2655, "PNWD");                // parasite write to the status address: no effect on the FIFO
    jig.direction(2660, Dir::Output, "PD7..PD0");
    jig.set(2670, "PNRD=0");
    jig.check(2680, "PD7..PD0=&3F");      // F1 clear: FIFO full at 24 bytes
    jig.set(2690, "HPHI=0");
    jig.set(2700, "HA0=1");
    jig.set(2710, "HPHI=1");
    jig.check(2720, "HD7..HD0=1");
    jig.clk(2730, "HPHI");
    jig.check(2740, "PD7..PD0=&7F");      // F1 set again after one byte removed
    jig.check(2750, "HD7..HD0=2");
    jig.clk(2760, "HPHI");
    jig.check(2770, "HD7..HD0=4");
    jig.clk(2780, "HPHI");
    jig.check(2790, "HD7..HD0=8");
    jig.clk(2800, "HPHI");
    jig.check(2810, "HD7..HD0=&10");
    jig.clk(2820, "HPHI");
    jig.check(2830, "HD7..HD0=&20");
    jig.clk(2840, "HPHI");
    jig.check(2850, "HD7..HD0=&40");
    jig.clk(2860, "HPHI");
    jig.check(2870, "HD7..HD0=&80");
    for (int i = 1; i <= 8; ++i) {        // 2875-2915
        jig.clk(2880, "HPHI");
        jig.check(2890, "HD7..HD0=&55");
        jig.clk(2900, "HPHI");
        jig.check(2910, "HD7..HD0=&AA");
    }
    jig.clk(2920, "HPHI");
    jig.check(2930, "HD7..HD0=&55");      // 25th read of an empty FIFO returns the last parasite-driven value

    // *****  REG 1B  *****  host to parasite latch, PIRQ via I flag
    jig.set(2950, "HPHI=0");
    jig.set(2960, "HA0=1,HRW=0");
    jig.direction(2970, Dir::Input, "HD7..HD0");
    jig.set(2980, "HD7..HD0=&55");
    jig.check(2990, "PIRQ=1");
    jig.set(3000, "HCS=1");
    jig.set(3010, "HPHI=1");
    jig.set(3020, "HPHI=0");
    jig.check(3030, "PIRQ=1");            // cycle with HCS high has no effect
    jig.set(3040, "HCS=0");
    jig.set(3050, "HPHI=1");
    jig.set(3060, "HPHI=0");
    jig.set(3061, "HA0=0,HA1=1");
    jig.set(3062, "HD7..HD0=&AA");
    jig.set(3063, "HPHI=1");
    jig.set(3064, "HPHI=0");              // host write to R2 status address: no effect
    jig.set(3065, "HA0=1,HA1=0");
    jig.check(3070, "PIRQ=0");
    jig.check(3080, "PD7..PD0=&FF");
    jig.set(3090, "PNRD=1");
    jig.set(3100, "PA0=1");
    jig.set(3110, "PCS=1");
    jig.clk(3120, "PNRD");
    jig.check(3130, "PIRQ=0");            // read with PCS high has no effect
    jig.set(3140, "PCS=0");
    jig.set(3150, "PNRD=0");
    jig.check(3160, "PD7..PD0=&55");
    jig.set(3170, "PNRD=1");
    jig.set(3180, "HD7..HD0=&AA");
    jig.set(3190, "HPHI=1");
    jig.set(3200, "HPHI=0");
    jig.set(3210, "HA0=0");
    jig.set(3220, "HD7..HD0=2");          // S=0: clear I
    jig.set(3230, "HPHI=1");
    jig.set(3240, "HPHI=0");
    jig.check(3250, "PIRQ=1");            // I clear masks PIRQ although data is waiting
    jig.direction(3260, Dir::Output, "HD7..HD0");
    jig.set(3270, "HRW=1");
    jig.set(3280, "HPHI=1");
    jig.check(3290, "HD7..HD0=&3D");      // F1 clear while the latch is full; flags without I
    jig.set(3300, "PNRD=0");
    jig.check(3310, "PD7..PD0=&AA");
    jig.set(3320, "PNRD=1");
    jig.check(3330, "HD7..HD0=&7D");
    jig.set(3340, "HPHI=0");

    // *****  REG 2A  *****  parasite to host latch
    jig.set(3360, "HA1=1,PA1=1,PA0=0");
    jig.set(3370, "HPHI=1,PNRD=0");
    jig.check(3380, "HD7..HD0=&7F");      // R2 status also shows bits 0-5 set
    jig.check(3390, "PD7..PD0=&7F");
    jig.set(3400, "PNRD=1");
    jig.direction(3410, Dir::Input, "PD7..PD0");
    jig.set(3420, "PD7..PD0=&55");
    jig.set(3430, "PA0=1");
    jig.clk(3440, "PNWD");
    jig.set(3441, "PA0=0");
    jig.set(3442, "PD7..PD0=&AA");
    jig.clk(3443, "PNWD");                // parasite write to R2 status address: no effect
    jig.set(3444, "PA0=1");
    jig.check(3450, "HD7..HD0=&FF");
    jig.set(3460, "HPHI=0");
    jig.set(3470, "HA0=1");
    jig.set(3480, "HPHI=1");
    jig.check(3490, "HD7..HD0=&55");
    jig.set(3500, "HPHI=0");
    jig.set(3510, "PD7..PD0=&AA");
    jig.clk(3520, "PNWD");
    jig.direction(3530, Dir::Output, "PD7..PD0");
    jig.set(3540, "PA0=0");
    jig.set(3550, "PNRD=0");
    jig.check(3560, "PD7..PD0=&3F");      // parasite sees R2 full
    jig.set(3570, "HPHI=1");
    jig.check(3580, "HD7..HD0=&AA");
    jig.set(3590, "HPHI=0");

    // *****  REG 2B  *****  host to parasite latch
    jig.set(3610, "HRW=0");
    jig.direction(3620, Dir::Input, "HD7..HD0");
    jig.set(3630, "HD7..HD0=&55");
    jig.check(3640, "PD7..PD0=&7F");
    jig.set(3650, "HPHI=1");
    jig.set(3660, "HPHI=0");
    jig.check(3670, "PD7..PD0=&FF");
    jig.set(3671, "HA0=0");
    jig.set(3672, "HD7..HD0=&AA");
    jig.set(3673, "HPHI=1");
    jig.set(3674, "HPHI=0");              // host write to R2 status address: no effect
    jig.set(3675, "HA0=1");
    jig.set(3680, "PNRD=1");
    jig.set(3690, "PA0=1");
    jig.set(3700, "PNRD=0");
    jig.check(3710, "PD7..PD0=&55");
    jig.set(3720, "PNRD=1");
    jig.set(3730, "HD7..HD0=&AA");
    jig.set(3740, "HPHI=1");
    jig.set(3750, "HPHI=0");
    jig.set(3760, "HA0=0,HRW=1");
    jig.direction(3770, Dir::Output, "HD7..HD0");
    jig.set(3780, "HPHI=1");
    jig.check(3790, "HD7..HD0=&3F");      // host sees R2 full
    jig.set(3800, "PNRD=0");
    jig.check(3810, "PD7..PD0=&AA");
    jig.set(3820, "PNRD=1,HPHI=0");

    // *****  REG 3A  *****  two-byte mode (V set), parasite to host
    jig.check(3840, "PNMI=1,DRQ=0");      // reset dummy byte in P-to-H holds PNMI off
    jig.set(3850, "HA2..HA0=4,PA2..PA0=4");
    jig.set(3860, "PNRD=0,HPHI=1");
    jig.check(3870, "HD7..HD0=&FF");      // host sees the dummy byte as data available
    jig.check(3880, "PD7..PD0=&3F");      // parasite: no data, no space, bits 0-5 set
    jig.set(3890, "HPHI=0");
    jig.set(3900, "HA0=1");
    jig.set(3910, "HPHI=1");
    jig.set(3920, "HPHI=0");              // host reads the dummy byte
    jig.check(3930, "PNMI=0,DRQ=1");      // P-to-H empty -> PNMI
    jig.check(3940, "PD7..PD0=&FF");      // parasite R3 status bit 7 follows the PNMI condition
    jig.set(3950, "PNRD=1");
    jig.direction(3960, Dir::Input, "PD7..PD0");
    jig.set(3970, "PD7..PD0=&55");
    jig.set(3980, "PA0=1");
    jig.clk(3990, "PNWD");
    jig.check(4000, "PNMI=0,DRQ=1");      // one byte of a pair: PNMI stays on
    jig.set(4010, "PD7..PD0=&AA");
    jig.clk(4020, "PNWD");
    jig.check(4030, "PNMI=1,DRQ=0");      // pair complete: PNMI off
    jig.set(4031, "PA0=0");
    jig.set(4032, "PD7..PD0=&55");
    jig.clk(4033, "PNWD");                // parasite write to R3 status address: no effect
    jig.set(4034, "PA0=1");
    jig.set(4040, "HPHI=1");
    jig.check(4050, "HD7..HD0=&55");
    jig.set(4060, "HPHI=0");
    jig.check(4070, "PNMI=1,DRQ=0");      // one byte removed: still off (sticky)
    jig.set(4080, "HPHI=1");
    jig.check(4090, "HD7..HD0=&AA");
    jig.set(4100, "HPHI=0");
    jig.check(4110, "PNMI=0,DRQ=1");      // both removed: PNMI on
    jig.set(4120, "HA2..HA0=0,HRW=0");
    jig.direction(4130, Dir::Input, "HD7..HD0");
    jig.set(4140, "HD7..HD0=&10");        // S=0: clear V -> one-byte mode
    jig.set(4150, "HPHI=1");
    jig.set(4160, "HPHI=0");
    jig.set(4170, "PCS=1,DACK=0");        // DMA cycles from here
    jig.clk(4180, "PNRD");                // DACK low: PNRD strobes a DMA write of PD (&55) into P-to-H R3
    jig.check(4190, "PNMI=1,DRQ=0");

    // *****  REG 3B  *****  one-byte mode, host to parasite, DMA, then V change
    jig.set(4210, "HA2..HA0=&5");
    jig.set(4220, "HD7..HD0=&55");
    jig.set(4230, "HPHI=1");
    jig.set(4240, "HPHI=0");
    jig.set(4241, "HA0=0");
    jig.set(4242, "HD7..HD0=&AA");
    jig.set(4243, "HPHI=1");
    jig.set(4244, "HPHI=0");              // host write to R3 status address: no effect
    jig.set(4245, "HA0=1");
    jig.check(4250, "PNMI=0,DRQ=1");      // one byte in one-byte mode: PNMI on
    jig.direction(4260, Dir::Output, "PD7..PD0");
    jig.set(4270, "PNWD=0");              // DACK low: PNWD low drives R3 data on PD
    jig.check(4280, "PD7..PD0=&55");
    jig.set(4290, "PNWD=1");
    jig.check(4300, "PNMI=1,DRQ=0");
    jig.set(4310, "PCS=0,DACK=1");        // end of DMA cycles
    jig.set(4320, "HD7..HD0=&AA");
    jig.set(4330, "HPHI=1");
    jig.set(4340, "HPHI=0");
    jig.set(4350, "HA0=0,HRW=1");
    jig.direction(4360, Dir::Output, "HD7..HD0");
    jig.set(4370, "HPHI=1");
    jig.check(4380, "HD7..HD0=&BF");      // A3 set (DMA-written byte waiting), F3 clear (H-to-P full)
    jig.set(4390, "PNRD=0");
    jig.check(4400, "PD7..PD0=&AA");
    jig.set(4410, "PNRD=1");
    jig.check(4420, "HD7..HD0=&FF");
    jig.set(4430, "HPHI=0");
    jig.set(4440, "HA0=1,HRW=0");
    jig.direction(4450, Dir::Input, "HD7..HD0");
    jig.set(4460, "HPHI=1");
    jig.set(4470, "HPHI=0");              // host writes &AA to R3
    jig.set(4480, "HA2..HA0=0");
    jig.set(4490, "HD7..HD0=8");
    jig.check(4500, "PNMI=0,DRQ=1");
    jig.set(4510, "HPHI=1");
    jig.set(4520, "HPHI=0");              // S=0: clear M
    jig.check(4530, "PNMI=1,DRQ=1");      // M masks PNMI but not DRQ
    jig.set(4540, "HD7..HD0=&98");        // S=1: set M, V -> two-byte mode
    jig.set(4550, "HPHI=1");
    jig.set(4560, "HPHI=0");
    jig.clk(4570, "PNRD");                // parasite reads the single byte although below threshold
    jig.check(4580, "PNMI=1,DRQ=0");
    jig.set(4590, "HA2..HA0=5");
    jig.set(4600, "HPHI=1");
    jig.set(4610, "HPHI=0");
    jig.check(4620, "PNMI=1,DRQ=0");      // first byte of a pair
    jig.set(4630, "HPHI=1");
    jig.set(4640, "HPHI=0");
    jig.check(4650, "PNMI=0,DRQ=1");      // second byte: PNMI on
    jig.clk(4660, "PNRD");
    jig.check(4670, "PNMI=0,DRQ=1");      // one read: still on (sticky)
    jig.clk(4680, "PNRD");
    jig.check(4690, "PNMI=1,DRQ=0");      // both read: off

    // *****  REG 4A  *****  parasite to host latch, HIRQ via Q flag
    jig.set(4710, "HA2..HA0=6,PA2..PA0=6,HRW=1");
    jig.direction(4720, Dir::Output, "HD7..HD0");
    jig.set(4730, "HPHI=1,PNRD=0");
    jig.check(4740, "HD7..HD0=&7F");
    jig.check(4750, "PD7..PD0=&7F");
    jig.set(4760, "PNRD=1");
    jig.direction(4770, Dir::Input, "PD7..PD0");
    jig.set(4780, "PA0=1");
    jig.set(4790, "PD7..PD0=&55");
    jig.clk(4800, "PNWD");
    jig.check(4810, "HD7..HD0=&FF");
    jig.set(4811, "PA0=0");
    jig.set(4812, "PD7..PD0=&AA");
    jig.clk(4813, "PNWD");                // parasite write to R4 status address: no effect
    jig.set(4814, "PA0=1");
    jig.set(4820, "HPHI=0");
    jig.set(4830, "HA0=1");
    jig.set(4840, "HPHI=1");
    jig.check(4850, "HD7..HD0=&55");
    jig.set(4860, "HPHI=0");
    jig.set(4870, "PD7..PD0=&AA");
    jig.check(4880, "HIRQ=1");
    jig.clk(4890, "PNWD");
    jig.check(4900, "HIRQ=0");            // Q set: parasite R4 write asserts HIRQ
    jig.set(4910, "PA0=0");
    jig.direction(4920, Dir::Output, "PD7..PD0");
    jig.set(4930, "PNRD=0");
    jig.check(4940, "PD7..PD0=&3F");
    jig.set(4950, "HPHI=1");
    jig.check(4960, "HD7..HD0=&AA");
    jig.check(4970, "PD7..PD0=&7F");
    jig.set(4980, "HPHI=0");

    // *****  REG 4B  *****  host to parasite latch, PIRQ via J flag
    jig.set(5000, "HRW=0");
    jig.direction(5010, Dir::Input, "HD7..HD0");
    jig.set(5020, "HD7..HD0=&55");
    jig.set(5030, "HPHI=1");
    jig.set(5040, "HPHI=0");
    jig.check(5050, "PD7..PD0=&FF");
    jig.set(5051, "HA0=0");
    jig.set(5052, "HD7..HD0=&AA");
    jig.set(5053, "HPHI=1");
    jig.set(5054, "HPHI=0");              // host write to R4 status address: no effect
    jig.set(5055, "HA0=1");
    jig.set(5060, "PNRD=1");
    jig.set(5070, "PA0=1");
    jig.set(5080, "PNRD=0");
    jig.check(5090, "PD7..PD0=&55");
    jig.set(5100, "PNRD=1");
    jig.set(5110, "HD7..HD0=&AA");
    jig.set(5120, "HPHI=1");
    jig.set(5130, "HPHI=0");
    jig.set(5140, "HRW=1,HA0=0");
    jig.direction(5150, Dir::Output, "HD7..HD0");
    jig.set(5160, "HPHI=1");
    jig.check(5170, "HD7..HD0=&3F");
    jig.check(5180, "PIRQ=0");            // J set: host R4 write asserts PIRQ
    jig.set(5190, "PNRD=0");
    jig.check(5200, "PD7..PD0=&AA");
    jig.set(5210, "PNRD=1");
    jig.check(5220, "PIRQ=1");
    jig.check(5230, "HD7..HD0=&7F");
}
