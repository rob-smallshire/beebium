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

#include "Music5000Synth.hpp"

#include <beebium/extension/AudioSourcePort.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/OneMHzBusDevice.hpp>
#include <beebium/extension/PeripheralExtension.hpp>

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
// 1 MHz bus, and its output fills one field of the machine's audio frames:
// field 2 for the Music 5000, field 3 for the Music 3000.
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

    // AudioFieldSource: the latest sample, as a (left, right) 2 x int16 field.
    //
    // TEMPORARY (milestone M1): this holds the most recent 46875 Hz sample
    // for each 48 kHz frame, nearest-sample, with no output filter. M2
    // replaces it with the analogue output filter and a polyphase resampler.
    uint32_t take_field() override { return latest_field_; }

    // The unit the "unit" parameter selects ("music-5000" when absent).
    Unit unit() const;

    // DAC current units to int16 at the design level mapping.
    static int16_t to_int16(int32_t current);

    // Inspection, for tests.
    const Music5000Synth& synth() const { return synth_; }
    uint64_t frames_rendered() const { return frames_rendered_; }

private:
    Music5000Synth synth_;
    // 1 MHz ticks to 46875 Hz samples: add 3 per tick, one sample per 64.
    uint32_t tick_accumulator_ = 0;
    uint64_t frames_rendered_ = 0;
    uint32_t latest_field_ = 0;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_HYBRID_MUSIC_5000_HPP
