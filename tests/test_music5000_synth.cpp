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

// The Music 5000 DSP against expectations worked by hand from
// docs/music5000/hardware-model.md (section numbers in the test names).

#include "Music5000Synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

using namespace beebium;

namespace {

// JIM page holding a RAM address, for the Music 5000's page nibble 3.
uint8_t page_for(uint16_t ram_address) {
    return static_cast<uint8_t>(0x30 | ((ram_address >> 8) << 1));
}

void poke(Music5000Synth& synth, uint16_t ram_address, uint8_t value) {
    synth.write(page_for(ram_address), static_cast<uint8_t>(ram_address & 0xFF), value);
}

}  // namespace

TEST_CASE("Music5000Synth host writes land in the RAM block page bits 3-1 select (1.2)",
          "[music5000][synth][registers]") {
    Music5000Synth synth;

    synth.write(0x30, 0x10, 0xA1);
    synth.write(0x32, 0x00, 0xA2);
    synth.write(0x3C, 0x7F, 0xA3);
    synth.write(0x3E, 0x05, 0xA4);

    auto ram = synth.ram();
    REQUIRE(ram[0x010] == 0xA1);
    REQUIRE(ram[0x100] == 0xA2);
    REQUIRE(ram[0x67F] == 0xA3);
    REQUIRE(ram[0x705] == 0xA4);
}

TEST_CASE("Music5000Synth ignores page bit 0 (1.1)", "[music5000][synth][registers]") {
    Music5000Synth synth;

    synth.write(0x3F, 0x85, 0x5A);
    REQUIRE(synth.ram()[0x785] == 0x5A);

    synth.write(0x31, 0x00, 0x11);
    synth.write(0x30, 0x00, 0x22);
    REQUIRE(synth.ram()[0x000] == 0x22);
}

TEST_CASE("Music5000Synth leaves page nibble decoding to its bus claim",
          "[music5000][synth][registers]") {
    // The synthesiser decodes only page bits 3-1; the extension claims pages
    // 0x30-0x3F (or 0x50-0x5F), so a Music 3000 page lands in the same block.
    Music5000Synth synth;
    synth.write(0x5E, 0x01, 0x77);
    REQUIRE(synth.ram()[0x701] == 0x77);
}

TEST_CASE("Music5000Synth register columns follow the counter order (1.3)",
          "[music5000][synth][registers]") {
    constexpr std::array<std::size_t, 16> expected = {0, 8, 1, 9, 2, 10, 3, 11,
                                                      4, 12, 5, 13, 6, 14, 7, 15};
    for (std::size_t k = 0; k < 16; ++k) {
        REQUIRE(Music5000Synth::column_for_counter(k) == expected[k]);
    }

    // Rows within a bank, e.g. column 9's amplitude in the alternate bank.
    REQUIRE(Music5000Synth::register_address(Music5000Synth::kAlternateBank,
                                             Music5000Synth::kAmplitudeRow, 9) == 0x7E9);
    REQUIRE(Music5000Synth::register_address(Music5000Synth::kNormalBank,
                                             Music5000Synth::kControlRow, 15) == 0x77F);
}

TEST_CASE("Music5000Synth reset zeroes the normal bank only (1.5)", "[music5000][synth][reset]") {
    Music5000Synth synth;
    for (uint16_t a = 0x000; a < 0x800; ++a) {
        poke(synth, a, 0xEE);
    }

    synth.reset();

    auto ram = synth.ram();
    for (uint16_t a = 0x700; a < 0x780; ++a) {
        INFO("address " << a);
        REQUIRE(ram[a] == 0x00);
    }
    for (uint16_t a = 0x780; a < 0x800; ++a) {
        INFO("address " << a);
        REQUIRE(ram[a] == 0xEE);
    }
    // Wave bytes survive, except the one each channel's phase addresses
    // (wave 0, index 0 here; see the wave-byte test below).
    REQUIRE(ram[0x001] == 0xEE);
    REQUIRE(ram[0x6FF] == 0xEE);
    REQUIRE_FALSE(synth.index());
}

