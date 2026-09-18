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

// Baseband-recovery experiment for the SN76489 sampled-sound technique.
//
// This is the WAV-emitting companion to the regression test
// tests/test_sn76489_sample_playback.cpp: both drive the real chip and measure
// through the same shared header (tests/sn76489_audio_analysis.hpp), so the
// numbers match. The test guards the behaviour in CI; this tool exists to
// produce listenable WAVs and printed numbers for the findings note and for
// ad-hoc listening.
//
// Build (no CMake needed; the chip is header-only apart from Sn76489.cpp):
//   c++ -std=c++20 -O2 -I src/core/include -I tests \
//       tools/audio-analysis/sn76489_baseband_experiment.cpp \
//       src/core/src/Sn76489.cpp -o <out>/sn76489_baseband_experiment
//
// Usage:
//   sn76489_baseband_experiment <wav_output_dirpath>

#include "sn76489_audio_analysis.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace beebium::audio_analysis;

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
    uint32_t fmt_len = 16;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&chunk, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
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

void run(const std::string& out_dirpath, uint16_t period, double test_freq,
         double update_hz, double seconds) {
    auto real = capture_sample_player(period, test_freq, update_hz, seconds);
    auto ideal = ideal_reference(period, test_freq, update_hz, seconds);

    double real_base = goertzel_amplitude(real, test_freq, kSampleRate);
    double ideal_base = goertzel_amplitude(ideal, test_freq, kSampleRate);
    double real_oob = out_of_band_fraction(real, kSampleRate, 7500.0);
    double ideal_oob = out_of_band_fraction(ideal, kSampleRate, 7500.0);

    // Samples are on the chip's full-scale (see Sn76489::FULL_SCALE); scale to
    // [-1, 1] for the WAV. FULL_SCALE/2 is the AC amplitude of a full square.
    auto to_unit = [](std::vector<double> v) {
        for (double& s : v) s /= (beebium::Sn76489::FULL_SCALE / 2.0);
        return v;
    };
    char name[256];
    std::snprintf(name, sizeof(name), "%s/beebium_period%u_%gk.wav", out_dirpath.c_str(),
                  period, test_freq / 1000.0);
    write_wav(name, to_unit(real), kSampleRate);
    std::snprintf(name, sizeof(name), "%s/ideal_period%u_%gk.wav", out_dirpath.c_str(),
                  period, test_freq / 1000.0);
    write_wav(name, to_unit(ideal), kSampleRate);

    std::printf("  period %-2u  baseband real=%.5f ideal=%.5f  recovery=%.4f  "
                "out-of-band real=%.3f ideal=%.5f\n",
                period, real_base, ideal_base, real_base / (ideal_base + 1e-12),
                real_oob, ideal_oob);
}

}  // namespace

int main(int argc, char** argv) {
    std::string out_dirpath = (argc > 1) ? argv[1] : ".";
    const double test_freq = 1000.0;
    const double update_hz = 15000.0;
    const double seconds = 1.0;

    std::printf("SN76489 baseband-recovery experiment (test tone %.0f Hz, "
                "volume update %.0f Hz, %.1fs)\n",
                test_freq, update_hz, seconds);
    run(out_dirpath, 1, test_freq, update_hz, seconds);
    run(out_dirpath, 4, test_freq, update_hz, seconds);
    std::printf("  wrote beebium_period*.wav and ideal_period*.wav to %s\n",
                out_dirpath.c_str());
    return 0;
}
