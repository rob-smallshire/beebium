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

// The hybrid-music-5000 extension on a Model B: its bus claim, its sample
// rate, reset delivery, two units on one bus, and sharing the bus with the
// SCSI host adapter.

#include "AcornScsiHostAdapter.hpp"
#include "HybridMusic5000.hpp"

#include <beebium/Machines.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/ExtensionRegistry.hpp>

#include <catch2/catch_test_macros.hpp>

#include "test_extension_ids.hpp"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace beebium;

namespace {

// A Model B running NOPs, with the extensions a test registers attached.
class Music5000Machine {
public:
    Music5000Machine() {
        std::vector<uint8_t> mos(16384, 0xEA);
        mos[0x3FFC] = 0x00;
        mos[0x3FFD] = 0x04;
        machine_.memory().load_mos(mos.data(), mos.size());
        machine_.reset();
        registry_.register_extension_point("1mhz-bus");
    }

    ~Music5000Machine() { registry_.shutdown(); }

    HybridMusic5000& add_unit(const std::string& unit, const std::string& id) {
        auto ext = HybridMusic5000::create();
        if (!unit.empty()) {
            ext->set_config_value("unit", unit);
        }
        ext->set_config_value("id", id);
        auto& ref = *ext;
        registry_.register_extension(std::move(ext));
        return ref;
    }

    void init() {
        auto& memory = machine_.memory();
        ExtensionContext ctx(&memory.one_mhz_bus(), nullptr, nullptr, &memory.indicators,
                             nullptr, nullptr, &memory.audio_source_port());
        registry_.resolve_and_init(ctx);
    }

    ExtensionRegistry& registry() { return registry_; }
    ModelB& machine() { return machine_; }

    void write(uint16_t address, uint8_t value) { machine_.memory().write(address, value); }
    uint8_t read(uint16_t address) { return machine_.memory().read(address); }

private:
    ModelB machine_;
    ExtensionRegistry registry_;
};

// Write a byte of the unit's RAM through the bus, as software does: the JIM
// paging register, then the byte in page &FD.
void write_ram(Music5000Machine& m, uint8_t page_nibble, uint16_t ram_address, uint8_t value) {
    m.write(0xFCFF, static_cast<uint8_t>(page_nibble << 4 | (ram_address >> 8) << 1));
    m.write(static_cast<uint16_t>(0xFD00 | (ram_address & 0xFF)), value);
}

}  // namespace

TEST_CASE("hybrid-music-5000 takes host writes through JIM pages &30-&3F",
          "[music5000][extension]") {
    Music5000Machine m;
    auto& synth = m.add_unit("", "music5000");
    m.init();

    write_ram(m, 0x3, 0x705, 0x42);
    write_ram(m, 0x3, 0x010, 0x43);
    REQUIRE(synth.synth().ram()[0x705] == 0x42);
    REQUIRE(synth.synth().ram()[0x010] == 0x43);

    // Page bit 0 is ignored: &3F lands in the same block as &3E.
    m.write(0xFCFF, 0x3F);
    m.write(0xFD06, 0x44);
    REQUIRE(synth.synth().ram()[0x706] == 0x44);

    // Another nibble is not this board's.
    m.write(0xFCFF, 0x2E);
    m.write(0xFD07, 0x45);
    REQUIRE(synth.synth().ram()[0x707] == 0x00);

    // The board is write-only: reads float.
    m.write(0xFCFF, 0x3E);
    REQUIRE(m.read(0xFD05) == 0xFF);
    REQUIRE(m.read(0xFCFF) == 0xFF);
}

