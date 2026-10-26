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

#ifndef BEEBIUM_MUSIC5000_RESAMPLER_HPP
#define BEEBIUM_MUSIC5000_RESAMPLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace beebium {

// Converts the Music 5000's 46875 Hz output to the machine's 48000 Hz audio
// rate, one side at a time. The ratio is exactly 128/125.
//
// A polyphase interpolator: conceptually upsample by 128 to 6 MHz, low-pass,
// and keep every 125th sample, computing only the outputs kept. The
// prototype low-pass is a Kaiser-windowed sinc designed once at construction:
//
//   - 5120 taps at 6 MHz, i.e. 40 taps per phase (one 40-tap dot product per
//     output sample);
//   - cutoff 23437.5 Hz, midway between the passband edge at 20 kHz and the
//     stop-band edge at 26875 Hz, where the first image of 20 kHz content
//     lands (46875 - 20000);
//   - Kaiser beta 9.0.
//
// Measured: passband ripple 0.0005 dB to 20 kHz, stop-band attenuation 91 dB
// from 26875 Hz. The signal reaching it has already been through the output
// filter (-57 dB at 20 kHz, -62 dB at the input Nyquist), so what lies in the
// transition band is far below the stop band's own floor.
//
// Group delay: (5120 - 1) / 2 samples at 6 MHz = 426.6 us, i.e. 19.996 input
// samples or 20.475 output samples.
//
// Exact rate: the n-th output is at input time n x 125/128 samples, kept in
// integer units of 1/128 sample, so N inputs yield exactly
// ceil(N x 128/125) outputs, with no drift however long it runs.
//
// Free of Beebium dependencies, so it can be tested and reused on its own.
class Music5000Resampler {
public:
    static constexpr uint32_t kUp = 128;
    static constexpr uint32_t kDown = 125;
    static constexpr std::size_t kTapsPerPhase = 40;
    static constexpr double kInputRateHz = 46875.0;
    static constexpr double kOutputRateHz = kInputRateHz * kUp / kDown;  // 48000
    static constexpr double kPassbandEdgeHz = 20000.0;
    static constexpr double kStopbandEdgeHz = kInputRateHz - kPassbandEdgeHz;
    static constexpr double kKaiserBeta = 9.0;

    Music5000Resampler();

    // Take one input sample and pass each output sample it completes (none,
    // one or two) to emit(double).
    template <typename Emit>
    void push(double x, Emit&& emit) {
        head_ = (head_ + kTapsPerPhase - 1) % kTapsPerPhase;
        history_[head_] = x;
        while (offset_ < kUp) {
            emit(output_at_phase(offset_));
            offset_ += kDown;
        }
        offset_ -= kUp;
    }

    void reset();

    // Group delay of the interpolating filter, in seconds.
    static constexpr double group_delay_seconds() {
        return (static_cast<double>(kUp * kTapsPerPhase) - 1.0) / 2.0 / (kInputRateHz * kUp);
    }

    // The prototype filter's taps, for tests.
    const std::vector<double>& prototype() const { return prototype_; }

private:
    // y = sum over k of h[phase + k x 128] x[i - k], with the newest input
    // at history_[head_].
    double output_at_phase(uint32_t phase) const {
        const double* h = &polyphase_[static_cast<std::size_t>(phase) * kTapsPerPhase];
        double sum = 0.0;
        std::size_t index = head_;
        for (std::size_t k = 0; k < kTapsPerPhase; ++k) {
            sum += h[k] * history_[index];
            index = (index + 1 == kTapsPerPhase) ? 0 : index + 1;
        }
        return sum;
    }

    std::vector<double> prototype_;   // kUp x kTapsPerPhase taps at 6 MHz
    std::vector<double> polyphase_;   // [phase][k] = prototype_[phase + k x kUp]
    std::array<double, kTapsPerPhase> history_{};
    std::size_t head_ = 0;
    // Position of the next output after the newest input, in 1/kUp samples.
    uint32_t offset_ = 0;
};

}  // namespace beebium

#endif  // BEEBIUM_MUSIC5000_RESAMPLER_HPP
