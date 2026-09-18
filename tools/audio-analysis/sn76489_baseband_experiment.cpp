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

// Baseband-recovery experiment for the SN76489 sampled-sound technique.
//
// A sample player parks a tone at a very short period so its square wave is
// ultrasonic (a "carrier"), then rewrites that channel's volume register at the
// sample rate. On real hardware the chip output is effectively unipolar, so the
// short-term mean of the carrier tracks the volume register and the PCM
// waveform emerges at baseband once downstream analogue filtering removes the
// carrier.
//
// This harness drives the REAL beebium Sn76489 exactly as a player would: tone 0
// period 1 (carrier at 125 kHz), all other channels silent, and tone 0's volume
// register stepped at a chosen update rate to encode a pure sine. It captures
// the 48 kHz output and measures how much of the intended sine survives at
// baseband (Goertzel at the test frequency) versus the total signal energy.
//
// For contrast it runs an offline "ideal" reference chip that is identical
// except its output is UNIPOLAR (level(v) when the flip-flop is high, a fixed
// silence level when low) and is decimated 250 kHz -> 48 kHz through a linear-
// phase FIR low-pass instead of point sampling. The gap between the two numbers
// is the defect the issue is chasing.
//
// Build (no CMake needed; the chip only depends on header-only std code):
//   c++ -std=c++20 -O2 -I src/core/include \
//       tools/audio-analysis/sn76489_baseband_experiment.cpp \
//       src/core/src/Sn76489.cpp -o <out>/sn76489_baseband_experiment
//
// Usage:
//   sn76489_baseband_experiment <wav_output_dirpath>

#include "beebium/AudioBuffer.hpp"
#include "beebium/devices/Sn76489.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kClockHz = 4'000'000;
constexpr uint32_t kSampleRate = 48'000;
constexpr uint32_t kCpuTickHz = 2'000'000;   // Sn76489::tick() is called at 2 MHz
constexpr uint32_t kInternalHz = 250'000;    // chip internal update rate

// One VOLUME_TABLE amplitude per attenuation code, mirroring Sn76489.hpp so the
// offline reference and the encoder agree with the real chip's quantisation.
constexpr std::array<int, 16> kVolumeAmplitude = {
    127, 101, 80, 64, 51, 40, 32, 25, 20, 16, 13, 10, 8, 6, 5, 0};

// Choose the volume code whose bipolar peak amplitude best matches a target in
// [0, 127]. This is the encoder a naive player-agnostic test uses; the point is
// not fidelity of encoding but whether the *chip* reproduces the requested
// envelope at all.
uint8_t amplitude_to_volume_code(double target_amplitude) {
    int best_code = 15;
    double best_err = 1e18;
    for (int code = 0; code < 16; ++code) {
        double err = std::abs(static_cast<double>(kVolumeAmplitude[code]) - target_amplitude);
        if (err < best_err) {
            best_err = err;
            best_code = code;
        }
    }
    return static_cast<uint8_t>(best_code);
}

// Minimal 16-bit mono PCM WAV writer (samples already in [-1, 1]).
void write_wav(const std::string& path, const std::vector<double>& samples, uint32_t rate) {
    std::vector<int16_t> pcm(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        double v = samples[i];
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        pcm[i] = static_cast<int16_t>(std::lround(v * 32767.0));
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return;
    }
    uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    uint32_t chunk = 36 + data_bytes;
    uint16_t fmt = 1, channels = 1, bits = 16, block_align = 2;
    uint32_t byte_rate = rate * block_align;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&chunk, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    uint32_t fmt_len = 16;
    std::fwrite(&fmt_len, 4, 1, f);
    std::fwrite(&fmt, 2, 1, f);
    std::fwrite(&channels, 2, 1, f);
    std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byte_rate, 4, 1, f);
    std::fwrite(&block_align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data_bytes, 4, 1, f);
    std::fwrite(pcm.data(), sizeof(int16_t), pcm.size(), f);
    std::fclose(f);
}

// Goertzel magnitude of one bin, normalised so a unit-amplitude sine reads ~1.0.
double goertzel_amplitude(const std::vector<double>& x, double freq, double rate) {
    const size_t n = x.size();
    double w = 2.0 * M_PI * freq / rate;
    double cw = std::cos(w), sw = std::sin(w);
    double coeff = 2.0 * cw;
    double s0 = 0, s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; ++i) {
        s0 = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    double real = s1 - s2 * cw;
    double imag = s2 * sw;
    return 2.0 * std::sqrt(real * real + imag * imag) / static_cast<double>(n);
}