TEST_CASE("hybrid-music-5000 runs at 46875 samples per emulated second",
          "[music5000][extension][rate]") {
    Music5000Machine m;
    auto& synth = m.add_unit("music-5000", "music5000");
    m.init();

    // One emulated second is 2,000,000 CPU cycles; the 1 MHz bus ticks the
    // device once per two. Measure over three seconds, after the first.
    m.machine().run(2'000'000);
    const uint64_t start_cycles = m.machine().cycle_count();
    const uint64_t start_frames = synth.frames_rendered();

    m.machine().run(6'000'000);
    const uint64_t cycles = m.machine().cycle_count() - start_cycles;
    const uint64_t frames = synth.frames_rendered() - start_frames;

    // run() finishes the instruction it is in, so allow its few cycles: the
    // count must be 3 per 64 ticks, i.e. 46875 per emulated second.
    const double expected = static_cast<double>(cycles) / 2.0 * 3.0 / 64.0;
    REQUIRE(cycles >= 6'000'000);
    REQUIRE(cycles < 6'000'010);
    REQUIRE(static_cast<double>(frames) >= expected - 1.0);
    REQUIRE(static_cast<double>(frames) <= expected + 1.0);
    REQUIRE(frames >= 140'624);
    REQUIRE(frames <= 140'626);
}

TEST_CASE("hybrid-music-5000 is reset by power-on and BREAK", "[music5000][extension][reset]") {
    Music5000Machine m;
    auto& synth = m.add_unit("", "music5000");
    m.init();

    write_ram(m, 0x3, 0x765, 0x50);  // a normal-bank amplitude
    write_ram(m, 0x3, 0x7E5, 0x51);  // the same in the alternate bank
    m.machine().soft_reset();
    REQUIRE(synth.synth().ram()[0x765] == 0x00);
    REQUIRE(synth.synth().ram()[0x7E5] == 0x51);

    write_ram(m, 0x3, 0x765, 0x52);
    m.machine().reset();
    REQUIRE(synth.synth().ram()[0x765] == 0x00);
}

TEST_CASE("hybrid-music-5000 Music 3000 and Music 5000 share the bus",
          "[music5000][extension][two-units]") {
    Music5000Machine m;
    auto& music5000 = m.add_unit("music-5000", "music5000");
    auto& music3000 = m.add_unit("music-3000", "music3000");
    m.init();

    write_ram(m, 0x3, 0x700, 0x35);
    write_ram(m, 0x5, 0x700, 0x53);
    REQUIRE(music5000.synth().ram()[0x700] == 0x35);
    REQUIRE(music3000.synth().ram()[0x700] == 0x53);

    auto& port = m.machine().memory().audio_source_port();
    REQUIRE(port.field_info(2)->name == "Music5000");
    REQUIRE(port.field_info(3)->name == "Music3000");
    REQUIRE(port.field_info(2)->bipolar);
    REQUIRE(port.field_info(3)->group_name == "Music 3000");
}

TEST_CASE("hybrid-music-5000 shares the bus with the SCSI host adapter",
          "[music5000][extension][scsi]") {
    Music5000Machine m;
    auto& synth = m.add_unit("", "music5000");
    m.registry().register_extension(with_default_id(AcornScsiHostAdapter::create()));
    m.init();

    auto& bus = m.machine().memory().one_mhz_bus();
    REQUIRE(bus.is_claimed(AcornScsiHostAdapter::kBaseOffset));
    REQUIRE(bus.is_jim_page_claimed(0x3E));

    write_ram(m, 0x3, 0x770, 0x08);
    REQUIRE(synth.synth().ram()[0x770] == 0x08);
    // SCSI's registers still answer at &FC40-&FC43 (not the float value of
    // an unclaimed address on every one of them).
    bool any_driven = false;
    for (uint16_t a = 0xFC40; a <= 0xFC43; ++a) {
        any_driven = any_driven || m.read(a) != 0xFF;
    }
    REQUIRE(any_driven);
}

TEST_CASE("hybrid-music-5000 rejects an unknown unit", "[music5000][extension]") {
    Music5000Machine m;
    m.add_unit("music-500", "music500");
    REQUIRE_THROWS_AS(m.init(), std::runtime_error);
}

TEST_CASE("hybrid-music-5000 level mapping: sixteen full-scale channels fill int16",
          "[music5000][extension][level]") {
    REQUIRE(HybridMusic5000::to_int16(0) == 0);
    REQUIRE(HybridMusic5000::to_int16(16 * 8159) == 32767);
    REQUIRE(HybridMusic5000::to_int16(-16 * 8159) == -32767);
    // One full-scale left channel: 8159 x 32767 / 130544 = 2047.94.
    REQUIRE(HybridMusic5000::to_int16(8159) == 2048);
    REQUIRE(HybridMusic5000::to_int16(-8031) == -2016);  // 2015.94
}

TEST_CASE("hybrid-music-5000 sound reaches audio field 2 (M1 hold)", "[music5000][extension][audio]") {
    Music5000Machine m;
    m.add_unit("", "music5000");
    m.init();

    // Column 0: wave 0 positive full magnitude, amplitude 0x80, hard left,
    // frequency 0x800000 (a wrap every second sample latches the amplitude).
    for (uint16_t i = 0; i < 128; ++i) {
        write_ram(m, 0x3, i, 0xFF);
    }
    write_ram(m, 0x3, 0x760, 0x80);
    write_ram(m, 0x3, 0x770, 0x08);
    write_ram(m, 0x3, 0x720, 0x80);

    m.machine().memory().enable_audio_output();
    m.machine().run(20'000);

    std::vector<AudioSample> samples(1024);
    const size_t count = m.machine().memory().audio_buffer->read(samples.data(), samples.size());
    REQUIRE(count > 400);
    // Past the first few samples every frame carries the held full-scale
    // left channel: to_int16(8159) = 2048 left, 0 right.
    const uint32_t expected = AudioSample::field_2x16bit(2048, 0);
    for (size_t i = 8; i < count; ++i) {
        INFO("frame " << i);
        REQUIRE(samples[i].sources[2] == expected);
        REQUIRE(samples[i].sources[3] == 0);
    }
}