namespace {

// Set channel counter value k's 24-bit frequency in the normal bank.
void set_frequency(Music5000Synth& synth, std::size_t k, uint32_t freq) {
    const std::size_t column = Music5000Synth::column_for_counter(k);
    const uint16_t base = Music5000Synth::kNormalBank;
    poke(synth, Music5000Synth::register_address(base, Music5000Synth::kFreqLoRow, column),
         static_cast<uint8_t>(freq));
    poke(synth, Music5000Synth::register_address(base, Music5000Synth::kFreqMidRow, column),
         static_cast<uint8_t>(freq >> 8));
    poke(synth, Music5000Synth::register_address(base, Music5000Synth::kFreqHiRow, column),
         static_cast<uint8_t>(freq >> 16));
}

void render(Music5000Synth& synth, int frames = 1) {
    int32_t left = 0;
    int32_t right = 0;
    for (int i = 0; i < frames; ++i) {
        synth.render_frame(left, right);
    }
}

}  // namespace

TEST_CASE("Music5000Synth accumulates a 24-bit phase per sample (3.1)",
          "[music5000][synth][phase]") {
    Music5000Synth synth;
    set_frequency(synth, 0, 0x000100);

    render(synth);
    REQUIRE(synth.phase(0) == 0x000100);
    REQUIRE_FALSE(synth.wrapped(0));

    render(synth, 9);
    REQUIRE(synth.phase(0) == 0x000A00);

    // Channels without a frequency keep their phase.
    REQUIRE(synth.phase(1) == 0);
}

TEST_CASE("Music5000Synth carries between phase bytes (3.1)", "[music5000][synth][phase]") {
    Music5000Synth synth;
    set_frequency(synth, 2, 0x00FFFE);  // bit 0 clear: an add, not a load
    render(synth);
    REQUIRE(synth.phase(2) == 0x00FFFE);

    set_frequency(synth, 2, 0x000002);
    render(synth);
    REQUIRE(synth.phase(2) == 0x010000);
}

TEST_CASE("Music5000Synth wraps when the phase carries out of bit 23 (3.1)",
          "[music5000][synth][phase]") {
    Music5000Synth synth;
    set_frequency(synth, 0, 0x800000);

    render(synth);
    REQUIRE(synth.phase(0) == 0x800000);
    REQUIRE_FALSE(synth.wrapped(0));

    render(synth);
    REQUIRE(synth.phase(0) == 0x000000);
    REQUIRE(synth.wrapped(0));

    render(synth);
    REQUIRE_FALSE(synth.wrapped(0));
}

TEST_CASE("Music5000Synth reads the frequency of the counter's column (1.3)",
          "[music5000][synth][phase]") {
    // Column 8 is read by counter value 1, column 1 by counter value 2.
    Music5000Synth synth;
    poke(synth, Music5000Synth::register_address(Music5000Synth::kNormalBank,
                                                 Music5000Synth::kFreqMidRow, 8), 0x02);
    poke(synth, Music5000Synth::register_address(Music5000Synth::kNormalBank,
                                                 Music5000Synth::kFreqMidRow, 1), 0x03);
    render(synth);
    REQUIRE(synth.phase(1) == 0x000200);
    REQUIRE(synth.phase(2) == 0x000300);
    REQUIRE(synth.phase(8) == 0);
}

TEST_CASE("Music5000Synth phase-set loads the frequency and never wraps (3.2)",
          "[music5000][synth][phase]") {
    Music5000Synth synth;

    SECTION("all 24 bits, bit 0 included") {
        set_frequency(synth, 3, 0x123457);
        render(synth, 5);
        REQUIRE(synth.phase(3) == 0x123457);
        REQUIRE_FALSE(synth.wrapped(3));
    }

    SECTION("even at full scale") {
        set_frequency(synth, 3, 0xFFFFFF);
        render(synth, 3);
        REQUIRE(synth.phase(3) == 0xFFFFFF);
        REQUIRE_FALSE(synth.wrapped(3));
    }

    SECTION("and the phase runs on from there once the bit is cleared") {
        set_frequency(synth, 3, 0x7FFF01);
        render(synth);
        set_frequency(synth, 3, 0x800000);
        render(synth);
        REQUIRE(synth.phase(3) == 0xFFFF01);
        render(synth);
        REQUIRE(synth.phase(3) == 0x7FFF01);
        REQUIRE(synth.wrapped(3));
    }
}

