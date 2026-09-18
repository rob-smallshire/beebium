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

// Shared signal-analysis helpers for evaluating SN76489 sample playback
// (issue #82). Used by the sample-playback regression tests and by the
// standalone tools/audio-analysis experiment so both measure identically.
//
// The "sample player" technique parks a tone at a short period so its square
// wave is an ultrasonic carrier, then rewrites that channel's volume register
// at the sample rate; the PCM waveform emerges at baseband as the local mean of
// the carrier tracks the volume. These helpers drive the real chip that way,
// measure how much baseband survives and how much energy aliases out of band,
// and provide an offline "ideal" reference (unipolar output, FIR-decimated) for
// contrast.

#pragma once

#include "beebium/AudioBuffer.hpp"
#include "beebium/devices/Sn76489.hpp"
#include "sn76489_channels.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

namespace beebium::audio_analysis {

constexpr uint32_t kClockHz = 4'000'000;
constexpr uint32_t kSampleRate = 48'000;
constexpr uint32_t kCpuTickHz = 2'000'000;  // Sn76489::tick() is called at 2 MHz
constexpr uint32_t kInternalHz = 250'000;   // chip internal update rate

// VOLUME_TABLE amplitudes mirrored from Sn76489.hpp so the offline reference and
// the encoder quantise exactly as the real chip does.
constexpr std::array<int, 16> kVolumeAmplitude = {
    127, 101, 80, 64, 51, 40, 32, 25, 20, 16, 13, 10, 8, 6, 5, 0};

inline uint8_t amplitude_to_volume_code(double target_amplitude) {
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

inline double mean(const std::vector<double>& x) {
    double acc = 0;
    for (double v : x) acc += v;
    return x.empty() ? 0.0 : acc / static_cast<double>(x.size());
}

inline double rms(const std::vector<double>& x) {
    double acc = 0;
    for (double v : x) acc += v * v;
    return x.empty() ? 0.0 : std::sqrt(acc / static_cast<double>(x.size()));
}

// Goertzel magnitude of one bin, normalised so a unit-amplitude sine reads ~1.0.
inline double goertzel_amplitude(const std::vector<double>& x, double freq, double rate) {
    const size_t n = x.size();
    if (n == 0) return 0.0;
    double w = 2.0 * std::numbers::pi * freq / rate;
    double cw = std::cos(w), sw = std::sin(w);
    double coeff = 2.0 * cw;
    double s1 = 0, s2 = 0;
    for (double v : x) {
        double s0 = v + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    double real = s1 - s2 * cw;
    double imag = s2 * sw;
    return 2.0 * std::hypot(real, imag) / static_cast<double>(n);
}

// In-place iterative radix-2 FFT. size must be a power of two.
inline void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * std::numbers::pi / static_cast<double>(len);
        std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                std::complex<double> u = a[i + k];
                std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// Fraction of spectral energy above band_hz. DC is removed first, a Hann window
// applied, and the largest power-of-two window of samples used.
inline double out_of_band_fraction(const std::vector<double>& signal, double rate,
                                   double band_hz) {
    size_t n = 1;
    while (n * 2 <= signal.size()) n <<= 1;
    if (n < 2) return 0.0;
    double dc = mean(signal);
    std::vector<std::complex<double>> spec(n);
    for (size_t i = 0; i < n; ++i) {
        double win = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (n - 1));  // Hann
        spec[i] = std::complex<double>((signal[i] - dc) * win, 0.0);
    }
    fft(spec);
    double total = 0, oob = 0;
    for (size_t k = 1; k < n / 2; ++k) {  // skip DC bin
        double f = static_cast<double>(k) * rate / static_cast<double>(n);
        double p = std::norm(spec[k]);
        total += p;
        if (f > band_hz) oob += p;
    }
    return total > 0 ? oob / total : 0.0;
}

// Fraction of spectral energy within [lo_hz, hi_hz]. DC removed, Hann windowed.
inline double band_energy_fraction(const std::vector<double>& signal, double rate,
                                   double lo_hz, double hi_hz) {
    size_t n = 1;
    while (n * 2 <= signal.size()) n <<= 1;
    if (n < 2) return 0.0;
    double dc = mean(signal);
    std::vector<std::complex<double>> spec(n);
    for (size_t i = 0; i < n; ++i) {
        double win = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (n - 1));
        spec[i] = std::complex<double>((signal[i] - dc) * win, 0.0);
    }
    fft(spec);
    double total = 0, band = 0;
    for (size_t k = 1; k < n / 2; ++k) {
        double f = static_cast<double>(k) * rate / static_cast<double>(n);
        double p = std::norm(spec[k]);
        total += p;
        if (f >= lo_hz && f <= hi_hz) band += p;
    }
    return total > 0 ? band / total : 0.0;
}

// Frequency of the strongest bin (excluding DC), in Hz.
inline double peak_frequency(const std::vector<double>& signal, double rate) {
    size_t n = 1;
    while (n * 2 <= signal.size()) n <<= 1;
    if (n < 2) return 0.0;
    double dc = mean(signal);
    std::vector<std::complex<double>> spec(n);
    for (size_t i = 0; i < n; ++i) {
        double win = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (n - 1));
        spec[i] = std::complex<double>((signal[i] - dc) * win, 0.0);
    }
    fft(spec);
    size_t best = 1;
    double best_p = -1;
    for (size_t k = 1; k < n / 2; ++k) {
        double p = std::norm(spec[k]);
        if (p > best_p) {
            best_p = p;
            best = k;
        }
    }
    return static_cast<double>(best) * rate / static_cast<double>(n);
}