double rms(const std::vector<double>& x) {
    double acc = 0;
    for (double v : x) acc += v * v;
    return std::sqrt(acc / static_cast<double>(x.size()));
}

double mean(const std::vector<double>& x) {
    double acc = 0;
    for (double v : x) acc += v;
    return acc / static_cast<double>(x.size());
}

struct RunResult {
    std::vector<double> samples;  // normalised to roughly [-1, 1]
    double baseband_amp;          // Goertzel at the test frequency
    double total_rms;
    double dc;
};

// Drive the real beebium chip: tone 0 carrier at period 1, volume rewritten at
// update_hz to encode a sine of test_freq. Returns the captured 48 kHz output.
RunResult run_real_chip(double test_freq, double update_hz, double seconds) {
    beebium::Sn76489 chip(kClockHz, kSampleRate);
    beebium::AudioBuffer buffer(static_cast<size_t>(kSampleRate * (seconds + 1.0)));

    // Carrier: tone 0 period 1. Latch reg 0 low nibble = 1, data byte high bits = 0.
    chip.write(0x80 | 0x01);  // latch tone0 freq, low nibble 1
    chip.write(0x00);         // data byte: high bits 0  -> frequency = 1
    // Silence tones 1, 2 and noise.
    chip.write(0x80 | (3 << 4) | 15);  // tone1 volume = 15
    chip.write(0x80 | (5 << 4) | 15);  // tone2 volume = 15
    chip.write(0x80 | (7 << 4) | 15);  // noise volume = 15

    const uint64_t total_ticks = static_cast<uint64_t>(seconds * kCpuTickHz);
    const double ticks_per_update = static_cast<double>(kCpuTickHz) / update_hz;
    uint64_t next_update_tick = 0;

    for (uint64_t t = 0; t < total_ticks; ++t) {
        if (t >= next_update_tick) {
            double phase = 2.0 * M_PI * test_freq * (static_cast<double>(t) / kCpuTickHz);
            double target = 0.5 * (std::sin(phase) + 1.0) * 127.0;  // 0..127 envelope
            uint8_t code = amplitude_to_volume_code(target);
            chip.write(0x80 | (1 << 4) | code);  // tone0 volume
            next_update_tick += static_cast<uint64_t>(ticks_per_update);
        }
        chip.tick(buffer);
    }

    RunResult r;
    std::vector<beebium::AudioSample> raw(buffer.available());
    size_t got = buffer.read(raw.data(), raw.size());
    r.samples.reserve(got);
    for (size_t i = 0; i < got; ++i) {
        uint8_t tone0 = static_cast<uint8_t>((raw[i].sources[0] >> 24) & 0xFF);
        r.samples.push_back((static_cast<double>(tone0) - 128.0) / 127.0);
    }
    r.dc = mean(r.samples);
    r.baseband_amp = goertzel_amplitude(r.samples, test_freq, kSampleRate);
    r.total_rms = rms(r.samples);
    return r;
}

// Windowed-sinc low-pass FIR, cutoff in Hz at the 250 kHz internal rate.
std::vector<double> make_lowpass_fir(double cutoff_hz, double rate, int taps) {
    std::vector<double> h(taps);
    double fc = cutoff_hz / rate;  // normalised
    int m = taps - 1;
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
        double n = i - m / 2.0;
        double sinc = (n == 0.0) ? 2.0 * fc
                                 : std::sin(2.0 * M_PI * fc * n) / (M_PI * n);
        double win = 0.54 - 0.46 * std::cos(2.0 * M_PI * i / m);  // Hamming
        h[i] = sinc * win;
        sum += h[i];
    }
    for (double& v : h) v /= sum;  // unity DC gain
    return h;
}