TEST_CASE("Music5000Synth reset zeroes the wave byte each channel addresses (1.5)",
          "[music5000][synth][reset]") {
    Music5000Synth synth;
    for (uint16_t a = 0x000; a < 0x700; ++a) {
        poke(synth, a, 0xEE);
    }
    // Counter value 0 parks at phase 0x460001 (phase-set); the others stay at
    // phase 0.
    set_frequency(synth, 0, 0x460001);
    render(synth);

    synth.reset();

    auto ram = synth.ram();
    REQUIRE(ram[0x023] == 0x00);  // wave 0, index 0x46 >> 1
    REQUIRE(ram[0x000] == 0x00);  // wave 0, index 0 (the other fifteen)
    REQUIRE(ram[0x001] == 0xEE);
    REQUIRE(ram[0x022] == 0xEE);
    REQUIRE(ram[0x0A3] == 0xEE);  // wave 1 is untouched
    // The phase RAM is untouched.
    REQUIRE(synth.phase(0) == 0x460001);
}

namespace {

void set_register(Music5000Synth& synth, std::size_t k, uint16_t row, uint8_t value) {
    poke(synth, Music5000Synth::register_address(Music5000Synth::kNormalBank, row,
                                                 Music5000Synth::column_for_counter(k)),
         value);
}

// Fill every byte of a wave table with one sample value.
void fill_wave(Music5000Synth& synth, uint8_t wave, uint8_t sample) {
    for (uint16_t i = 0; i < 128; ++i) {
        poke(synth, static_cast<uint16_t>(wave << 7 | i), sample);
    }
}

int32_t render_right(Music5000Synth& synth) {
    int32_t left = 0;
    int32_t right = 0;
    synth.render_frame(left, right);
    return right;
}

}  // namespace

TEST_CASE("Music5000Synth DAC current follows the AM6070's laws (7.1)",
          "[music5000][synth][dac]") {
    // Decode: datasheet Table 3. Encode: Table 4.
    struct Point { uint8_t chord; uint8_t step; int32_t decode; int32_t encode; };
    constexpr std::array<Point, 6> points = {{
        {0, 0, 0, 1},
        {0, 1, 2, 3},
        {1, 0, 33, 35},
        {3, 5, 311, 319},
        {5, 10, 1663, 1695},
        {7, 15, 8031, 8159},
    }};
    for (const auto& p : points) {
        const uint8_t code = static_cast<uint8_t>(p.chord << 4 | p.step);
        INFO("chord " << int(p.chord) << " step " << int(p.step));
        REQUIRE(Music5000Synth::decode_current(code) == p.decode);
        REQUIRE(Music5000Synth::encode_current(code) == p.encode);
        // Encode is decode plus half a step (2^chord).
        REQUIRE(Music5000Synth::encode_current(code)
                == Music5000Synth::decode_current(code) + (1 << p.chord));
    }
}

TEST_CASE("Music5000Synth amplitude takes effect at the first wrap after a write (4)",
          "[music5000][synth][amplitude]") {
    Music5000Synth synth;
    fill_wave(synth, 0, 0xC0);  // positive, log magnitude 0x40
    set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x50);
    set_frequency(synth, 0, 0x800000);

    // Frame 1: no wrap yet, the stored amplitude is still 0, and
    // 0xC0 + 0x00 = 0xC0 leaves bit 7 unchanged: silent.
    REQUIRE(render_right(synth) == 0);
    REQUIRE(synth.stored_amplitude(0) == 0x00);

    // Frame 2 wraps: 0x50 is latched and used on this very sample.
    // 0xC0 + 0x50 = 0x110 -> 0x10: chord 1, step 0 -> 33.
    REQUIRE(render_right(synth) == 33);
    REQUIRE(synth.stored_amplitude(0) == 0x50);

    // A new amplitude waits for the next wrap.
    set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x60);
    REQUIRE(render_right(synth) == 33);  // frame 3, no wrap
    // Frame 4 wraps: 0xC0 + 0x60 = 0x120 -> 0x20: chord 2, step 0 -> 99.
    REQUIRE(render_right(synth) == 99);
}