// --- Driving the real chip ---

// Latch a tone channel's frequency (10-bit divider) and volume, plus silence the
// others. channel is 0..2.
inline void program_tone(Sn76489& chip, int channel, uint16_t divider, uint8_t volume) {
    uint8_t freq_reg = static_cast<uint8_t>(channel * 2);
    uint8_t vol_reg = static_cast<uint8_t>(channel * 2 + 1);
    chip.write(0x80 | (freq_reg << 4) | (divider & 0x0F));  // latch: low nibble
    chip.write(static_cast<uint8_t>((divider >> 4) & 0x3F));  // data: high 6 bits
    chip.write(0x80 | (vol_reg << 4) | (volume & 0x0F));
}

// Drive tone 0 as an ultrasonic carrier of the given period, rewriting its
// volume at update_hz to encode a sine of test_freq. Returns the 48 kHz output,
// centred (mean removed) and scaled to roughly [-1, 1].
inline std::vector<double> capture_sample_player(uint16_t period, double test_freq,
                                                 double update_hz, double seconds) {
    Sn76489 chip(kClockHz, kSampleRate);
    AudioBuffer buffer(static_cast<size_t>(kSampleRate * (seconds + 1.0)));
    program_tone(chip, 0, period, 0);
    // Silence tones 1, 2 and noise.
    chip.write(0x80 | (3 << 4) | 15);
    chip.write(0x80 | (5 << 4) | 15);
    chip.write(0x80 | (7 << 4) | 15);

    const uint64_t total_ticks = static_cast<uint64_t>(seconds * kCpuTickHz);
    const double ticks_per_update = static_cast<double>(kCpuTickHz) / update_hz;
    uint64_t next_update_tick = 0;
    for (uint64_t t = 0; t < total_ticks; ++t) {
        if (t >= next_update_tick) {
            double phase = 2.0 * std::numbers::pi * test_freq * (static_cast<double>(t) / kCpuTickHz);
            double target = 0.5 * (std::sin(phase) + 1.0) * 127.0;
            chip.write(0x80 | (1 << 4) | amplitude_to_volume_code(target));
            next_update_tick += static_cast<uint64_t>(ticks_per_update);
        }
        chip.tick(buffer);
    }

    std::vector<AudioSample> raw(buffer.available());
    size_t got = buffer.read(raw.data(), raw.size());
    std::vector<double> out;
    out.reserve(got);
    for (size_t i = 0; i < got; ++i) {
        out.push_back(static_cast<double>(sn_tone0(raw[i])));
    }
    double dc = mean(out);
    for (double& v : out) v -= dc;
    return out;
}

