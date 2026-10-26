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

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <numbers>
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

namespace {

// Set up column 0 (counter 0): amplitude 0x80 (so the DAC code is the wave
// byte's magnitude), the given control and 24-bit frequency.
void set_channel_0(Music5000Machine& m, uint8_t control, uint32_t freq) {
    write_ram(m, 0x3, 0x760, 0x80);
    write_ram(m, 0x3, 0x770, control);
    write_ram(m, 0x3, 0x700, static_cast<uint8_t>(freq));
    write_ram(m, 0x3, 0x710, static_cast<uint8_t>(freq >> 8));
    write_ram(m, 0x3, 0x720, static_cast<uint8_t>(freq >> 16));
}

std::vector<AudioSample> capture(Music5000Machine& m, uint64_t cycles) {
    m.machine().memory().enable_audio_output();
    m.machine().run(cycles);
    std::vector<AudioSample> samples(48000);
    samples.resize(m.machine().memory().audio_buffer->read(samples.data(), samples.size()));
    return samples;
}

int16_t left_of(uint32_t field) { return static_cast<int16_t>(field & 0xFFFF); }
int16_t right_of(uint32_t field) { return static_cast<int16_t>(field >> 16); }

}  // namespace

TEST_CASE("hybrid-music-5000 a held full-scale channel settles to its DC level",
          "[music5000][extension][audio]") {
    Music5000Machine m;
    m.add_unit("", "music5000");
    m.init();
    for (uint16_t i = 0; i < 128; ++i) {
        write_ram(m, 0x3, i, 0xFF);  // positive, full magnitude
    }
    set_channel_0(m, 0x08, 0x800000);  // hard left; wraps latch the amplitude

    const std::vector<AudioSample> samples = capture(m, 400'000);  // 0.2 s
    REQUIRE(samples.size() > 9000);
    // Full-scale left (encode) current 8159 at unity DC gain:
    // 8159 x 32767 / (16 x 8159) = 2047.94.
    for (std::size_t i = samples.size() - 200; i < samples.size(); ++i) {
        INFO("frame " << i);
        REQUIRE(std::abs(left_of(samples[i].sources[2]) - 2048) <= 1);
        REQUIRE(right_of(samples[i].sources[2]) == 0);
        REQUIRE(samples[i].sources[3] == 0);
    }
}

TEST_CASE("hybrid-music-5000 a 1 kHz tone arrives at 1 kHz and the filter's level",
          "[music5000][extension][audio]") {
    Music5000Machine m;
    m.add_unit("", "music5000");
    m.init();

    // Wave 0: a sine in the board's sign/log form, each byte the code whose
    // decode (right side) current is nearest |sin| x full scale.
    std::array<double, 128> currents{};
    for (int i = 0; i < 128; ++i) {
        const double s = std::sin(2.0 * std::numbers::pi * i / 128.0);
        const double target = std::abs(s) * 8031.0;
        uint8_t best = 0;
        for (uint8_t c = 1; c < 128; ++c) {
            if (std::abs(Music5000Synth::decode_current(c) - target)
                < std::abs(Music5000Synth::decode_current(best) - target)) {
                best = c;
            }
        }
        write_ram(m, 0x3, static_cast<uint16_t>(i), static_cast<uint8_t>((s >= 0.0 ? 0x80 : 0x00) | best));
        currents[i] = (s >= 0.0 ? 1.0 : -1.0) * Music5000Synth::decode_current(best);
    }
    // 1 kHz: freq = 1000 x 2^24 / 46875 = 357913.9 -> 357914 (bit 0 clear).
    set_channel_0(m, 0x00, 357914);

    const std::vector<AudioSample> samples = capture(m, 600'000);  // 0.3 s
    constexpr std::size_t kN = 4800;  // 0.1 s: 10 Hz bins, 1 kHz in bin 100
    REQUIRE(samples.size() > kN + 1000);
    const std::size_t start = samples.size() - kN;

    std::vector<std::complex<double>> bins(kN / 2 + 1);
    for (std::size_t k = 0; k < bins.size(); ++k) {
        std::complex<double> sum = 0.0;
        const double w = 2.0 * std::numbers::pi * static_cast<double>(k) / kN;
        for (std::size_t n = 0; n < kN; ++n) {
            sum += static_cast<double>(right_of(samples[start + n].sources[2]))
                 * std::polar(1.0, -w * static_cast<double>(n));
        }
        bins[k] = sum;
    }
    std::size_t peak = 1;
    for (std::size_t k = 1; k < bins.size(); ++k) {
        if (std::abs(bins[k]) > std::abs(bins[peak])) {
            peak = k;
        }
    }
    REQUIRE(peak == 100);

    // The DAC sequence's fundamental, from the wave table, scaled to int16
    // and attenuated by the filter at 1 kHz: -0.62 dB (hardware-model.md
    // appendix).
    std::complex<double> fundamental = 0.0;
    for (int i = 0; i < 128; ++i) {
        fundamental += currents[i] * std::polar(1.0, -2.0 * std::numbers::pi * i / 128.0);
    }
    const double dac_amplitude = std::abs(fundamental) * 2.0 / 128.0;
    const double expected = dac_amplitude * 32767.0 / HybridMusic5000::kFullScaleCurrent
                          * std::pow(10.0, -0.62 / 20.0);
    const double measured = std::abs(bins[peak]) * 2.0 / kN;
    INFO("expected " << expected << ", measured " << measured);
    REQUIRE(std::abs(20.0 * std::log10(measured / expected)) < 0.1);
}

TEST_CASE("hybrid-music-5000 delivers 48000 samples per emulated second without repeats",
          "[music5000][extension][audio][rate]") {
    Music5000Machine m;
    auto& synth = m.add_unit("", "music5000");
    m.init();
    m.machine().memory().enable_audio_output();

    m.machine().run(2'000'000);
    const uint64_t start_cycles = m.machine().cycle_count();
    const uint64_t start_samples = synth.samples_produced();
    for (int i = 0; i < 30; ++i) {
        m.machine().run(200'000);
        // Drain the frames so the machine's audio buffer never fills.
        std::vector<AudioSample> sink(48000);
        m.machine().memory().audio_buffer->read(sink.data(), sink.size());
    }
    const uint64_t cycles = m.machine().cycle_count() - start_cycles;
    const uint64_t produced = synth.samples_produced() - start_samples;

    // 48000 per 2,000,000 cycles, within the part-instruction run() overshoot.
    const double expected = static_cast<double>(cycles) * 48000.0 / 2'000'000.0;
    REQUIRE(std::abs(static_cast<double>(produced) - expected) <= 2.0);
    REQUIRE(synth.underruns() == 0);
    REQUIRE(synth.overruns() == 0);
}