TEST_CASE("Music5000Synth gate shuts when the add does not flip bit 7 (4)",
          "[music5000][synth][amplitude]") {
    Music5000Synth synth;
    set_frequency(synth, 0, 0x800000);

    SECTION("positive sample, underflow") {
        fill_wave(synth, 0, 0xC0);
        set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x3F);
        render_right(synth);
        REQUIRE(render_right(synth) == 0);  // 0xC0 + 0x3F = 0xFF: shut
    }

    SECTION("positive sample, just open at code 0") {
        fill_wave(synth, 0, 0xC0);
        set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x40);
        render_right(synth);
        // 0xC0 + 0x40 = 0x100 -> 0x00: open, code 0, decode current 0.
        REQUIRE(render_right(synth) == 0);
        // One step louder: code 1 -> 2.
        set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x41);
        render_right(synth);
        REQUIRE(render_right(synth) == 2);
    }

    SECTION("negative sample") {
        fill_wave(synth, 0, 0x40);  // sign 0: negative, log magnitude 0x40
        set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x50);
        render_right(synth);
        // 0x40 + 0x50 = 0x90: bit 7 flipped to 1, code 0x10 -> 33, negative.
        REQUIRE(render_right(synth) == -33);
    }

    SECTION("negative sample, underflow") {
        fill_wave(synth, 0, 0x20);
        set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x50);
        render_right(synth);
        REQUIRE(render_right(synth) == 0);  // 0x20 + 0x50 = 0x70: shut
    }

    SECTION("amplitude bit 7 shuts the loud end") {
        fill_wave(synth, 0, 0xF0);  // positive, magnitude 0x70
        set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x90);
        render_right(synth);
        REQUIRE(render_right(synth) == 0);  // 0xF0 + 0x90 = 0x180 -> 0x80: shut
    }
}

TEST_CASE("Music5000Synth control bit 4 inverts the sign (6)", "[music5000][synth][amplitude]") {
    Music5000Synth synth;
    fill_wave(synth, 0, 0xC0);
    set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x50);
    set_register(synth, 0, Music5000Synth::kControlRow, 0x10);
    set_frequency(synth, 0, 0x800000);
    render_right(synth);
    REQUIRE(render_right(synth) == -33);
}

TEST_CASE("Music5000Synth reads the wave the select's upper nibble names at the new phase (2)",
          "[music5000][synth][amplitude]") {
    Music5000Synth synth;
    set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x50);
    set_register(synth, 0, Music5000Synth::kWaveSelectRow, 0x2F);  // wave 2; low nibble unused
    // Wave 2: index 0 loud (code 0x20 -> 99), index 1 quieter (code 0x10 -> 33).
    poke(synth, 0x100, 0xD0);
    poke(synth, 0x101, 0xC0);
    set_frequency(synth, 0, 0x800000);

    render_right(synth);                 // phase 0x800000, no wrap, amplitude 0
    REQUIRE(render_right(synth) == 99);  // wraps to phase 0: index 0
    set_frequency(synth, 0, 0x020000);
    REQUIRE(render_right(synth) == 33);  // phase 0x020000: index 1
}

TEST_CASE("Music5000Synth phase-set holds a constant level (3.2)", "[music5000][synth][amplitude]") {
    Music5000Synth synth;
    fill_wave(synth, 0, 0xC0);
    set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x50);
    set_frequency(synth, 0, 0x800000);
    render_right(synth);
    render_right(synth);  // the wrap latches the amplitude

    // Phase-set parks the phase; the amplitude stays as latched and the
    // channel keeps sounding the same sample.
    set_frequency(synth, 0, 0x000001);
    set_register(synth, 0, Music5000Synth::kAmplitudeRow, 0x00);
    for (int i = 0; i < 100; ++i) {
        REQUIRE(render_right(synth) == 33);
    }
    REQUIRE(synth.phase(0) == 0x000001);
}

namespace {

// Register a frequency in the alternate bank for a register column.
void set_alternate_frequency(Music5000Synth& synth, std::size_t column, uint32_t freq) {
    const uint16_t base = Music5000Synth::kAlternateBank;
    poke(synth, Music5000Synth::register_address(base, Music5000Synth::kFreqLoRow, column),
         static_cast<uint8_t>(freq));
    poke(synth, Music5000Synth::register_address(base, Music5000Synth::kFreqMidRow, column),
         static_cast<uint8_t>(freq >> 8));
    poke(synth, Music5000Synth::register_address(base, Music5000Synth::kFreqHiRow, column),
         static_cast<uint8_t>(freq >> 16));
}

void set_column_register(Music5000Synth& synth, std::size_t column, uint16_t row, uint8_t value) {
    poke(synth, Music5000Synth::register_address(Music5000Synth::kNormalBank, row, column), value);
}

// Counter value whose register column is `column`.
std::size_t counter_for_column(std::size_t column) {
    return ((column & 7) << 1) | (column >> 3);
}

// Give every column a normal-bank frequency of 0x000100 and an alternate-bank
// frequency of 0x000300, so a channel's phase step shows which bank it read.
void mark_banks(Music5000Synth& synth) {
    for (std::size_t column = 0; column < 16; ++column) {
        set_frequency(synth, counter_for_column(column), 0x000100);
        set_alternate_frequency(synth, column, 0x000300);
    }
}

}  // namespace

