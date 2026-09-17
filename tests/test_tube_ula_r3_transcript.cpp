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

// Golden transcript for the Tube ULA register 3 FIFO, reproducing issue #71.
//
// hoglet measured a genuine Acorn (Ferranti) Tube ULA with his tube_r3_tests
// program and published the full log
// (https://stardot.org.uk/forums/viewtopic.php?p=409877). tom_seddon confirmed
// the HP3 section on a Master 128 + cheese wedge. The program drives R3 in four
// sections -- PH3 and HP3, each in one-byte and two-byte mode -- each running
// seven access patterns: reset; R; W R R; W W R R R; W W W R R R R; W R W R R;
// W W R W R R R.
//
// The tables below are that log, line for line. After every step this test
// asserts the four status flags the program prints (hdav = host R3 status bit 7,
// hsav = host R3 status bit 6, pnmi = coprocessor R3 status bit 7, psav =
// coprocessor R3 status bit 6) and, for host writes, that the write did NOT
// stall the host (stretched() stays false).
//
// EXPECTED TO FAIL until issue #71 is fixed (this is the test-first red): on the
// third write of the "W W W" pattern the R3 H-to-P FIFO already holds two bytes.
// The real ULA ignores the write and it completes; Beebium today raises a bus
// stretch (host_write case 5), which in the full machine deadlocks the host
// against the waiting coprocessor. Here it shows up as stretched()==true after
// that write. Only the two HP3 sections write from the host side, so only they
// expose the stall.
//
// The flag columns and the non-empty data bytes already match the model, so
// those assertions pass today; that validates the transcript and isolates the
// stall as the one red. Two reads are deliberately NOT checked for their value,
// per the design decision in docs/discussion/tube-ula-full-register-writes.md:
//   - the byte read immediately after a Tube reset is undefined (reset-time
//     garbage on the real part); and
//   - a read of an empty R3 returns a fixed 0xE4 (parasite) / 0x96 (host) on the
//     Ferranti part, but Beebium returns the other side's bus latch, and that
//     simplification is being KEPT (section 3 of the design note). Both are
//     masked here -- their flags are still asserted.
//
// NOTE: the accepted fix removes TubeUla::stretched() and try_complete_stretch()
// altogether (design note section 2). When it lands, the stretched() probe below
// must be replaced by the store-or-drop contract from the note's table; the flag
// and data assertions and the masking stay as they are.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/TubeUla.hpp>

#include <cstdint>
#include <vector>

using namespace beebium;

namespace {

enum class Op { HostWrite, HostRead, ParasiteWrite, ParasiteRead };

// Data classification for reads. Writes are always Normal.
enum class Kind {
    Normal,      // a real data byte; assert it
    EmptyRead,   // read of an empty FIFO; Ferranti returns 0xE4/0x96
    ResetByte,   // first read after reset; undefined, masked
};

struct Step {
    Op op;
    uint8_t data;                 // value written, or expected value read
    uint8_t hdav, hsav, pnmi, psav;  // expected status flags after the step
    Kind kind = Kind::Normal;
};

// R3 register offsets (mirrored from &FEE4/&FEE5 host, &FEFC/&FEFD coprocessor).
constexpr uint8_t R3_STATUS = 4;
constexpr uint8_t R3_DATA = 5;

struct Section {
    const char* name;
    bool two_byte;               // V flag: R3 two-byte mode
    std::vector<Step> steps;
};

// Result of driving a section against the ULA.
struct Driven {
    std::vector<uint8_t> actual_read;   // per-step read value (0 for writes)
    std::vector<uint8_t> hdav, hsav, pnmi, psav;
    int first_stalled_write = -1;       // data of the first host write that stalled
};

bool bit7(uint8_t status) { return (status & TubeUla::DATA_AVAILABLE) != 0; }
bool bit6(uint8_t status) { return (status & TubeUla::SPACE_AVAILABLE) != 0; }

Driven drive(const Section& section) {
    TubeUla tube;
    tube.reset();
    if (section.two_byte)
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_V);  // set two-byte mode

