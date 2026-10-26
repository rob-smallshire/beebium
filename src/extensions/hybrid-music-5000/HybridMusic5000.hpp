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

#ifndef BEEBIUM_EXTENSION_HYBRID_MUSIC_5000_HPP
#define BEEBIUM_EXTENSION_HYBRID_MUSIC_5000_HPP

#include "Music5000OutputFilter.hpp"
#include "Music5000Resampler.hpp"
#include "Music5000Synth.hpp"

#include <beebium/extension/AudioSourcePort.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/OneMHzBusDevice.hpp>
#include <beebium/extension/PeripheralExtension.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace beebium {

// The Hybrid Music 5000 synthesiser, or its Music 3000 expander, on the 1 MHz
// bus (docs/discussion/music5000-design.md).
//
// The board answers to JIM pages 0x30-0x3F (Music 5000) or 0x50-0x5F (Music
// 3000), chosen by the "unit" parameter, and is write-only. Its DSP
// (Music5000Synth) runs at 46875 Hz, three samples for every 64 ticks of the
// 1 MHz bus. Each sample goes, per side, through the analogue output filter
// (Music5000OutputFilter), the level mapping, and the 46875 to 48000 Hz
// resampler (Music5000Resampler), into a short ring that one field of the
// machine's audio frames draws from: field 2 for the Music 5000, field 3 for
// the Music 3000. Both clocks derive from the same 2 MHz, so the ring neither
// fills nor empties in steady state.
class HybridMusic5000 : public PeripheralExtension,
                        public OneMHzBusDevice,
                        public AudioFieldSource {
public:
    enum class Unit { Music5000, Music3000 };

    // Sixteen channels in phase at full scale on the left (encode) side, the
    // largest frame the DSP can produce, maps to int16 full scale (design
    // 3.4.2), so one channel at full scale is about 1/16 of it.
    static constexpr int32_t kFullScaleCurrent = 16 * 8159;

    HybridMusic5000();
    ~HybridMusic5000() override;

    static std::unique_ptr<HybridMusic5000> create();

    // PeripheralExtension
    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"1mhz-bus"};
        return deps;
    }
    std::span<const std::string_view> provides() const override { return {}; }
    void init(ExtensionContext& ctx) override;
    void shutdown() override {}

    // OneMHzBusDevice. Nothing in FRED; JIM is write-only (hardware-model.md
    // 1.4), so jim_read keeps the floating default.
    uint8_t read(uint16_t /*offset*/) override { return 0xFF; }
    void write(uint16_t /*offset*/, uint8_t /*value*/) override {}
    void jim_write(uint8_t page, uint8_t offset, uint8_t value) override {
        synth_.write(page, offset, value);
    }
    void tick() override;
    void reset(ResetCause /*cause*/) override { synth_.reset(); }

    // AudioFieldSource: the next 48 kHz sample, as a (left, right) 2 x int16
    // field. With none ready (only at start-up) the last one is repeated, as
    // the DAC and filter hold their output.
    uint32_t take_field() override;

    // The unit the "unit" parameter selects ("music-5000" when absent).
    Unit unit() const;

    // DAC current units to int16 at the design level mapping.
    static int16_t to_int16(double current);

    // Inspection, for tests.
    const Music5000Synth& synth() const { return synth_; }
    uint64_t frames_rendered() const { return frames_rendered_; }
    uint64_t samples_produced() const { return samples_produced_; }
    // take_field() calls that found the ring empty once delivering, and
    // samples overwritten in a full ring while delivering.
    uint64_t underruns() const { return underruns_; }
    uint64_t overruns() const { return overruns_; }

private:
    static constexpr std::size_t kRingSize = 16;
    // Delivery starts (and restarts after an underrun) once this many samples
    // are waiting, so the two 48 kHz clocks' small phase wander is absorbed.
    static constexpr std::size_t kPrimeLevel = kRingSize / 2;

    void produce(uint32_t field);

    Music5000Synth synth_;
    // 1 MHz ticks to 46875 Hz samples: add 3 per tick, one sample per 64.
    uint32_t tick_accumulator_ = 0;
    uint64_t frames_rendered_ = 0;

    std::array<Music5000OutputFilter, 2> filters_{Music5000OutputFilter{Music5000Resampler::kInputRateHz},
                                                  Music5000OutputFilter{Music5000Resampler::kInputRateHz}};
    std::array<Music5000Resampler, 2> resamplers_{};

    // 48 kHz fields waiting for the frame assembler. When it is not taking
    // them (audio output disabled) the oldest are overwritten, and delivery
    // resumes from the prime level.
    std::array<uint32_t, kRingSize> ring_{};
    std::size_t ring_read_ = 0;
    std::size_t ring_count_ = 0;
    uint32_t last_field_ = 0;
    bool delivering_ = false;
    uint64_t samples_produced_ = 0;
    uint64_t underruns_ = 0;
    uint64_t overruns_ = 0;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_HYBRID_MUSIC_5000_HPP
