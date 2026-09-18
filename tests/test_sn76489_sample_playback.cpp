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

// Regression tests for SN76489 sampled-sound playback (issue #82).
//
// The sample player parks a tone at a short period so its square wave is an
// ultrasonic carrier, then rewrites that channel's volume at the sample rate;
// the PCM emerges at baseband because the local mean of the carrier tracks the
// volume, and the decimation to the output rate must not fold the carrier back
// into the audible band. These tests assert that the baseband survives and that
// the output is not dominated by aliased carrier, without harming ordinary audio.
//
// The measurement harness is exercised for its own soundness: the ideal
// reference model must PASS the same spectral criteria the real chip is held to,
// so a criterion that could never fail (or never pass) is caught.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sn76489_audio_analysis.hpp"

using namespace beebium;
using namespace beebium::audio_analysis;

namespace {

// Encode a 1 kHz sine by rewriting the volume at this rate; matches the 15 kHz
// class of the scarybeasts players.
constexpr double kTestFreqHz = 1000.0;
constexpr double kUpdateHz = 15000.0;
constexpr double kSeconds = 0.5;
constexpr double kBandHz = 7500.0;  // out-of-band threshold

// Acceptance thresholds (chosen from phase-1 measurements; see
// docs/discussion/audio-sample-playback-82.md).
constexpr double kMinRecoveryRatio = 0.7;   // real baseband vs ideal baseband
constexpr double kMaxRealOutOfBand = 0.10;  // real chip carrier residual
constexpr double kMaxIdealOutOfBand = 0.02;  // positive control: a good signal

}  // namespace

// Positive control: the analysis harness reports a faithful (unipolar,
// FIR-decimated) signal as good. If this ever fails, the criteria below are
// mismeasuring, not the chip. Paired with the real-chip tests -- which fail on
// the current bipolar/point-sampled path -- this proves the harness both can
// pass and can fail.
TEST_CASE("sample playback: analysis harness passes an ideal signal",
          "[sn76489][sample-playback][control]") {
    for (uint16_t period : {uint16_t{1}, uint16_t{4}}) {
        auto ideal = ideal_reference(period, kTestFreqHz, kUpdateHz, kSeconds);
        double base = goertzel_amplitude(ideal, kTestFreqHz, kSampleRate);
        double oob = out_of_band_fraction(ideal, kSampleRate, kBandHz);
        INFO("period " << period << " ideal baseband " << base << " oob " << oob);
        CHECK(base > 0.2);                  // baseband strongly present
        CHECK(oob < kMaxIdealOutOfBand);    // negligible out-of-band energy
    }
}

// A1: the real chip must reconstruct the sampled baseband and must not drown it
// in aliased carrier. Fails on the current path; passes once the output is
// unipolar and the decimation anti-aliases (Steps B and C).
TEST_CASE("sample playback: baseband recovered at period 1",
          "[sn76489][sample-playback]") {
    auto real = capture_sample_player(1, kTestFreqHz, kUpdateHz, kSeconds);
    auto ideal = ideal_reference(1, kTestFreqHz, kUpdateHz, kSeconds);
    double real_base = goertzel_amplitude(real, kTestFreqHz, kSampleRate);
    double ideal_base = goertzel_amplitude(ideal, kTestFreqHz, kSampleRate);
    double recovery = real_base / ideal_base;
    double oob = out_of_band_fraction(real, kSampleRate, kBandHz);
    INFO("recovery " << recovery << " (real " << real_base << " ideal " << ideal_base
                     << ") out-of-band " << oob);
    CHECK(recovery >= kMinRecoveryRatio);
    CHECK(oob < kMaxRealOutOfBand);
}