    Driven d;
    for (const Step& step : section.steps) {
        uint8_t value = 0;
        switch (step.op) {
        case Op::HostWrite:
            tube.host_write(R3_DATA, step.data);
            if (tube.stretched() && d.first_stalled_write < 0)
                d.first_stalled_write = step.data;
            break;
        case Op::ParasiteWrite:
            tube.coprocessor_write(R3_DATA, step.data);
            break;
        case Op::HostRead:
            value = tube.host_read(R3_DATA);
            break;
        case Op::ParasiteRead:
            value = tube.coprocessor_read(R3_DATA);
            break;
        }
        d.actual_read.push_back(value);
        d.hdav.push_back(bit7(tube.host_peek(R3_STATUS)));
        d.hsav.push_back(bit6(tube.host_peek(R3_STATUS)));
        d.pnmi.push_back(bit7(tube.coprocessor_peek(R3_STATUS)));
        d.psav.push_back(bit6(tube.coprocessor_peek(R3_STATUS)));
    }
    return d;
}

// Shorthands for the tables.
Step hw(uint8_t d, uint8_t a, uint8_t s, uint8_t n, uint8_t p) {
    return Step{Op::HostWrite, d, a, s, n, p, Kind::Normal};
}
Step pw(uint8_t d, uint8_t a, uint8_t s, uint8_t n, uint8_t p) {
    return Step{Op::ParasiteWrite, d, a, s, n, p, Kind::Normal};
}
Step hr(uint8_t d, uint8_t a, uint8_t s, uint8_t n, uint8_t p, Kind k) {
    return Step{Op::HostRead, d, a, s, n, p, k};
}
Step pr(uint8_t d, uint8_t a, uint8_t s, uint8_t n, uint8_t p, Kind k) {
    return Step{Op::ParasiteRead, d, a, s, n, p, k};
}

// -----------------------------------------------------------------------------
// The four Ferranti sections (hoglet's log, verbatim).
// -----------------------------------------------------------------------------

// PH3 = parasite writes, host reads. HP3 = host writes, parasite reads.
// Columns after the data byte are hdav, hsav, pnmi, psav.

Section ph3_one_byte() {
    return {"PH3 one-byte (Type 0)", false, {
        hr(0x34, 0,1,1,1, Kind::ResetByte),
        pw(0x11, 1,1,0,0), hr(0x11, 0,1,1,1, Kind::Normal), hr(0x96, 0,1,1,1, Kind::EmptyRead),
        pw(0x22, 1,1,0,0), pw(0x33, 1,1,0,0),
        hr(0x22, 1,1,0,0, Kind::Normal), hr(0x33, 0,1,1,1, Kind::Normal), hr(0x96, 0,1,1,1, Kind::EmptyRead),
        pw(0x44, 1,1,0,0), pw(0x55, 1,1,0,0), pw(0x66, 1,1,0,0),
        hr(0x44, 1,1,0,0, Kind::Normal), hr(0x55, 0,1,1,1, Kind::Normal),
        hr(0x96, 0,1,1,1, Kind::EmptyRead), hr(0x96, 0,1,1,1, Kind::EmptyRead),
        pw(0x77, 1,1,0,0), hr(0x77, 0,1,1,1, Kind::Normal),
        pw(0x88, 1,1,0,0), hr(0x88, 0,1,1,1, Kind::Normal), hr(0x96, 0,1,1,1, Kind::EmptyRead),
        pw(0x99, 1,1,0,0), pw(0xAA, 1,1,0,0), hr(0x99, 1,1,0,0, Kind::Normal),
        pw(0xBB, 1,1,0,0), hr(0xAA, 1,1,0,0, Kind::Normal), hr(0xBB, 0,1,1,1, Kind::Normal),
        hr(0x96, 0,1,1,1, Kind::EmptyRead),
    }};
}