TEST_CASE("Music5000Synth column r modulates column r+1 (5)", "[music5000][synth][modulation]") {
    Music5000Synth synth;
    mark_banks(synth);
    fill_wave(synth, 0, 0xC0);  // every channel reads a positive sample
    set_column_register(synth, 0, Music5000Synth::kControlRow, 0x20);  // column 0 modulates

    render(synth);

    for (std::size_t column = 0; column < 16; ++column) {
        INFO("column " << column);
        const uint32_t expected = (column == 1) ? 0x000300 : 0x000100;
        REQUIRE(synth.phase(counter_for_column(column)) == expected);
    }
}

TEST_CASE("Music5000Synth column 7 modulates column 0 of the next sample (5)",
          "[music5000][synth][modulation]") {
    Music5000Synth synth;
    mark_banks(synth);
    fill_wave(synth, 0, 0xC0);
    set_column_register(synth, 7, Music5000Synth::kControlRow, 0x20);

    render(synth);
    // Not column 8 in the same sample, as the existing emulators have it.
    REQUIRE(synth.phase(counter_for_column(8)) == 0x000100);
    REQUIRE(synth.phase(counter_for_column(0)) == 0x000100);

    render(synth);
    REQUIRE(synth.phase(counter_for_column(0)) == 0x000100 + 0x000300);
    REQUIRE(synth.phase(counter_for_column(8)) == 0x000200);
}

TEST_CASE("Music5000Synth column 15 modulates column 8 of the next sample (5)",
          "[music5000][synth][modulation]") {
    Music5000Synth synth;
    mark_banks(synth);
    fill_wave(synth, 0, 0xC0);
    set_column_register(synth, 15, Music5000Synth::kControlRow, 0x20);

    render(synth);
    render(synth);
    REQUIRE(synth.phase(counter_for_column(8)) == 0x000100 + 0x000300);
    REQUIRE(synth.phase(counter_for_column(0)) == 0x000200);
    REQUIRE(synth.phase(counter_for_column(9)) == 0x000200);
}

TEST_CASE("Music5000Synth modulates on a positive sample or a wrap (5)",
          "[music5000][synth][modulation]") {
    Music5000Synth synth;
    mark_banks(synth);
    set_column_register(synth, 0, Music5000Synth::kControlRow, 0x20);
    const std::size_t target = counter_for_column(1);

    SECTION("a negative sample modulates only on its wrapping samples") {
        fill_wave(synth, 0, 0x40);                   // negative everywhere
        set_frequency(synth, counter_for_column(0), 0x800000);  // wraps every 2nd sample
        render(synth);
        REQUIRE(synth.phase(target) == 0x000100);            // no wrap: normal bank
        render(synth);
        REQUIRE(synth.phase(target) == 0x000100 + 0x000300); // wrap: alternate
        render(synth);
        REQUIRE(synth.phase(target) == 0x000500);
        render(synth);
        REQUIRE(synth.phase(target) == 0x000800);
    }

    SECTION("inversion does not change the sign modulation sees") {
        fill_wave(synth, 0, 0xC0);
        set_column_register(synth, 0, Music5000Synth::kControlRow, 0x30);
        render(synth);
        REQUIRE(synth.phase(target) == 0x000300);
    }

    SECTION("without control bit 5 nothing modulates") {
        fill_wave(synth, 0, 0xC0);
        set_column_register(synth, 0, Music5000Synth::kControlRow, 0x10);
        render(synth);
        REQUIRE(synth.phase(target) == 0x000100);
    }
}

TEST_CASE("Music5000Synth reset clears INDEX (1.5)", "[music5000][synth][modulation][reset]") {
    Music5000Synth synth;
    mark_banks(synth);
    fill_wave(synth, 0, 0xC0);
    set_column_register(synth, 7, Music5000Synth::kControlRow, 0x20);
    render(synth);
    // Column 7 (counter 14) leaves INDEX set for counter 0 of the next sample.
    REQUIRE(synth.index());

    synth.reset();
    REQUIRE_FALSE(synth.index());
}