// Offline "ideal" reference: identical carrier and volume schedule, but UNIPOLAR
// output (level(v) high, fixed silence level low) generated at 250 kHz, FIR
// low-passed, then decimated to 48 kHz. This is what a faithful chip + proper
// decimation would produce.
RunResult run_ideal_reference(double test_freq, double update_hz, double seconds,
                              double fir_cutoff_hz) {
    const uint64_t internal_ticks = static_cast<uint64_t>(seconds * kInternalHz);
    std::vector<double> internal;
    internal.reserve(internal_ticks);

    // Tone 0 period 1: flip-flop toggles every internal tick -> 125 kHz square.
    bool flip = false;
    uint8_t code = 15;
    const double ticks_per_update = static_cast<double>(kInternalHz) / update_hz;
    uint64_t next_update_tick = 0;
    // Unipolar: high level proportional to amplitude code, low level = 0.
    for (uint64_t t = 0; t < internal_ticks; ++t) {
        if (t >= next_update_tick) {
            double phase = 2.0 * M_PI * test_freq * (static_cast<double>(t) / kInternalHz);
            double target = 0.5 * (std::sin(phase) + 1.0) * 127.0;
            code = amplitude_to_volume_code(target);
            next_update_tick += static_cast<uint64_t>(ticks_per_update);
        }
        flip = !flip;
        double level = flip ? (kVolumeAmplitude[code] / 127.0) : 0.0;
        internal.push_back(level);
    }

    // FIR low-pass at 250 kHz.
    std::vector<double> fir = make_lowpass_fir(fir_cutoff_hz, kInternalHz, 129);
    std::vector<double> filtered(internal.size(), 0.0);
    int taps = static_cast<int>(fir.size());
    for (size_t i = 0; i < internal.size(); ++i) {
        double acc = 0;
        for (int k = 0; k < taps; ++k) {
            long idx = static_cast<long>(i) - k;
            if (idx >= 0) acc += fir[k] * internal[static_cast<size_t>(idx)];
        }
        filtered[i] = acc;
    }

    // Decimate 250 kHz -> 48 kHz by fractional accumulate-and-average, splitting
    // the border sample into fractions exactly as beebjit's resampler does so the
    // output rate is the true 48 kHz rather than an integer-ratio approximation.
    RunResult r;
    double ratio = static_cast<double>(kInternalHz) / kSampleRate;  // 5.208...
    double acc = 0, count = 0;
    for (size_t i = 0; i < filtered.size(); ++i) {
        double value = filtered[i];
        count += 1.0;
        if (count < ratio) {
            acc += value;
            continue;
        }
        double leftover = count - ratio;
        acc += (1.0 - leftover) * value;
        r.samples.push_back(acc / ratio);
        acc = leftover * value;
        count = leftover;
    }
    // Centre for a fair baseband/DC comparison (real chip is centred at 128).
    double dc = mean(r.samples);
    for (double& v : r.samples) v -= dc;
    r.dc = dc;
    r.baseband_amp = goertzel_amplitude(r.samples, test_freq, kSampleRate);
    r.total_rms = rms(r.samples);
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    std::string out_dirpath = (argc > 1) ? argv[1] : ".";
    const double test_freq = 1000.0;   // 1 kHz baseband tone
    const double update_hz = 15000.0;  // sample update rate used by the player
    const double seconds = 1.0;
    const double fir_cutoff_hz = 7200.0;  // matches beebjit's default filter_cutoff

    RunResult real = run_real_chip(test_freq, update_hz, seconds);
    RunResult ideal = run_ideal_reference(test_freq, update_hz, seconds, fir_cutoff_hz);

    write_wav(out_dirpath + "/beebium_period1_1khz.wav", real.samples, kSampleRate);
    write_wav(out_dirpath + "/ideal_period1_1khz.wav", ideal.samples, kSampleRate);

    std::printf("SN76489 baseband-recovery experiment\n");
    std::printf("  carrier: tone0 period 1 (125 kHz), test tone %.0f Hz, "
                "volume update %.0f Hz, %.1fs\n",
                test_freq, update_hz, seconds);
    std::printf("\n");
    std::printf("  %-28s %12s %12s %12s\n", "model", "baseband(1k)", "total RMS",
                "baseband/RMS");
    std::printf("  %-28s %12.5f %12.5f %12.4f\n", "beebium real chip",
                real.baseband_amp, real.total_rms,
                real.baseband_amp / (real.total_rms + 1e-12));
    std::printf("  %-28s %12.5f %12.5f %12.4f\n", "ideal unipolar + FIR",
                ideal.baseband_amp, ideal.total_rms,
                ideal.baseband_amp / (ideal.total_rms + 1e-12));
    std::printf("\n");
    std::printf("  baseband recovery ratio (real / ideal): %.4f\n",
                real.baseband_amp / (ideal.baseband_amp + 1e-12));
    std::printf("  wrote %s/beebium_period1_1khz.wav and ideal_period1_1khz.wav\n",
                out_dirpath.c_str());
    return 0;
}