Section hp3_one_byte() {
    return {"HP3 one-byte (Type 1)", false, {
        pr(0xE4, 1,1,0,0, Kind::ResetByte),
        hw(0x11, 1,0,1,0), pr(0x11, 1,1,0,0, Kind::Normal), pr(0xE4, 1,1,0,0, Kind::EmptyRead),
        hw(0x22, 1,0,1,0), hw(0x33, 1,0,1,0),
        pr(0x22, 1,0,1,0, Kind::Normal), pr(0x33, 1,1,0,0, Kind::Normal), pr(0xE4, 1,1,0,0, Kind::EmptyRead),
        hw(0x44, 1,0,1,0), hw(0x55, 1,0,1,0), hw(0x66, 1,0,1,0),   // 0x66 overfills: must not stall
        pr(0x44, 1,0,1,0, Kind::Normal), pr(0x55, 1,1,0,0, Kind::Normal),
        pr(0xE4, 1,1,0,0, Kind::EmptyRead), pr(0xE4, 1,1,0,0, Kind::EmptyRead),
        hw(0x77, 1,0,1,0), pr(0x77, 1,1,0,0, Kind::Normal),
        hw(0x88, 1,0,1,0), pr(0x88, 1,1,0,0, Kind::Normal), pr(0xE4, 1,1,0,0, Kind::EmptyRead),
        hw(0x99, 1,0,1,0), hw(0xAA, 1,0,1,0), pr(0x99, 1,0,1,0, Kind::Normal),
        hw(0xBB, 1,0,1,0), pr(0xAA, 1,0,1,0, Kind::Normal), pr(0xBB, 1,1,0,0, Kind::Normal),
        pr(0xE4, 1,1,0,0, Kind::EmptyRead),
    }};
}

Section ph3_two_byte() {
    return {"PH3 two-byte (Type 2)", true, {
        hr(0x0D, 0,1,1,1, Kind::ResetByte),
        pw(0x11, 0,1,1,1), hr(0x11, 0,1,1,1, Kind::Normal), hr(0x96, 0,1,1,1, Kind::EmptyRead),
        pw(0x22, 0,1,1,1), pw(0x33, 1,1,0,0),
        hr(0x22, 1,1,0,0, Kind::Normal), hr(0x33, 0,1,1,1, Kind::Normal), hr(0x96, 0,1,1,1, Kind::EmptyRead),
        pw(0x44, 0,1,1,1), pw(0x55, 1,1,0,0), pw(0x66, 1,1,0,0),
        hr(0x44, 1,1,0,0, Kind::Normal), hr(0x55, 0,1,1,1, Kind::Normal),
        hr(0x96, 0,1,1,1, Kind::EmptyRead), hr(0x96, 0,1,1,1, Kind::EmptyRead),
        pw(0x77, 0,1,1,1), hr(0x77, 0,1,1,1, Kind::Normal),
        pw(0x88, 0,1,1,1), hr(0x88, 0,1,1,1, Kind::Normal), hr(0x96, 0,1,1,1, Kind::EmptyRead),
        pw(0x99, 0,1,1,1), pw(0xAA, 1,1,0,0), hr(0x99, 1,1,0,0, Kind::Normal),
        pw(0xBB, 1,1,0,0), hr(0xAA, 1,1,0,0, Kind::Normal), hr(0xBB, 0,1,1,1, Kind::Normal),
        hr(0x96, 0,1,1,1, Kind::EmptyRead),
    }};
}

Section hp3_two_byte() {
    return {"HP3 two-byte (Type 3)", true, {
        pr(0xE4, 1,1,0,0, Kind::ResetByte),
        hw(0x11, 1,1,0,0), pr(0x11, 1,1,0,0, Kind::Normal), pr(0xE4, 1,1,0,0, Kind::EmptyRead),
        hw(0x22, 1,1,0,0), hw(0x33, 1,0,1,0),
        pr(0x22, 1,0,1,0, Kind::Normal), pr(0x33, 1,1,0,0, Kind::Normal), pr(0xE4, 1,1,0,0, Kind::EmptyRead),
        hw(0x44, 1,1,0,0), hw(0x55, 1,0,1,0), hw(0x66, 1,0,1,0),   // 0x66 overfills: must not stall
        pr(0x44, 1,0,1,0, Kind::Normal), pr(0x55, 1,1,0,0, Kind::Normal),
        pr(0xE4, 1,1,0,0, Kind::EmptyRead), pr(0xE4, 1,1,0,0, Kind::EmptyRead),
        hw(0x77, 1,1,0,0), pr(0x77, 1,1,0,0, Kind::Normal),
        hw(0x88, 1,1,0,0), pr(0x88, 1,1,0,0, Kind::Normal), pr(0xE4, 1,1,0,0, Kind::EmptyRead),
        hw(0x99, 1,1,0,0), hw(0xAA, 1,0,1,0), pr(0x99, 1,0,1,0, Kind::Normal),
        hw(0xBB, 1,0,1,0), pr(0xAA, 1,0,1,0, Kind::Normal), pr(0xBB, 1,1,0,0, Kind::Normal),
        pr(0xE4, 1,1,0,0, Kind::EmptyRead),
    }};
}