TEST_CASE("sample playback: baseband recovered at period 4",
          "[sn76489][sample-playback]") {
    auto real = capture_sample_player(4, kTestFreqHz, kUpdateHz, kSeconds);
    auto ideal = ideal_reference(4, kTestFreqHz, kUpdateHz, kSeconds);
    double real_base = goertzel_amplitude(real, kTestFreqHz, kSampleRate);
    double ideal_base = goertzel_amplitude(ideal, kTestFreqHz, kSampleRate);
    double recovery = real_base / ideal_base;
    double oob = out_of_band_fraction(real, kSampleRate, kBandHz);
    INFO("recovery " << recovery << " (real " << real_base << " ideal " << ideal_base
                     << ") out-of-band " << oob);
    CHECK(recovery >= kMinRecoveryRatio);
    CHECK(oob < kMaxRealOutOfBand);
}

// A2: ordinary audio must not be harmed. A plain ~1 kHz square at two volumes
// must keep its fundamental frequency and its AC loudness (after DC removal).
TEST_CASE("sample playback: ordinary tone loudness and pitch preserved",
          "[sn76489][sample-playback]") {
    // Divider 125 -> 4 MHz / (32 * 125) = 1000 Hz nominal.
    struct Case { uint8_t volume; double expected_rms; };
    for (Case c : {Case{0, kVolumeAmplitude[0] / 127.0},
                   Case{8, kVolumeAmplitude[8] / 127.0}}) {
        auto s = capture_plain_tone(125, c.volume, kSeconds);
        double peak = peak_frequency(s, kSampleRate);
        double ac = rms(s);
        INFO("volume " << int(c.volume) << " peak " << peak << " Hz rms " << ac
                       << " expected " << c.expected_rms);
        // Fundamental within 1.5% of nominal (the tone frequency must not shift).
        CHECK(std::abs(peak - 1000.0) < 15.0);
        // AC amplitude within 12% (loudness must not change).
        CHECK(ac == Catch::Approx(c.expected_rms).epsilon(0.12));
    }
}

// Headroom: no single channel, at any volume or period, may reach the int16
// clamp rail. The anti-alias filter's step overshoot (up to ~11%) rides above
// the full-scale high level, so the emitted samples must carry headroom above
// FULL_SCALE; if they did not, a full-volume square would clip on every edge --
// hard clipping on the loudest, most common game sound.
TEST_CASE("sample playback: full-volume tone does not clip",
          "[sn76489][sample-playback]") {
    Sn76489 chip(kClockHz, kSampleRate);
    AudioBuffer buffer(static_cast<size_t>(kSampleRate * 2));
    // ~625 Hz square at volume 0 (max): divider 200.
    program_tone(chip, 0, 200, 0);
    chip.write(0x80 | (3 << 4) | 15);
    chip.write(0x80 | (5 << 4) | 15);
    chip.write(0x80 | (7 << 4) | 15);
    const uint64_t total_ticks = static_cast<uint64_t>(kCpuTickHz);  // 1 s
    for (uint64_t t = 0; t < total_ticks; ++t) chip.tick(buffer);

    std::vector<AudioSample> samples(buffer.available());
    size_t count = buffer.read(samples.data(), samples.size());
    REQUIRE(count > 1000);
    size_t at_rail = 0;
    int16_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
        int16_t v = sn_tone0(samples[i]);
        peak = std::max<int16_t>(peak, v);
        if (v >= 32767 || v <= -32768) at_rail++;
    }
    INFO("peak " << peak << " / FULL_SCALE " << Sn76489::FULL_SCALE
                 << "; samples at the int16 clamp rail: " << at_rail << " / " << count);
    CHECK(at_rail == 0);
    // The overshoot rides above full scale but well within int16.
    CHECK(peak > Sn76489::FULL_SCALE);
}

// A2 alias guard: a high tone must not acquire audible alias lines low in the
// band. The guard band sits below the tone's fundamental, where a clean high
// tone has little energy; point-sampled aliasing would deposit spurs there.
TEST_CASE("sample playback: high tone acquires no low-band alias lines",
          "[sn76489][sample-playback]") {
    // Divider 13 -> ~9.6 kHz tone.
    auto s = capture_plain_tone(13, 0, kSeconds);
    double guard = band_energy_fraction(s, kSampleRate, 500.0, 7000.0);
    INFO("guard-band [0.5-7 kHz] energy fraction " << guard);
    CHECK(guard < 0.15);
}