// Drive a plain steady tone 0 (period + volume fixed) for `seconds`, capturing
// the 48 kHz output centred. Used to check ordinary audio is intact.
inline std::vector<double> capture_plain_tone(uint16_t period, uint8_t volume,
                                              double seconds) {
    Sn76489 chip(kClockHz, kSampleRate);
    AudioBuffer buffer(static_cast<size_t>(kSampleRate * (seconds + 1.0)));
    program_tone(chip, 0, period, volume);
    chip.write(0x80 | (3 << 4) | 15);
    chip.write(0x80 | (5 << 4) | 15);
    chip.write(0x80 | (7 << 4) | 15);
    const uint64_t total_ticks = static_cast<uint64_t>(seconds * kCpuTickHz);
    for (uint64_t t = 0; t < total_ticks; ++t) chip.tick(buffer);
    std::vector<AudioSample> raw(buffer.available());
    size_t got = buffer.read(raw.data(), raw.size());
    // Normalise by half full-scale so a volume-0 square has unit AC amplitude,
    // matching the pre-16-bit loudness convention (the AC swing is FULL_SCALE/2).
    const double norm = Sn76489::FULL_SCALE / 2.0;
    std::vector<double> out;
    out.reserve(got);
    for (size_t i = 0; i < got; ++i) {
        out.push_back(static_cast<double>(sn_tone0(raw[i])) / norm);
    }
    double dc = mean(out);
    for (double& v : out) v -= dc;
    return out;
}

// --- Offline "ideal" reference ---

inline std::vector<double> make_lowpass_fir(double cutoff_hz, double rate, int taps) {
    std::vector<double> h(taps);
    double fc = cutoff_hz / rate;
    int m = taps - 1;
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
        double x = i - m / 2.0;
        double sinc = (x == 0.0) ? 2.0 * fc : std::sin(2.0 * std::numbers::pi * fc * x) / (std::numbers::pi * x);
        double win = 0.54 - 0.46 * std::cos(2.0 * std::numbers::pi * i / m);  // Hamming
        h[i] = sinc * win;
        sum += h[i];
    }
    for (double& v : h) v /= sum;
    return h;
}

// A faithful chip + proper decimation: unipolar output (level(v) high, silence
// low) generated at 250 kHz, FIR low-passed, fractional-average decimated to
// 48 kHz. Same carrier and volume schedule as capture_sample_player.
inline std::vector<double> ideal_reference(uint16_t period, double test_freq,
                                           double update_hz, double seconds,
                                           double fir_cutoff_hz = 7200.0) {
    const uint64_t internal_ticks = static_cast<uint64_t>(seconds * kInternalHz);
    std::vector<double> internal;
    internal.reserve(internal_ticks);
    uint16_t counter = period;
    bool flip = false;
    uint8_t code = 15;
    const double ticks_per_update = static_cast<double>(kInternalHz) / update_hz;
    uint64_t next_update_tick = 0;
    for (uint64_t t = 0; t < internal_ticks; ++t) {
        if (t >= next_update_tick) {
            double phase = 2.0 * std::numbers::pi * test_freq * (static_cast<double>(t) / kInternalHz);
            double target = 0.5 * (std::sin(phase) + 1.0) * 127.0;
            code = amplitude_to_volume_code(target);
            next_update_tick += static_cast<uint64_t>(ticks_per_update);
        }
        if (counter > 0) {
            --counter;
        } else {
            flip = !flip;
            counter = (period == 0) ? 1024 : period;
        }
        // Unipolar, on the same full-scale law the chip emits.
        double high = (code >= 15) ? 0.0
                                   : Sn76489::FULL_SCALE * std::pow(10.0, -0.1 * code);
        internal.push_back(flip ? high : 0.0);
    }

    std::vector<double> fir = make_lowpass_fir(fir_cutoff_hz, kInternalHz, 129);
    int taps = static_cast<int>(fir.size());
    std::vector<double> filtered(internal.size(), 0.0);
    for (size_t i = 0; i < internal.size(); ++i) {
        double acc = 0;
        for (int k = 0; k < taps; ++k) {
            long idx = static_cast<long>(i) - k;
            if (idx >= 0) acc += fir[k] * internal[static_cast<size_t>(idx)];
        }
        filtered[i] = acc;
    }

    std::vector<double> out;
    double ratio = static_cast<double>(kInternalHz) / kSampleRate;
    double acc = 0, count = 0;
    for (double value : filtered) {
        count += 1.0;
        if (count < ratio) {
            acc += value;
            continue;
        }
        double leftover = count - ratio;
        acc += (1.0 - leftover) * value;
        out.push_back(acc / ratio);
        acc = leftover * value;
        count = leftover;
    }
    double dc = mean(out);
    for (double& v : out) v -= dc;
    return out;
}

}  // namespace beebium::audio_analysis