void check_section(const Section& section) {
    Driven d = drive(section);
    const auto& steps = section.steps;

    // Post-reset status is the same for every section: host sees P-to-H data
    // (the reset dummy byte) and H-to-P space; the coprocessor sees neither.
    {
        TubeUla t;
        t.reset();
        if (section.two_byte)
            t.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_V);
        CHECK(bit7(t.host_peek(R3_STATUS)) == true);          // hdav
        CHECK(bit6(t.host_peek(R3_STATUS)) == true);          // hsav
        CHECK(bit7(t.coprocessor_peek(R3_STATUS)) == false);  // pnmi
        CHECK(bit6(t.coprocessor_peek(R3_STATUS)) == false);  // psav
    }

    // Flags line for line, and every non-empty read's data byte. Empty reads and
    // the post-reset read are masked (value undefined / a kept simplification);
    // their flags are still checked.
    for (size_t i = 0; i < steps.size(); ++i) {
        INFO(section.name << " step " << i << " data=0x" << std::hex
             << static_cast<int>(steps[i].data));
        CHECK(d.hdav[i] == (steps[i].hdav != 0));
        CHECK(d.hsav[i] == (steps[i].hsav != 0));
        CHECK(d.pnmi[i] == (steps[i].pnmi != 0));
        CHECK(d.psav[i] == (steps[i].psav != 0));
        if (steps[i].kind == Kind::Normal &&
            (steps[i].op == Op::HostRead || steps[i].op == Op::ParasiteRead)) {
            CHECK(static_cast<int>(d.actual_read[i]) == static_cast<int>(steps[i].data));
        }
    }

    // The stall defect (#71): no host write may leave the bus stretched. The
    // third write of the W W W pattern (0x66) overfills a full R3; the real ULA
    // ignores it, Beebium stretches. -1 means no write stalled.
    INFO(section.name << ": first host write that stalled the bus (data byte, -1 = none)");
    CHECK(d.first_stalled_write == -1);
}

}  // namespace

TEST_CASE("Tube R3 transcript: PH3 one-byte mode (Ferranti golden)", "[tube][r3][issue71][transcript]") {
    check_section(ph3_one_byte());
}

TEST_CASE("Tube R3 transcript: HP3 one-byte mode (Ferranti golden)", "[tube][r3][issue71][transcript]") {
    check_section(hp3_one_byte());
}

TEST_CASE("Tube R3 transcript: PH3 two-byte mode (Ferranti golden)", "[tube][r3][issue71][transcript]") {
    check_section(ph3_two_byte());
}

TEST_CASE("Tube R3 transcript: HP3 two-byte mode (Ferranti golden)", "[tube][r3][issue71][transcript]") {
    check_section(hp3_two_byte());
}

// The minimal, unambiguous reproduction of issue #71: a third host write into a
// full R3 H-to-P register must complete without stalling the host, and the byte
// must be dropped (the FIFO drains to its two earlier bytes and is then empty).
TEST_CASE("Issue #71: a host write to a full R3 must not stall the host", "[tube][r3][issue71]") {
    TubeUla tube;
    tube.reset();  // one-byte mode (V clear); FIFO depth is two either way

    tube.host_write(R3_DATA, 0x44);
    REQUIRE_FALSE(tube.stretched());
    tube.host_write(R3_DATA, 0x55);
    REQUIRE_FALSE(tube.stretched());

    // Third write: the H-to-P FIFO already holds two bytes. The real Ferranti
    // ULA ignores the write and it completes; Beebium today raises a bus stretch.
    tube.host_write(R3_DATA, 0x66);
    CHECK_FALSE(tube.stretched());  // RED until #71 is fixed

    // The two earlier bytes are still readable in order...
    CHECK(static_cast<int>(tube.coprocessor_read(R3_DATA)) == 0x44);
    CHECK(static_cast<int>(tube.coprocessor_read(R3_DATA)) == 0x55);
    // ...and 0x66 was dropped, so the FIFO is now empty: the host sees space.
    CHECK((tube.host_peek(R3_STATUS) & TubeUla::SPACE_AVAILABLE) != 0);
}
